#include <metal_stdlib>
using namespace metal;

// -------------------------------------------------------------
// STRUCTS & GLOBALS
// -------------------------------------------------------------

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
};

constant float SagA_rs = 1.269e10;
constant float D_LAMBDA = 1e7;
constant float ESCAPE_R = 1e30f;

struct Ray {
    float x, y, z, r, theta, phi;
    float dr, dtheta, dphi;
    float E, L;
};

// -------------------------------------------------------------
// COMPUTE KERNEL (Raytracing)
// -------------------------------------------------------------

Ray initRay(float3 pos, float3 dir) {
    Ray ray;
    ray.x = pos.x; ray.y = pos.y; ray.z = pos.z;
    ray.r = length(pos);
    ray.theta = acos(pos.z / ray.r);
    ray.phi = atan2(pos.y, pos.x);

    float dx = dir.x, dy = dir.y, dz = dir.z;
    ray.dr     = sin(ray.theta)*cos(ray.phi)*dx + sin(ray.theta)*sin(ray.phi)*dy + cos(ray.theta)*dz;
    ray.dtheta = (cos(ray.theta)*cos(ray.phi)*dx + cos(ray.theta)*sin(ray.phi)*dy - sin(ray.theta)*dz) / ray.r;
    ray.dphi   = (-sin(ray.phi)*dx + cos(ray.phi)*dy) / (ray.r * sin(ray.theta));

    ray.L = ray.r * ray.r * sin(ray.theta) * ray.dphi;
    float f = 1.0 - SagA_rs / ray.r;
    float dt_dL = sqrt((ray.dr*ray.dr)/f + ray.r*ray.r*(ray.dtheta*ray.dtheta + sin(ray.theta)*sin(ray.theta)*ray.dphi*ray.dphi));
    ray.E = f * dt_dL;

    return ray;
}

bool intercept(Ray ray, float rs) {
    return ray.r <= rs;
}

bool interceptObject(Ray ray, constant Objects& objs, thread float4& objectColor, thread float3& hitCenter, thread float& hitRadius) {
    float3 P = float3(ray.x, ray.y, ray.z);
    for (int i = 0; i < objs.numObjects; ++i) {
        float3 center = objs.objPosRadius[i].xyz;
        float radius = objs.objPosRadius[i].w;
        if (distance(P, center) <= radius) {
            objectColor = objs.objColor[i];
            hitCenter = center;
            hitRadius = radius;
            return true;
        }
    }
    return false;
}

void geodesicRHS(Ray ray, thread float3& d1, thread float3& d2) {
    float r = ray.r, theta = ray.theta;
    float dr = ray.dr, dtheta = ray.dtheta, dphi = ray.dphi;
    float f = 1.0 - SagA_rs / r;
    float dt_dL = ray.E / f;

    d1 = float3(dr, dtheta, dphi);
    d2.x = - (SagA_rs / (2.0 * r*r)) * f * dt_dL * dt_dL
         + (SagA_rs / (2.0 * r*r * f)) * dr * dr
         + r * (dtheta*dtheta + sin(theta)*sin(theta)*dphi*dphi);
    d2.y = -2.0*dr*dtheta/r + sin(theta)*cos(theta)*dphi*dphi;
    d2.z = -2.0*dr*dphi/r - 2.0*cos(theta)/(sin(theta)) * dtheta * dphi;
}

void rk4Step(thread Ray& ray, float dL) {
    float3 k1a, k1b;
    geodesicRHS(ray, k1a, k1b);

    ray.r      += dL * k1a.x;
    ray.theta  += dL * k1a.y;
    ray.phi    += dL * k1a.z;
    ray.dr     += dL * k1b.x;
    ray.dtheta += dL * k1b.y;
    ray.dphi   += dL * k1b.z;

    ray.x = ray.r * sin(ray.theta) * cos(ray.phi);
    ray.y = ray.r * sin(ray.theta) * sin(ray.phi);
    ray.z = ray.r * cos(ray.theta);
}

bool crossesEquatorialPlane(float3 oldPos, float3 newPos, constant Disk& disk) {
    bool crossed = (oldPos.y * newPos.y < 0.0);
    float r = length(float2(newPos.x, newPos.z));
    return crossed && (r >= disk.disk_r1 && r <= disk.disk_r2);
}

kernel void geodesic(texture2d<float, access::write> outImage [[texture(0)]],
                     constant Camera& cam [[buffer(0)]],
                     constant Disk& disk [[buffer(1)]],
                     constant Objects& objs [[buffer(2)]],
                     uint2 pix [[thread_position_in_grid]])
{
    int WIDTH  = cam.moving ? 200 : 200;
    int HEIGHT = cam.moving ? 150 : 150;

    if (pix.x >= (uint)WIDTH || pix.y >= (uint)HEIGHT) return;

    float u = (2.0 * (pix.x + 0.5) / (float)WIDTH - 1.0) * cam.aspect * cam.tanHalfFov;
    float v = (1.0 - 2.0 * (pix.y + 0.5) / (float)HEIGHT) * cam.tanHalfFov;
    float3 dir = normalize(u * cam.camRight.xyz - v * cam.camUp.xyz + cam.camForward.xyz);
    Ray ray = initRay(cam.camPos.xyz, dir);

    float4 color = float4(0.0);
    float3 prevPos = float3(ray.x, ray.y, ray.z);
    float lambda = 0.0;

    bool hitBlackHole = false;
    bool hitDisk      = false;
    bool hitObject    = false;

    float4 objectColor = float4(0.0);
    float3 hitCenter = float3(0.0);
    float hitRadius = 0.0;

    int steps = cam.moving ? 5000 : 60000;

    for (int i = 0; i < steps; ++i) {
        if (intercept(ray, SagA_rs)) { hitBlackHole = true; break; }
        
        // Adaptive step size: take larger steps far away, smaller steps near the horizon
        float adaptive_dL = D_LAMBDA * (1.0 + (ray.r - SagA_rs) / (SagA_rs * 2.0));
        adaptive_dL = min(adaptive_dL, D_LAMBDA * 50.0); // Cap step size to avoid jumping over objects
        
        rk4Step(ray, adaptive_dL);
        lambda += adaptive_dL;

        float3 newPos = float3(ray.x, ray.y, ray.z);
        if (prevPos.y * newPos.y <= 0.0) { // Only check disk if crossing equatorial plane
            if (crossesEquatorialPlane(prevPos, newPos, disk)) { hitDisk = true; break; }
        }
        
        if (interceptObject(ray, objs, objectColor, hitCenter, hitRadius)) { hitObject = true; break; }
        
        prevPos = newPos;
        if (ray.r > ESCAPE_R) break;
    }

    if (hitDisk) {
        float r = length(float3(ray.x, ray.y, ray.z)) / disk.disk_r2;
        float3 diskColor = float3(1.0, r, 0.2);
        color = float4(diskColor, r);
    } else if (hitBlackHole) {
        color = float4(0.0, 0.0, 0.0, 1.0);
    } else if (hitObject) {
        float3 P = float3(ray.x, ray.y, ray.z);
        float3 N = normalize(P - hitCenter);
        float3 V = normalize(cam.camPos.xyz - P);
        float ambient = 0.1;
        float diff = max(dot(N, V), 0.0);
        float intensity = ambient + (1.0 - ambient) * diff;
        float3 shaded = objectColor.rgb * intensity;
        color = float4(shaded, objectColor.a);
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
    return float4(0.5, 0.5, 0.5, 0.7); // translucent blue lines
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
