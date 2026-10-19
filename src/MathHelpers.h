#pragma once
#include <simd/simd.h>

namespace math {

    inline simd::float3 normalize(const simd::float3& v) {
        return simd_normalize(v);
    }
    
    inline simd::float3 cross(const simd::float3& a, const simd::float3& b) {
        return simd_cross(a, b);
    }

    inline simd::float4x4 perspective(float fovRadians, float aspect, float nearZ, float farZ) {
        float ys = 1.f / tanf(fovRadians * 0.5f);
        float xs = ys / aspect;
        float zs = farZ / (nearZ - farZ);

        return simd_matrix_from_rows(
            (simd::float4){xs, 0.0f, 0.0f, 0.0f},
            (simd::float4){0.0f, ys, 0.0f, 0.0f},
            (simd::float4){0.0f, 0.0f, zs, nearZ * zs},
            (simd::float4){0.0f, 0.0f, -1.0f, 0.0f}
        );
    }

    inline simd::float4x4 lookAt(const simd::float3& eye, const simd::float3& center, const simd::float3& up) {
        simd::float3 z = simd_normalize(eye - center);
        simd::float3 x = simd_normalize(simd_cross(up, z));
        simd::float3 y = simd_cross(z, x);

        simd::float4x4 m = simd_matrix_from_rows(
            (simd::float4){x.x, x.y, x.z, -simd_dot(x, eye)},
            (simd::float4){y.x, y.y, y.z, -simd_dot(y, eye)},
            (simd::float4){z.x, z.y, z.z, -simd_dot(z, eye)},
            (simd::float4){0.0f, 0.0f, 0.0f, 1.0f}
        );
        return m;
    }

    inline simd::float4x4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
        float ral = right + left;
        float rsl = right - left;
        float tab = top + bottom;
        float tsb = top - bottom;
        float fan = farZ + nearZ;
        float fsn = farZ - nearZ;

        return simd_matrix_from_rows(
            (simd::float4){2.0f / rsl, 0.0f, 0.0f, -ral / rsl},
            (simd::float4){0.0f, 2.0f / tsb, 0.0f, -tab / tsb},
            (simd::float4){0.0f, 0.0f, -2.0f / fsn, -fan / fsn},
            (simd::float4){0.0f, 0.0f, 0.0f, 1.0f}
        );
    }
}
