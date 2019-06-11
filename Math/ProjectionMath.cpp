// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// warnings from CML
#pragma warning(disable:4267)       //  warning C4267: 'initializing' : conversion from 'size_t' to 'int', possible loss of data

#include "ProjectionMath.h"
#include "Geometry.h"
#include "../Core/Prefix.h"
#include "../Core/SelectConfiguration.h"
#include "../Utility/IteratorUtils.h"
#include <assert.h>
#include <cfloat>
#include <limits>

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
    #include <intrin.h>
    #define HAS_SSE_INSTRUCTIONS
#endif

namespace XLEMath
{
    static Float4x4 InvertWorldToProjection(const Float4x4& input, bool useAccurateInverse)
    {
        if (useAccurateInverse) {
            return cml::inverse(input);
        } else {
            return Inverse(input);
        }
    }

    void CalculateAbsFrustumCorners(
        Float3 frustumCorners[8], const Float4x4& worldToProjection,
        ClipSpaceType clipSpaceType)
    {
            //  So long as we can invert the world to projection matrix accurately, we can 
            //  extract the frustum corners easily. We just need to pass the coordinates
            //  of the corners of clip space through the inverse matrix.
            //
            //  If the matrix inversion is not accurate enough, we can do this by going back to
            //  the source components that built the worldToProjection matrix.
            //  We can easily get the projection top/left/right/bottom from the raw projection matrix
            //  and we can also get the near and far clip from that. The world to view matrix can be
            //  inverted accurately with InvertOrthonormalTransform (and normally we should have the
            //  world to view matrix calculated at higher points in the pipeline). 
            //  So by using those source components, we can calculate the corners without and extra
            //  matrix inversion operations.
        static bool useAccurateInverse = true;      // the normal cml invert is pretty accurate. But sometimes it seems we get a better result with this
        auto projectionToWorld = InvertWorldToProjection(worldToProjection, useAccurateInverse);
        float yAtTop = 1.f;
        float yAtBottom = -1.f;
        if (clipSpaceType == ClipSpaceType::PositiveRightHanded) {
            yAtTop = -1.f;
            yAtBottom = 1.f;
        }
        Float4 v0, v1, v2, v3;
        if (clipSpaceType == ClipSpaceType::StraddlingZero) {
            v0 = projectionToWorld * Float4(-1.f, yAtTop,    -1.f, 1.f);
            v1 = projectionToWorld * Float4(-1.f, yAtBottom, -1.f, 1.f);
            v2 = projectionToWorld * Float4( 1.f, yAtTop,    -1.f, 1.f);
            v3 = projectionToWorld * Float4( 1.f, yAtBottom, -1.f, 1.f);
        } else {
            v0 = projectionToWorld * Float4(-1.f, yAtTop,    0.f, 1.f);
            v1 = projectionToWorld * Float4(-1.f, yAtBottom, 0.f, 1.f);
            v2 = projectionToWorld * Float4( 1.f, yAtTop,    0.f, 1.f);
            v3 = projectionToWorld * Float4( 1.f, yAtBottom, 0.f, 1.f);
        }

        Float4 v4 = projectionToWorld * Float4(-1.f, yAtTop,    1.f, 1.f);
        Float4 v5 = projectionToWorld * Float4(-1.f, yAtBottom, 1.f, 1.f);
        Float4 v6 = projectionToWorld * Float4( 1.f, yAtTop,    1.f, 1.f);
        Float4 v7 = projectionToWorld * Float4( 1.f, yAtBottom, 1.f, 1.f);

        frustumCorners[0] = Truncate(v0) / v0[3];
        frustumCorners[1] = Truncate(v1) / v1[3];
        frustumCorners[2] = Truncate(v2) / v2[3];
        frustumCorners[3] = Truncate(v3) / v3[3];

        frustumCorners[4] = Truncate(v4) / v4[3];
        frustumCorners[5] = Truncate(v5) / v5[3];
        frustumCorners[6] = Truncate(v6) / v6[3];
        frustumCorners[7] = Truncate(v7) / v7[3];
    }
    
#if defined(HAS_SSE_INSTRUCTIONS)

