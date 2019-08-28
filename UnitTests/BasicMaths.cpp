// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Math/Transformations.h"
#include "../Math/ProjectionMath.h"
#include "../Math/Geometry.h"
#include <random>

#if !defined(XC_TEST_ADAPTER)
    #include <CppUnitTest.h>
    using namespace Microsoft::VisualStudio::CppUnitTestFramework;
#endif

namespace UnitTests
{
    static Float3 RandomUnitVector(std::mt19937& rng)
    {
        return SphericalToCartesian(Float3(
            Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng)),
            Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng)),
            1.f));
    }

    static float RandomSign(std::mt19937& rng) { return ((std::uniform_real_distribution<>(-1.f, 1.f)(rng) < 0.f) ? -1.f : 1.f); }
    static Float3 RandomScaleVector(std::mt19937& rng)
    {
        return Float3(
            RandomSign(rng) * (float)std::uniform_real_distribution<>(0.1f, 10.f)(rng),
            RandomSign(rng) * (float)std::uniform_real_distribution<>(0.1f, 10.f)(rng),
            RandomSign(rng) * (float)std::uniform_real_distribution<>(0.1f, 10.f)(rng));
    }

    static Float3 RandomTranslationVector(std::mt19937& rng)
    {
        return Float3(
            (float)std::uniform_real_distribution<>(-10000.f, 10000.f)(rng),
            (float)std::uniform_real_distribution<>(-10000.f, 10000.f)(rng),
            (float)std::uniform_real_distribution<>(-10000.f, 10000.f)(rng));
    }

	TEST_CLASS(BasicMaths)
	{
	public:
		
		TEST_METHOD(TestMethod1)
		{
			// Test some fundamental 3d geometry maths stuff
			// Using Combine_InPlace, produce 2 complex rotation
			// matrixes. Each should be the inverse of the other.

			const float tolerance = 1.e-5f; 
			
			{
				Float4x4 rotA = Identity<Float4x4>();
				Combine_InPlace(RotationX(.85f * gPI), rotA);
				Combine_InPlace(RotationY(-.35f * gPI), rotA);
				Combine_InPlace(RotationZ(.5f  * gPI), rotA);

				Float4x4 rotB = Identity<Float4x4>();
				Combine_InPlace(RotationZ(-.5f * gPI), rotB);
				Combine_InPlace(RotationY(.35f * gPI), rotB);
				Combine_InPlace(RotationX(-.85f * gPI), rotB);

				auto shouldBeIdentity = Combine(rotA, rotB);
				Assert::IsTrue(Equivalent(Identity<Float4x4>(), shouldBeIdentity, tolerance));
				
				auto invRotA = Inverse(rotA);
				auto invRotA2 = InvertOrthonormalTransform(rotA);
				Assert::IsTrue(Equivalent(rotB, invRotA, tolerance));
				Assert::IsTrue(Equivalent(rotB, invRotA2, tolerance));
				
				Float3 starterVector(1.f, 2.f, 3.f);
				auto trans1 = TransformDirectionVector(rotA, starterVector);
				auto trans2 = TransformDirectionVector(rotB, trans1);
				Assert::IsTrue(Equivalent(trans2, starterVector, tolerance));

                auto trans1a = TransformPoint(rotA, starterVector);
				auto trans2a = TransformPointByOrthonormalInverse(rotA, trans1a);
                auto trans3a = TransformPoint(InvertOrthonormalTransform(rotA), trans1a);
				Assert::IsTrue(Equivalent(trans2a, starterVector, tolerance));
                Assert::IsTrue(Equivalent(trans3a, starterVector, tolerance));
			}

				// test different types of rotation construction
			{
				auto quat = MakeRotationQuaternion(Normalize(Float3(1.f, 2.f, 3.f)), .6f * gPI);
				auto rotMat = MakeRotationMatrix(Normalize(Float3(1.f, 2.f, 3.f)), .6f * gPI);

				Assert::IsTrue(Equivalent(AsFloat4x4(quat), AsFloat4x4(rotMat), tolerance));
			}
		}

        TEST_METHOD(MatrixAccumulationAndDecomposition)
        {
                // Compare 2 method of building scale/rotation/translation matrices
                // Also check the decomposition is accurate
            std::mt19937 rng(std::random_device().operator()());
            const unsigned tests = 500;
            const float tolerance = 1e-4f;
            for (unsigned c2=0; c2<tests; ++c2) {
                auto rotationAxis = RandomUnitVector(rng);
                auto rotationAngle = Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng));
                auto scale = RandomScaleVector(rng);
                auto translation = RandomTranslationVector(rng);

                ScaleRotationTranslationQ srt(
                    scale, 
				    MakeRotationQuaternion(rotationAxis, rotationAngle),
				    translation);

			    Float4x4 accumulativeMatrix = Identity<Float4x4>();
			    Combine_InPlace(accumulativeMatrix, ArbitraryScale(scale));
                auto rotMat = MakeRotationMatrix(rotationAxis, rotationAngle);
			    accumulativeMatrix = Combine(accumulativeMatrix, AsFloat4x4(rotMat));
			    Combine_InPlace(accumulativeMatrix, translation);
				
			    auto srtMatrix = AsFloat4x4(srt);
			    Assert::IsTrue(Equivalent(srtMatrix, accumulativeMatrix, tolerance), L"Acculumated matrix does not match ScaleRotationTranslationQ version");

                    // note that sometimes the decomposition will be different from the 
                    // original scale/rotation values... But the final result will be the same.
                    // We can compenstate for this by pushing sign differences in the scale
                    // values into the rotation matrix
                ScaleRotationTranslationM decomposed(accumulativeMatrix);
                auto signCompScale = decomposed._scale;
                auto signCompRot = decomposed._rotation;
                for (unsigned c=0; c<3; ++c)
                    if (signCompScale[c]<0.f != scale[c]<0.f) {
                        signCompScale[c] *= -1.f;
                        signCompRot(0, c) *= -1.f; signCompRot(1, c) *= -1.f; signCompRot(2, c) *= -1.f;
                    }

                Assert::IsTrue(Equivalent(signCompScale, scale, tolerance), L"Scale in decomposed matrix doesn't match");
                Assert::IsTrue(Equivalent(decomposed._translation, translation, tolerance), L"Translation in decomposed matrix doesn't match");
                Assert::IsTrue(Equivalent(signCompRot, rotMat, tolerance), L"Rotation in decomposed matrix doesn't match");

                auto rebuilt = AsFloat4x4(decomposed);
                Assert::IsTrue(Equivalent(srtMatrix, rebuilt, tolerance), L"Rebuilt matrix doesn't match ScaleRotationTranslationQ matrix");
            }
        }

        TEST_METHOD(ProjectionMath)
        {
            std::mt19937 rng(std::random_device().operator()());
            const unsigned tests = 500;
            const float tolerance = 1e-4f;
            for (unsigned c=0; c<tests; ++c) {
                float fov = Deg2Rad((float)std::uniform_real_distribution<>(15.f, 80.f)(rng));
                float aspect = (float)std::uniform_real_distribution<>(.5f, 3.f)(rng);
                float near = (float)std::uniform_real_distribution<>(0.01f, 1.f)(rng);
                float far = (float)std::uniform_real_distribution<>(100.f, 1000.f)(rng);

                auto proj = PerspectiveProjection(
                    fov, aspect, near, far,
                    GeometricCoordinateSpace::RightHanded,
                    ClipSpaceType::Positive);

                float outNear, outFar;
                float outFOV, outAspect;
                std::tie(outNear, outFar) = CalculateNearAndFarPlane(
                    ExtractMinimalProjection(proj), ClipSpaceType::Positive);
                std::tie(outFOV, outAspect) = CalculateFov(
                    ExtractMinimalProjection(proj), ClipSpaceType::Positive);

                Assert::AreEqual(fov, outFOV, fov * tolerance, L"Extracted FOV");
                Assert::AreEqual(aspect, outAspect, aspect * tolerance, L"Extracted aspect");
                Assert::AreEqual(near, outNear, near * tolerance, L"Extracted near clip");
                    // Calculations for the far clip are much less accurate. We're
                    // essentially dividing by (-1 + 1/far + 1), so the larger far
                    // is, the more inaccurate it gets (and the variance in the
                    // inaccuracy grows even faster, making random measurements
                    // more fun). We then double the precision loss in our reverse
                    // math test. Running an independent test of the same math,
                    // for numbers in the range of [100, 1000], the chances of being
                    // off by more than 7 should be right around 1 in a million, so...
                    // within 7.5 should be a good enough test to run 500 times.
                Assert::AreEqual(far, outFar, 7.5f, L"Extracted far clip");
            }
        }

	};
}
