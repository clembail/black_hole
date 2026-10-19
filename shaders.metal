#include <metal_stdlib>
using namespace metal;

// =============================================================
// TUNING CONSTANTS
// =============================================================

// Adaptive step size is proportional to the current radius, clamped to
// [MIN_DL, MAX_DL]. Small steps in the strong-field region keep the
// integration accurate near the horizon; large steps in the far field
// let rays escape in a handful of iterations instead of tens of thousands.
constant float STEP_FRAC = 0.02f;
constant float MIN_DL    = 1e7f;
constant float MAX_DL    = 1e10f;

// Hard safety cap. Measured worst case is ~700 steps across all camera
// positions, so this never truncates a ray in practice.
constant int MAX_STEPS = 2048;

constant float SagA_rs = 1.269e10f;

// =============================================================
// STRUCTS
// =============================================================

struct Camera {
    float4 camPos;
    float4 camRight;
    float4 camUp;
    float4 camForward;
    float tanHalfFov;
    float aspect;
    bool moving;
    int   _pad4;
};

struct Disk {
    float disk_r1;
    float disk_r2;
    float disk_num;
    float thickness;
};

struct Objects {
    int numObjects;
    float4 objPosRadius[16];
    float4 objColor[16];
    float mass[16];
    // Radial band containing every object, so per-ray object tests can be
    // skipped entirely while the ray is outside it.
    float objMinR;
    float objMaxR;
    float _pad0;
    float _pad1;
};

// Reduced geodesic state: the motion is planar, so only (r, phi, dr, dphi)
// are needed instead of the full 3D spherical (r, theta, phi, dr, dtheta, dphi).
struct State {
    float r, phi, dr, dphi;
};

// =============================================================
// GEODESIC INTEGRATION
// =============================================================
//
// Schwarzschild null geodesics are planar (central force), so the trajectory
// is integrated in the orbital plane. This drops the theta equation, the
// sin(theta)/cos(theta) terms and the 1/sin(theta) coordinate singularity,
// and cuts the work per derivative evaluation from 6 to 4 states.

inline State geodesicRHS(float r, float dr, float dphi, float E, float rs) {
    float f  = 1.0f - rs / r;
    float dt = E / f;

    State d;
    d.r    = dr;
    d.phi  = dphi;
    d.dr   = -(rs / (2.0f * r * r)) * f * dt * dt
           + (rs / (2.0f * r * r * f)) * dr * dr
           + r * dphi * dphi;
    d.dphi = -2.0f * dr * dphi / r;
    return d;
}

// Explicit Euler. Cheap, and accurate enough with the step sizes used here
// because the crossing test below interpolates rather than relying on the
// post-step sample.
inline void stepEuler(thread State& s, float dL, float E, float rs) {
    State k = geodesicRHS(s.r, s.dr, s.dphi, E, rs);
    s.r    += dL * k.r;
    s.phi  += dL * k.phi;
    s.dr   += dL * k.dr;
    s.dphi += dL * k.dphi;
}

// Midpoint (RK2). Two derivative evaluations per step, notably more robust
// than Euler around the photon sphere for a modest cost.
inline void stepMidpoint(thread State& s, float dL, float E, float rs) {
    State k1 = geodesicRHS(s.r, s.dr, s.dphi, E, rs);
    float h  = 0.5f * dL;
    State k2 = geodesicRHS(s.r + h * k1.r, s.dr + h * k1.dr,
                           s.dphi + h * k1.dphi, E, rs);
    s.r    += dL * k2.r;
    s.phi  += dL * k2.phi;
    s.dr   += dL * k2.dr;
    s.dphi += dL * k2.dphi;
}

// =============================================================
// COMPUTE KERNEL (Raytracing)
// =============================================================