    static inline void TestAABB_SSE_TransCorner(
        __m128 corner0, __m128 corner1, 
        __m128& A0, __m128& A1, __m128& A2,
        float* dst)
    {
        // still need many registers --
        // A0, A1, A2
        // x0, y0, z2
        // x1, y1, z1
        // abuv, cz11
        auto x0 = _mm_dp_ps(A0, corner0, (0xF<<4)|(1<<0));      // L: ~12, T: 0 (varies for different processors)
        auto y0 = _mm_dp_ps(A1, corner0, (0xF<<4)|(1<<1));      // L: ~12, T: 0 (varies for different processors)
        auto z0 = _mm_dp_ps(A2, corner0, (0xF<<4)|(1<<2));      // L: ~12, T: 0 (varies for different processors)

        auto x1 = _mm_dp_ps(A0, corner1, (0xF<<4)|(1<<0));      // L: ~12, T: 0 (varies for different processors)
        auto y1 = _mm_dp_ps(A1, corner1, (0xF<<4)|(1<<1));      // L: ~12, T: 0 (varies for different processors)
        auto z1 = _mm_dp_ps(A2, corner1, (0xF<<4)|(1<<2));      // L: ~12, T: 0 (varies for different processors)

        auto clipSpaceXYZ0 = _mm_add_ps(x0, y0);                // L: 3, T: 1
        auto clipSpaceXYZ1 = _mm_add_ps(x1, y1);                // L: 3, T: 1
        clipSpaceXYZ0 = _mm_add_ps(z0, clipSpaceXYZ0);          // L: 3, T: 1
        clipSpaceXYZ1 = _mm_add_ps(z1, clipSpaceXYZ1);          // L: 3, T: 1
        _mm_store_ps(dst, clipSpaceXYZ0);
        _mm_store_ps(dst + 4, clipSpaceXYZ1);
    }

    __declspec(align(16)) static const unsigned g_zeroZWComponentsInit[] = { 0xffffffff, 0xffffffff, 0, 0 };
    static const auto g_zeroZWComponents = _mm_load_ps((const float*)g_zeroZWComponentsInit);
    static const auto g_signMask = _mm_set1_ps(-0.f);       // -0.f = 1 << 31
        
    static inline void TestAABB_SSE_CalcFlags(
        const float* clipSpaceXYZMem, const float* clipSpaceWMem,
        __m128& andUpper, __m128& andLower,
        __m128& orUpperLower)
    {
        assert((size_t(clipSpaceXYZMem)&0xF) == 0);
        assert((size_t(clipSpaceWMem)&0xF) == 0);

        auto xyz = _mm_load_ps(clipSpaceXYZMem);
        auto w = _mm_load_ps(clipSpaceWMem);

        auto cmp0 = _mm_cmpgt_ps(xyz, w);               // L: 3, T: -

        auto negW = _mm_xor_ps(w, g_signMask);          // L: 1, T: ~0.33 (this will flip the sign of w)
        negW = _mm_and_ps(negW, g_zeroZWComponents);    // L: 1, T: 1

        auto cmp1 = _mm_cmplt_ps(xyz, negW);            // L: 3, T: -

            // apply bitwise "and" and "or" as required...
        andUpper = _mm_and_ps(andUpper, cmp0);          // L: 1, T: ~1
        andLower = _mm_and_ps(andLower, cmp1);          // L: 1, T: ~1

        orUpperLower = _mm_or_ps(orUpperLower, cmp0);   // L: 1, T: .33
        orUpperLower = _mm_or_ps(orUpperLower, cmp1);   // L: 1, T: .33
    }

