#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float pointSize [[point_size]];
    float4 color;
};

struct Uniforms {
    float4x4 proj;
};

vertex VertexOut color_vertex(VertexIn in [[stage_in]],
                              constant Uniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    out.position = uniforms.proj * float4(in.position, 0.0, 1.0);
    out.pointSize = 4.0;
    out.color = in.color;
    return out;
}

fragment float4 color_fragment(VertexOut in [[stage_in]])
{
    return in.color;
}