kernel void geodesic(texture2d<float, access::write> outImage [[texture(0)]],
                     constant Camera& cam [[buffer(0)]],
                     constant Disk& disk [[buffer(1)]],
                     constant Objects& objs [[buffer(2)]],
                     uint2 pix [[thread_position_in_grid]])
{
    const int WIDTH  = (int)outImage.get_width();
    const int HEIGHT = (int)outImage.get_height();
    const float rs   = SagA_rs;

    float u = (2.0f * (pix.x + 0.5f) / (float)WIDTH - 1.0f) * cam.aspect * cam.tanHalfFov;
    float v = (1.0f - 2.0f * (pix.y + 0.5f) / (float)HEIGHT) * cam.tanHalfFov;
    float3 dir = normalize(u * cam.camRight.xyz - v * cam.camUp.xyz + cam.camForward.xyz);
    float3 p0  = cam.camPos.xyz;

    // --- Orbital plane basis ---------------------------------------------
    // e1 points from the black hole to the camera. e2 is the component of the
    // ray direction perpendicular to e1, so the whole trajectory lies in the
    // plane spanned by (e1, e2).
    float  r0 = length(p0);
    float3 e1 = p0 / r0;

    float3 perp   = dir - e1 * dot(e1, dir);
    float  perpLen = length(perp);
    float3 e2;
    if (perpLen < 1e-8f) {
        // Degenerate (radial) ray: any perpendicular works.
        e2 = normalize(cross(e1, float3(0.0f, 1.0f, 0.0f)));
        if (any(isnan(e2))) e2 = normalize(cross(e1, float3(1.0f, 0.0f, 0.0f)));
    } else {
        e2 = perp / perpLen;
    }

    State s;
    s.r    = r0;
    s.phi  = 0.0f;
    s.dr   = dot(dir, e1);
    s.dphi = dot(dir, e2) / r0;

    // Conserved quantity from the initial conditions.
    float f0 = 1.0f - rs / r0;
    float E  = f0 * sqrt((s.dr * s.dr) / f0 + r0 * r0 * s.dphi * s.dphi);

    // Components of the plane basis along world Y, used to detect crossings
    // of the equatorial (y = 0) disk plane without rebuilding full 3D points.
    float aY = e1.y;
    float bY = e2.y;

    // Escape radius must lie beyond every object, otherwise rays that are
    // still travelling outwards would stop before they can hit anything.
    const float escapeR = max(2.0f * r0, objs.objMaxR * 1.05f);

    float4 color = float4(0.0f);
    bool hitBlackHole = false;
    bool hitDisk      = false;
    bool hitObject    = false;

    float4 objectColor = float4(0.0f);
    float3 hitCenter   = float3(0.0f);

    float prevY = r0 * aY;   // sin/cos of phi = 0 at the start
    float prevR = r0;

    for (int i = 0; i < MAX_STEPS; ++i) {
        if (s.r <= rs) { hitBlackHole = true; break; }

        float dL = clamp(STEP_FRAC * s.r, MIN_DL, MAX_DL);
        stepMidpoint(s, dL, E, rs);

        float cph = cos(s.phi);
        float sph = sin(s.phi);
        float y   = s.r * (aY * cph + bY * sph);

        // --- Accretion disk: crossing of the equatorial plane ------------
        // The crossing point is interpolated, not sampled after the step.
        // Without this, a large step can jump clean over the thin annulus and
        // leave speckles along the disk edge.
        if (prevY * y < 0.0f) {
            float t   = prevY / (prevY - y);
            float rc  = mix(prevR, s.r, t);
            float rho = rc;   // on the y = 0 plane, cylindrical radius == r
            if (rho >= disk.disk_r1 && rho <= disk.disk_r2) {
                hitDisk = true;
                s.r     = rc;   // report the crossing radius for shading
                break;
            }
        }

        // --- Objects: only tested while inside their radial band ---------
        if (s.r >= objs.objMinR && s.r <= objs.objMaxR) {
            float3 P = s.r * (e1 * cph + e2 * sph);
            for (int j = 0; j < objs.numObjects; ++j) {
                float3 c   = objs.objPosRadius[j].xyz;
                float  rad = objs.objPosRadius[j].w;
                if (distance_squared(P, c) <= rad * rad) {
                    objectColor = objs.objColor[j];
                    hitCenter   = c;
                    hitObject   = true;
                    break;
                }
            }
            if (hitObject) break;
        }

        prevY = y;
        prevR = s.r;

        // Only stop once the ray is genuinely heading outwards.
        if (s.r > escapeR && s.dr > 0.0f) break;
    }

    // --- Shading ---------------------------------------------------------
    if (hitDisk) {
        float rr = s.r / disk.disk_r2;
        color = float4(1.0f, rr, 0.2f, rr);
    } else if (hitBlackHole) {
        color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (hitObject) {
        float3 P = s.r * (e1 * cos(s.phi) + e2 * sin(s.phi));
        float3 N = normalize(P - hitCenter);
        float3 V = normalize(cam.camPos.xyz - P);
        const float ambient = 0.1f;
        float diff = max(dot(N, V), 0.0f);
        float intensity = ambient + (1.0f - ambient) * diff;
        color = float4(objectColor.rgb * intensity, objectColor.a);
    }

    outImage.write(color, pix);
}

// -------------------------------------------------------------
// GRID RENDERING (Vertex / Fragment)
// -------------------------------------------------------------
struct GridVertexIn {
    float3 position [[attribute(0)]];
};

struct GridVertexOut {
    float4 position [[position]];
};

struct GridUniforms {
    float4x4 viewProj;
};

vertex GridVertexOut grid_vertex(GridVertexIn in [[stage_in]],
                                 constant GridUniforms& uniforms [[buffer(1)]])
{
    GridVertexOut out;
    out.position = uniforms.viewProj * float4(in.position, 1.0);
    return out;
}

fragment float4 grid_fragment(GridVertexOut in [[stage_in]])
{
    return float4(0.5, 0.5, 0.5, 0.7);
}

// -------------------------------------------------------------
// SCREEN QUAD (Vertex / Fragment)
// -------------------------------------------------------------
struct QuadVertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct QuadVertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex QuadVertexOut quad_vertex(QuadVertexIn in [[stage_in]]) {
    QuadVertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 quad_fragment(QuadVertexOut in [[stage_in]],
                              texture2d<float> screenTexture [[texture(0)]])
{
    constexpr sampler texSampler (mag_filter::linear, min_filter::linear);
    return screenTexture.sample(texSampler, in.texCoord);
}