    static AABBIntersection::Enum TestAABB_SSE(
        const float localToProjection[], 
        const Float3& mins, const Float3& maxs)
    {
        // Perform projection into culling space...

        // We can perform the matrix * vector multiply in three ways:
        //      1. using "SSE4.1" dot product instruction "_mm_dp_ps"
        //      2. using SSE3 vector multiply and horizontal add instructions
        //      2. using "FMA" vector multiply and fused vector add
        //
        // FMA is not supported on Intel chips earlier than Haswell. That's a
        // bit frustrating.
        //
        // The dot production instruction has low throughput but very
        // high latency. That means we need to interleave a number of 
        // transforms in order to get the best performance. Actually, compiler
        // generated optimization should be better for doing that. But 
        // I'm currently using a compiler that doesn't seem to generate that
        // instruction (so, doing it by hand).
        //
        // We can separate the test for each point into 2 parts;
        //      1. the matrix * vector multiply
        //      2. comparing the result against the edges of the frustum
        //
        // The 1st part has a high latency. But the latency values for the
        // second part are much smaller. The second part is much more compact
        // and easier to optimise. It makes sense to do 2 points in parallel,
        // to cover the latency of the 1st part with the calculations from the
        // 2nd part.
        //
        // However, we have a bit of problem with register counts! We need a lot
        // of registers. Visual Studio 2010 is only using 8 xmm registers, which is
        // not really enough. We need 16 registers to do this well. But it seems that
        // we can only have 16 register in x64 mode.
        //
        //       0-3  : matrix
        //       4xyz : clipSpaceXYZ
        //       5xyz : clipSpaceWWW (then -clipSpaceWWW)
        //       6    : utility
        //       7xyz : andUpper
        //       8xyz : andLower
        //       9xyz : orUpperAndLower
        //      10    : abuv
        //      11    : cw11
        //
        // If we want to do multiple corners at the same time, it's just going to
        // increase the registers we need.
        //
        //  What this means is the idea situation depends on the hardware we're
        //  targeting!
        //      1. x86 Haswell+ --> fused-multiply-add
        //      2. x64 --> dot product with 16 xmm registers
        //      3. otherwise, 2-step, transform corners and then clip test
        //
        // One solution is to do the transformation first, and write the result to
        // memory. But this makes it more difficult to cover the latency in the
        // dot product.

        // We can use SSE "shuffle" to load the vectors for each corner.
        //
        //  abc = mins[0,1,2]
        //  uvw = maxs[0,1,2]
        //
        //  r0 = abuv
        //  r1 = cw,1,1
        //
        //  abc, abw
        //  ubc, ubw
        //  avc, avw
        //  uvc, uvw
        //
        //  abc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 0));
        //  ubc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 2));
        //  avc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 0));
        //  uvc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 2));
        //
        //  abw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 0));
        //  ubw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 2));
        //  avw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 0));
        //  uvw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 2));
        //
        // Now we need to do the matrix multiply & perspective divide
        // then we have to compare the results against 0 and 1 and 
        // do some binary comparisons.

        assert((size_t(localToProjection) & 0xf) == 0);
        auto abuv = _mm_set_ps(maxs[1], maxs[0], mins[1], mins[0]);   // (note; using WZYX order)
        auto cw11 = _mm_set_ps(1.f, 1.f, maxs[2], mins[2]);

        __m128 corners[8];
        corners[0] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 0));
        corners[1] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 2));
        corners[2] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 0));
        corners[3] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 2));
        corners[4] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 0));
        corners[5] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 2));
        corners[6] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 0));
        corners[7] = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 2));

        auto A0 = _mm_load_ps(localToProjection +  0);
        auto A1 = _mm_load_ps(localToProjection +  4);
        auto A2 = _mm_load_ps(localToProjection +  8);
        auto A3 = _mm_load_ps(localToProjection + 12);

        __declspec(align(16)) Float4 cornerClipSpaceXYZ[8];
        __declspec(align(16)) Float4 cornerClipW[8];

            //  We want to interleave projection calculations for multiple vectors in 8 registers
            //  We have very few registers. So we need to separate the calculation of the "W" part
            //  This will mean having to duplicate the "shuffle" instruction. But this is a very
            //  cheap instruction.

        TestAABB_SSE_TransCorner(
            corners[0], corners[1],
            A0, A1, A2, &cornerClipSpaceXYZ[0][0]);
        TestAABB_SSE_TransCorner(
            corners[2], corners[3],
            A0, A1, A2, &cornerClipSpaceXYZ[2][0]);
        TestAABB_SSE_TransCorner(
            corners[4], corners[5],
            A0, A1, A2, &cornerClipSpaceXYZ[4][0]);
        TestAABB_SSE_TransCorner(
            corners[6], corners[7],
            A0, A1, A2, &cornerClipSpaceXYZ[6][0]);

            //  Now do the "W" parts.. Do 4 at a time to try to cover latency
        {
            auto abc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 0));
            auto w0 = _mm_dp_ps(A3, abc1, (0xF<<4)|( 0xF));
            auto ubc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 1, 2));
            auto w1 = _mm_dp_ps(A3, ubc1, (0xF<<4)|( 0xF));
            auto avc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 0));
            auto w2 = _mm_dp_ps(A3, avc1, (0xF<<4)|( 0xF));
            auto uvc1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 0, 3, 2));
            auto w3 = _mm_dp_ps(A3, uvc1, (0xF<<4)|( 0xF));

            _mm_store_ps(&cornerClipW[0][0], w0);
            _mm_store_ps(&cornerClipW[1][0], w1);
            _mm_store_ps(&cornerClipW[2][0], w2);
            _mm_store_ps(&cornerClipW[3][0], w3);

            auto abw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 0));
            auto w4 = _mm_dp_ps(A3, abw1, (0xF<<4)|( 0xF));
            auto ubw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 1, 2));
            auto w5 = _mm_dp_ps(A3, ubw1, (0xF<<4)|( 0xF));
            auto avw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 0));
            auto w6 = _mm_dp_ps(A3, avw1, (0xF<<4)|( 0xF));
            auto uvw1 = _mm_shuffle_ps(abuv, cw11, _MM_SHUFFLE(2, 1, 3, 2));
            auto w7 = _mm_dp_ps(A3, uvw1, (0xF<<4)|( 0xF));

            _mm_store_ps(&cornerClipW[4][0], w4);
            _mm_store_ps(&cornerClipW[5][0], w5);
            _mm_store_ps(&cornerClipW[6][0], w6);
            _mm_store_ps(&cornerClipW[7][0], w7);
        }

            // Now compare with screen edges and calculate the bit masks

        __declspec(align(16)) unsigned andInitializer[] = { 0xffffffff, 0xffffffff, 0xffffffff, 0 };
        assert((size_t(andInitializer) & 0xf) == 0);

        auto andUpper = _mm_load_ps((const float*)andInitializer);
        auto andLower = _mm_load_ps((const float*)andInitializer);
        auto orUpperLower = _mm_setzero_ps();

        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[0][0], &cornerClipW[0][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[1][0], &cornerClipW[1][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[2][0], &cornerClipW[2][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[3][0], &cornerClipW[3][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[4][0], &cornerClipW[4][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[5][0], &cornerClipW[5][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[6][0], &cornerClipW[6][0], andUpper, andLower, orUpperLower);
        TestAABB_SSE_CalcFlags(&cornerClipSpaceXYZ[7][0], &cornerClipW[7][0], andUpper, andLower, orUpperLower);

            // Get the final result...

        andUpper        = _mm_hadd_ps(andLower,     andUpper);
        orUpperLower    = _mm_hadd_ps(orUpperLower, orUpperLower);
        andUpper        = _mm_hadd_ps(andUpper,     andUpper);
        orUpperLower    = _mm_hadd_ps(orUpperLower, orUpperLower);
        andUpper        = _mm_hadd_ps(andUpper,     andUpper);

        __declspec(align(16)) unsigned andResult[4];
        __declspec(align(16)) unsigned orUpperLowerResult[4];
        assert((size_t(andResult) & 0xf) == 0);
        assert((size_t(orUpperLowerResult) & 0xf) == 0);

        _mm_store_ps((float*)orUpperLowerResult, orUpperLower);
        _mm_store_ps((float*)andResult, andUpper);

        if (andResult[0])           { return AABBIntersection::Culled; }
        if (orUpperLowerResult[0])  { return AABBIntersection::Boundary; }
        return AABBIntersection::Within;
    }
    
#endif

    static inline Float4 XYZProj(const Float4x4& localToProjection, const Float3 input)
    {
        return localToProjection * Expand(input, 1.f);
    }

    static AABBIntersection::Enum TestAABB_Basic(const Float4x4& localToProjection, const Float3& mins, const Float3& maxs, ClipSpaceType clipSpaceType)
    {
            //  for the box to be culled, all points must be outside of the same bounding box
            //  plane... We can do this in clip space (assuming we can do a fast position transform on
            //  the CPU). We can also do this in world space by finding the planes of the frustum, and
            //  comparing each corner point to each plane.
        Float3 corners[8] = 
        {
            Float3(mins[0], mins[1], mins[2]),
            Float3(maxs[0], mins[1], mins[2]),
            Float3(mins[0], maxs[1], mins[2]),
            Float3(maxs[0], maxs[1], mins[2]),

            Float3(mins[0], mins[1], maxs[2]),
            Float3(maxs[0], mins[1], maxs[2]),
            Float3(mins[0], maxs[1], maxs[2]),
            Float3(maxs[0], maxs[1], maxs[2])
        };

        Float4 projectedCorners[8];
        projectedCorners[0] = XYZProj(localToProjection, corners[0]);
        projectedCorners[1] = XYZProj(localToProjection, corners[1]);
        projectedCorners[2] = XYZProj(localToProjection, corners[2]);
        projectedCorners[3] = XYZProj(localToProjection, corners[3]);
        projectedCorners[4] = XYZProj(localToProjection, corners[4]);
        projectedCorners[5] = XYZProj(localToProjection, corners[5]);
        projectedCorners[6] = XYZProj(localToProjection, corners[6]);
        projectedCorners[7] = XYZProj(localToProjection, corners[7]);

        bool leftAnd = true, rightAnd = true, topAnd = true, bottomAnd = true, nearAnd = true, farAnd = true;
        bool leftOr = false, rightOr = false, topOr = false, bottomOr = false, nearOr = false, farOr = false;
        for (unsigned c=0; c<8; ++c) {
            leftAnd     &= (projectedCorners[c][0] < -projectedCorners[c][3]);
            rightAnd    &= (projectedCorners[c][0] >  projectedCorners[c][3]);
            topAnd      &= (projectedCorners[c][1] < -projectedCorners[c][3]);
            bottomAnd   &= (projectedCorners[c][1] >  projectedCorners[c][3]);
            farAnd      &= (projectedCorners[c][2] >  projectedCorners[c][3]);

            leftOr      |= (projectedCorners[c][0] < -projectedCorners[c][3]);
            rightOr     |= (projectedCorners[c][0] >  projectedCorners[c][3]);
            topOr       |= (projectedCorners[c][1] < -projectedCorners[c][3]);
            bottomOr    |= (projectedCorners[c][1] >  projectedCorners[c][3]);
            farOr       |= (projectedCorners[c][2] >  projectedCorners[c][3]);
        }
        
        if (clipSpaceType == ClipSpaceType::Positive) {
            for (unsigned c=0; c<8; ++c) {
                nearOr      |= (projectedCorners[c][2] < 0.f);
                nearAnd     &= (projectedCorners[c][2] < 0.f);
            }
        } else if (clipSpaceType == ClipSpaceType::StraddlingZero) {
            for (unsigned c=0; c<8; ++c) {
                nearOr      |= (projectedCorners[c][2] < -projectedCorners[c][3]);
                nearAnd     &= (projectedCorners[c][2] < -projectedCorners[c][3]);
            }
        } else {
            assert(0);  // unsupported clip space type
        }
        
        if (leftAnd | rightAnd | topAnd | bottomAnd | nearAnd | farAnd) {
            return AABBIntersection::Culled;
        }
        if (leftOr | rightOr | topOr | bottomOr | nearOr | farOr) {
            return AABBIntersection::Boundary;
        }
        return AABBIntersection::Within;
    }

    AABBIntersection::Enum TestAABB(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
        return TestAABB_Basic(localToProjection, mins, maxs, clipSpaceType);
    }

    AABBIntersection::Enum TestAABB_Aligned(
        const Float4x4& localToProjection, 
        const Float3& mins, const Float3& maxs,
        ClipSpaceType clipSpaceType)
    {
#if defined(HAS_SSE_INSTRUCTIONS)
        assert(clipSpaceType == ClipSpaceType::Positive);
        return TestAABB_SSE(AsFloatArray(localToProjection), mins, maxs);
#else
        return TestAABB(localToProjection, mins, maxs, clipSpaceType);
#endif
    }

    Float4 ExtractMinimalProjection(const Float4x4& projectionMatrix)
    {
        return Float4(projectionMatrix(0,0), projectionMatrix(1,1), projectionMatrix(2,2), projectionMatrix(2,3));
    }

    bool IsOrthogonalProjection(const Float4x4& projectionMatrix)
    {
        // In an orthogonal projection matrix, the 'w' component should
        // be constant for all inputs.
        // Let's compare the bottom row to 0.f to check this
        return      projectionMatrix(3,0) == 0.f
                &&  projectionMatrix(3,1) == 0.f
                &&  projectionMatrix(3,2) == 0.f;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    Float4x4 PerspectiveProjection(
        float verticalFOV, float aspectRatio,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace::Enum coordinateSpace,
        ClipSpaceType clipSpaceType )
    {

            //
            //      Generate a perspective projection matrix with the given
            //      parameters.
            //
            //      Note that we have a few things to consider:
            //
            //          Depth range for homogeneous clip space:
            //              OpenGL defines valid clip space depths as -w<z<w
            //              But in DirectX, we need to use 0<z<w
            //              (in other words, OpenGL straddles 0, while DirectX doesn't)
            //          It's a bit odd, but we're kind of stuck with it.
            //
            //      We're assuming the "camera forward" direction as -Z in camera
            //      space. This is the Collada standard... I'm sure how common that
            //      is.
            //
            //      After transformation, +Z will be away from the viewer.
            //      (ie, increasing Z values mean greater depth)
            //
            //      The caller can choose a left handed or right handed coordinate system
            //      (this will just flip the image horizontally).
            //  
            //      We always use "verticalFOV" and an aspect ratio to define the
            //      viewing angles. This tends to make the most sense to the viewer when
            //      they are (for example) resizing a window. In that case, normally
            //      verticalFOV should stay static, while the aspect ratio will change
            //      (ie horizontal viewing angle will adapt to the dimensions of the window)
            //
            //      BTW, verticalFOV should be in radians, and is the half angle
            //      (ie, it's the angle between the centre ray and the edge of the screen, not
            //      from one edge of the screen to the other)
            //
            //      This code doesn't support skewed or off centre projections for multi-screen
            //      output.
            //      See this link for a generalised transform: 
            //              http://csc.lsu.edu/~kooima/pdfs/gen-perspective.pdf
            //

        const float n = nearClipPlane;
        const float h = n * XlTan(.5f * verticalFOV);
        const float w = h * aspectRatio;
        float l, r;
        const float t = h, b = -h;

        if (coordinateSpace == GeometricCoordinateSpace::LeftHanded) {
            l = w; r = -w;
        } else {
            l = -w; r = w;
        }

        return PerspectiveProjection(l, t, r, b, nearClipPlane, farClipPlane, clipSpaceType);
    }

    Float4x4 PerspectiveProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        ClipSpaceType clipSpaceType )
    {
        const float n = nearClipPlane;
        const float f = farClipPlane;

            // Note --  there's a slight awkward thing here... l, t, r and b
            //          are defined to mean values between -nearClipPlane and +nearClipPlane
            //          it might seem more logical to define them on the range between -1 and 1...?

        Float4x4 result = Identity<Float4x4>();
        result(0,0) =  (2.f * n) / (r-l);
        result(0,2) =  (r+l) / (r-l);

        result(1,1) =  (2.f * n) / (t-b);
        result(1,2) =  (t+b) / (t-b);

        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
                //  This is the D3D view of clip space
                //      0<z/w<1
            result(2,2) =    -(f) / (f-n);            // (note z direction flip here as well as below)
            result(2,3) =  -(f*n) / (f-n);
        } else {
                //  This is the OpenGL view of clip space
                //      -1<z/w<1
            result(2,2) =       -(f+n) / (f-n);
            result(2,3) =   -(2.f*f*n) / (f-n);
        }

        result(3,2) =   -1.f;    // (-1 required to flip space around from -Z camera forward to (z/w) increasing with distance)
        result(3,3) =   0.f;

            //
            //      Both OpenGL & DirectX expect a left-handed coordinate system post-projection
            //          +X is right
            //          +Y is up (ie, coordinates are bottom-up)
            //          +Z is into the screen (increasing values are increasing depth, negative depth values are behind the camera)
            //
            //      But Vulkan uses a right handed coordinate system. In this system, +Y points towards
            //      the bottom of the screen.
            //

        if (clipSpaceType == ClipSpaceType::PositiveRightHanded)
            result(1,1) = -result(1,1);

        return result;
    }
    
    Float4x4 OrthogonalProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        GeometricCoordinateSpace::Enum coordinateSpace,
        ClipSpaceType clipSpaceType)
    {
        const float n = nearClipPlane;
        const float f = farClipPlane;

        Float4x4 result = Identity<Float4x4>();
        result(0,0) =  2.f / (r-l);
        result(0,3) =  -(r+l) / (r-l);

        result(1,1) =  2.f / (t-b);
        result(1,3) =  -(t+b) / (t-b);

        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
                //  This is the D3D view of clip space
                //      0<z/w<1
            result(2,2) =  -1.f / (f-n);            // (note z direction flip here)
            result(2,3) =    -n / (f-n);
        } else {
            assert(0);
        }

        if (clipSpaceType == ClipSpaceType::PositiveRightHanded) {
            result(1,1) = -result(1,1);
            result(1,3) = -result(1,3);
        }

        return result;
    }

    Float4x4 OrthogonalProjection(
        float l, float t, float r, float b,
        float nearClipPlane, float farClipPlane,
        ClipSpaceType clipSpaceType)
    {
        return OrthogonalProjection(
            l, t, r, b, nearClipPlane, farClipPlane,
            GeometricCoordinateSpace::RightHanded, clipSpaceType);
    }

    std::pair<float, float> CalculateNearAndFarPlane(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
            // Given a "minimal projection", figure out the near and far plane
            // that was used to create this projection matrix (assuming it was a 
            // perspective projection created with the function 
            // "PerspectiveProjection"
            //
            // Note that the "minimal projection" can be got from a projection
            // matrix using the "ExtractMinimalProjection" function.
            //
            // We just need to do some algebra to reverse the calculations we
            // used to build the perspective transform matrix.
            //
            // For ClipSpaceType::Positive:
            //      miniProj[2] = A = -f / (f-n)
            //      miniProj[3] = B = -(f*n) / (f-n)
            //      C = B / A = n
            //      A * (f-n) = -f
            //      Af - An = -f
            //      Af + f = An
            //      (A + 1) * f = An
            //      f = An / (A+1)
            //        = B / (A+1)
            //
            // For ClipSpaceType::StraddlingZero
            //      miniProj[2] = A = -(f+n) / (f-n)
            //      miniProj[3] = B = -(2fn) / (f-n)
            //      n = B / (A - 1)
            //      f = B / (A + 1)

        const float A = minimalProjection[2];
        const float B = minimalProjection[3];
        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
            return std::make_pair(B / A, B / (A + 1.f));
        } else {
            return std::make_pair(B / (A - 1.f), B / (A + 1.f));
        }
    }

    std::pair<float, float> CalculateFov(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
        // calculate the vertical field of view and aspect ration from the given
        // standard projection matrix;
        float n, f;
        std::tie(n, f) = CalculateNearAndFarPlane(minimalProjection, clipSpaceType);

        // M(1,1) =  (2.f * n) / (t-b);
        float tmb = (2.f * n) / minimalProjection[1];
        // tmb = 2 * h
        // h = n * XlTan(.5f * verticalFOV);
        // XlTan(.5f * verticalFOV) = h/n
        // verticalFOV = 2.f * XlATan(h/n);
        float verticalFOV = 2.f * XlATan2(tmb/2.f, n);
        float aspect = minimalProjection[1] / minimalProjection[0];
        return std::make_pair(verticalFOV, aspect);
    }

    std::pair<float, float> CalculateNearAndFarPlane_Ortho(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
            // For ClipSpaceType::Positive:
            //      miniProj[2] = A = -1 / (f-n)
            //      miniProj[3] = B = -n / (f-n)
            //      C = B / A = n

        const float A = minimalProjection[2];
        const float B = minimalProjection[3];
        if (clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::PositiveRightHanded) {
            return std::make_pair(B / A, (B - 1.f) / A);
        } else {
            assert(0);
            return std::make_pair(0.f, 0.f);
        }
    }

    Float2 CalculateDepthProjRatio_Ortho(
        const Float4& minimalProjection, ClipSpaceType clipSpaceType)
    {
        auto nearAndFar = CalculateNearAndFarPlane_Ortho(minimalProjection, clipSpaceType);
        return Float2(    1.f / (nearAndFar.second - nearAndFar.first),
            -nearAndFar.first / (nearAndFar.second - nearAndFar.first));
    }

    std::pair<Float3, Float3> BuildRayUnderCursor(
        Int2 mousePosition, 
        Float3 absFrustumCorners[], 
        const Float3& cameraPosition,
        float nearClip, float farClip,
        const std::pair<Float2, Float2>& viewport)
    {
        float u = (float(mousePosition[0]) - viewport.first[0]) / (viewport.second[0] - viewport.first[0]);
        float v = (float(mousePosition[1]) - viewport.first[1]) / (viewport.second[1] - viewport.first[1]);
        float w0 = (1.0f - u) * (1.0f - v);
        float w1 = (1.0f - u) * v;
        float w2 = u * (1.0f - v);
        float w3 = u * v;

        Float3 direction = 
              w0 * (absFrustumCorners[0] - cameraPosition)
            + w1 * (absFrustumCorners[1] - cameraPosition)
            + w2 * (absFrustumCorners[2] - cameraPosition)
            + w3 * (absFrustumCorners[3] - cameraPosition)
            ;
        direction = Normalize(direction);

            // Getting some problems with the intersection ray finishing slightly before
            // the far clip plane... Let's push the ray length slightly beyond, just to catch this.
        const float farBias = 1.1f;
        return std::make_pair(
            cameraPosition + nearClip * direction,
            cameraPosition + (farBias * farClip) * direction);
    }

    std::pair<Float2, Float2> GetPlanarMinMax(const Float4x4& worldToClip, const Float4& plane, ClipSpaceType clipSpaceType)
    {
        Float3 cameraAbsFrustumCorners[8];
        CalculateAbsFrustumCorners(cameraAbsFrustumCorners, worldToClip, clipSpaceType);

        const std::pair<unsigned, unsigned> edges[] =
        {
            std::make_pair(0, 1), std::make_pair(1, 3), std::make_pair(3, 2), std::make_pair(2, 0),
            std::make_pair(4, 5), std::make_pair(5, 7), std::make_pair(7, 6), std::make_pair(6, 4),
            std::make_pair(0, 4), std::make_pair(1, 5), std::make_pair(2, 6), std::make_pair(3, 7)
        };

        Float2 minIntersection(FLT_MAX, FLT_MAX), maxIntersection(-FLT_MAX, -FLT_MAX);
        float intersectionPts[dimof(edges)];
        for (unsigned c=0; c<dimof(edges); ++c) {
            intersectionPts[c] = RayVsPlane(cameraAbsFrustumCorners[edges[c].first], cameraAbsFrustumCorners[edges[c].second], plane);
            if (intersectionPts[c] >= 0.f && intersectionPts[c] <= 1.f) {
                auto intr = LinearInterpolate(cameraAbsFrustumCorners[edges[c].first], cameraAbsFrustumCorners[edges[c].second], intersectionPts[c]);
                minIntersection[0] = std::min(minIntersection[0], intr[0]);
                minIntersection[1] = std::min(minIntersection[1], intr[2]);
                maxIntersection[0] = std::max(maxIntersection[0], intr[0]);
                maxIntersection[1] = std::max(maxIntersection[1], intr[2]);
            }
        }

        return std::make_pair(minIntersection, maxIntersection);
    }
    
    static bool IntersectsWhenProjects(const IteratorRange<const Float3*> obj1, const IteratorRange<const Float3*> obj2, const Float3 &axis) {
        if (MagnitudeSquared(axis) < 0.00001f) {
            return true;
        }
        
        float min1 = FLT_MAX;
        float max1 = -FLT_MAX;
        for (const Float3 &p : obj1) {
            float dist = Dot(p, axis);
            min1 = std::min(min1, dist);
            max1 = std::max(max1, dist);
        }
        
        float min2 = FLT_MAX;
        float max2 = -FLT_MAX;
        for (const Float3 &p : obj2) {
            float dist = Dot(p, axis);
            min2 = std::min(min2, dist);
            max2 = std::max(max2, dist);
        }
        
        return (min1 < max2 && min1 > min2) ||
        (max1 < max2 && max1 > min2) ||
        (min2 < max1 && min2 > min1) ||
        (max2 < max1 && max2 > min1);
    }
    
    // This check is based on the Separating Axis Theorem
    static bool SeparatingAxisTheoremCheck(const std::pair<IteratorRange<const unsigned *>, IteratorRange<const Float3*>> &geometry,
                                           const Float4x4 &projectionMatrix,
                                           ClipSpaceType clipSpaceType) {
        // Convert frustum to world space
        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, projectionMatrix, clipSpaceType);
        
        Int3 faceTriangles[6] = {
            {0, 1, 2},
            {4, 6, 5},
            {0, 4, 1},
            {2, 3, 6},
            {1, 5, 3},
            {0, 2, 4}
        };
        
        Int2 frustumEdgeIndexes[12] = {
            {0, 1},
            {0, 2},
            {0, 4},
            {1, 3},
            {1, 5},
            {2, 3},
            {2, 6},
            {3, 7},
            {4, 5},
            {4, 6},
            {5, 7},
            {6, 7}
        };
        
        Float3 frustumNormals[6];
        for (unsigned planeIdx = 0; planeIdx < 6; ++planeIdx) {
            Float3 faceTriangle[3] = {frustumCorners[faceTriangles[planeIdx][0]], frustumCorners[faceTriangles[planeIdx][1]], frustumCorners[faceTriangles[planeIdx][2]]};
            frustumNormals[planeIdx] = Normalize(Cross(faceTriangle[1] - faceTriangle[0], faceTriangle[2] - faceTriangle[0]));
        }
        
        Float3 frustumEdges[12];
        for (unsigned edgeIdx = 0; edgeIdx < 12; ++edgeIdx) {
            frustumEdges[edgeIdx] = frustumCorners[frustumEdgeIndexes[edgeIdx][1]] - frustumCorners[frustumEdgeIndexes[edgeIdx][0]];
        }
        
        auto &indexes = geometry.first;
        auto &vertexes = geometry.second;
        for (unsigned triangleIdx = 0; triangleIdx < indexes.size(); triangleIdx += 3) {
            bool intersects = true;
            Float3 triangle[3];
            for (unsigned vertexIdx = 0; vertexIdx < 3; ++vertexIdx) {
                triangle[vertexIdx] = vertexes[indexes[triangleIdx + vertexIdx]];
            }
            
            if (MagnitudeSquared(triangle[0] - triangle[1]) < 0.00001f ||
                MagnitudeSquared(triangle[0] - triangle[2]) < 0.00001f ||
                MagnitudeSquared(triangle[1] - triangle[2]) < 0.00001f) {
                continue;
            }
            
            for (Float3 axis : frustumNormals) {
                if (!IntersectsWhenProjects(MakeIteratorRange(frustumCorners), MakeIteratorRange(triangle), axis)) {
                    intersects = false;
                    break;
                }
            }
            
            if (!intersects) {
                continue;
            }
            
            Float3 triangleNormal = Normalize(Cross(triangle[1] - triangle[0], triangle[2] - triangle[0]));
            if (!IntersectsWhenProjects(MakeIteratorRange(frustumCorners), MakeIteratorRange(triangle), triangleNormal)) {
                intersects = false;
                continue;
            }
            
            
            for (unsigned triEdgeIdx = 0; triEdgeIdx < 3; ++triEdgeIdx) {
                unsigned endIdx = (triEdgeIdx + 1) % 3;
                Float3 edge = triangle[endIdx] - triangle[triEdgeIdx];
                for (unsigned frustumEdgeIdx = 0; frustumEdgeIdx < 12; ++frustumEdgeIdx) {
                    Float3 axis = Normalize(Cross(frustumEdges[frustumEdgeIdx], edge));
                    if (!IntersectsWhenProjects(MakeIteratorRange(frustumCorners), MakeIteratorRange(triangle), axis)) {
                        intersects = false;
                        break;
                    }
                }
                if (!intersects) {
                    break;
                }
            }
            
            if (!intersects) {
                continue;
            }
            
            return true;
        }
        return false;
    }
    
    bool TestTriangleList(const std::pair<IteratorRange<const unsigned *>, IteratorRange<const Float3*>> &geometry,
                          const Float4x4 &projectionMatrix,
                          ClipSpaceType clipSpaceType) {
        assert(clipSpaceType == ClipSpaceType::Positive || clipSpaceType == ClipSpaceType::StraddlingZero);
        bool allAbove = true;
        bool allBelow = true;
        bool allLeft = true;
        bool allRight = true;
        bool allNear = true;
        bool allFar = true;
        
        unsigned minIdx = std::numeric_limits<unsigned>::max();
        unsigned maxIdx = 0;
        
        for (unsigned idx : geometry.first) {
            minIdx = idx < minIdx ? idx : minIdx;
            maxIdx = idx > maxIdx ? idx : maxIdx;
        }
        
        for (unsigned idx = minIdx; idx <= maxIdx; ++idx) {
            const Float3 &vertex = geometry.second[idx];
            Float4 projected = XYZProj(projectionMatrix, vertex);
            bool left = projected[0] < -projected[3];
            bool right = projected[0] > projected[3];
            bool below = projected[1] < -projected[3];
            bool above = projected[1] > projected[3];
            bool far = projected[2] > projected[3];
            
            bool near = true;
            if (clipSpaceType == ClipSpaceType::Positive) {
                near = projected[2] < 0.f;
            } else if (clipSpaceType == ClipSpaceType::StraddlingZero) {
                near = projected[2] < -projected[3];
            }
            
            if (!left && !right &&
                !above && !below &&
                !near && !far) {
                return true;
            }
            
            allAbove &= above;
            allBelow &= below;
            allLeft &= left;
            allRight &= right;
            allNear &= near;
            allFar &= far;
        }
        
        if (allAbove || allBelow ||
            allLeft || allRight ||
            allNear || allFar) {
            return false;
        }
        
        return SeparatingAxisTheoremCheck(geometry, projectionMatrix, clipSpaceType);
    }
}
