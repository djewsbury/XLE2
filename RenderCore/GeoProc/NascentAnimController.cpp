// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS   // warning C4996: 'std::_Copy_impl': Function call with parameters that may be unsafe

#include "NascentAnimController.h"
#include "NascentRawGeometry.h"
#include "GeometryAlgorithm.h"
#include "GeoProcUtil.h"
#include "../Assets/AssetUtils.h"
#include "../Format.h"
#include "../Types.h"
#include "../VertexUtil.h"
#include "../../Assets/BlockSerializer.h"
#include "../../ConsoleRig/Log.h"
#include "../../ConsoleRig/LogUtil.h"
#include "../../Assets/Assets.h"
#include "../../Math/Transformations.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StreamUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{
    static const bool SkinNormals = true;

	static const std::string DefaultSemantic_Weights         = "WEIGHTS";
    static const std::string DefaultSemantic_JointIndices    = "JOINTINDICES";

	class BuckettedSkinController
	{
	public:
		struct Bucket
        {
            std::vector<InputElementDesc>	_vertexInputLayout;
            unsigned						_weightCount = 0;

            std::unique_ptr<uint8_t[]>		_vertexBufferData;
            size_t							_vertexBufferSize = 0;
        };

		Bucket						_bucket[4];      // 4, 2, 1, 0
        std::vector<uint32_t>		_positionIndexToBucketIndex;
		std::vector<unsigned>		_jointIndexRemapping;

		void RemapJoints(IteratorRange<const unsigned*> newIndices);

		BuckettedSkinController(const UnboundSkinController& src);
	};

	template <int WeightCount>
        class VertexWeightAttachment
    {
    public:
        uint8_t       _weights[WeightCount];            // go straight to compressed 8 bit value
        uint8_t       _jointIndex[WeightCount];
    };

    template <>
        class VertexWeightAttachment<0>
    {
    };

    template <int WeightCount>
        class VertexWeightAttachmentBucket
    {
    public:
		unsigned _vertexCount = 0;
        std::vector<VertexWeightAttachment<WeightCount>>    _weightAttachments;
    };

    template<unsigned WeightCount> 
        VertexWeightAttachment<WeightCount> BuildWeightAttachment(const uint8_t weights[], const unsigned joints[], unsigned jointCount)
    {
        VertexWeightAttachment<WeightCount> attachment;
		unsigned c=0;
		for (; c<std::min(WeightCount, jointCount); ++c) {
			attachment._weights[c] = weights[c];
			attachment._jointIndex[c] = (uint8_t)joints[c];
		}
		for (; c<WeightCount; ++c) {
			attachment._weights[c] = 0;
			attachment._jointIndex[c] = 0;
		}
        return attachment;
    }

    template<> inline VertexWeightAttachment<0> BuildWeightAttachment(const uint8_t weights[], const unsigned joints[], unsigned jointCount)
    {
        return VertexWeightAttachment<0>();
    }

	template<unsigned WeightCount>
		void AccumulateJointUsage(
			const VertexWeightAttachmentBucket<WeightCount>& bucket,
			std::vector<unsigned>& accumulator);
	template<unsigned WeightCount>
		void RemapJointIndices(
			VertexWeightAttachmentBucket<WeightCount>& bucket,
			IteratorRange<const unsigned*> remapping);

	BuckettedSkinController::BuckettedSkinController(const UnboundSkinController& src)
	{
			//
            //      If we have a mesh where there are many vertices with only 1 or 2
            //      weights, but others with 4, we could potentially split the
            //      skinning operation up into multiple parts.
            //
            //      This could be done in 2 ways:
            //          1. split the geometry into multiple meshes, with extra draw calls
            //          2. do skinning in a preprocessing step before Draw
            //                  Ie; multiple skin -> to vertex buffer steps
            //                  then a single draw call...
            //              That would be efficient if writing to a GPU vertex buffer was
            //              very fast (it would also help reduce shader explosion).
            //
            //      If we were using type 2, it might be best to separate the animated parts
            //      of the vertex from the main vertex buffer. So texture coordinates and vertex
            //      colors will be static, so left in a separate buffer. But positions (and possibly
            //      normals and tangent frames) might be animated. So they could be held separately.
            //
            //      It might be handy to have a vertex buffer with just the positions. This could
            //      be used for pre-depth passes, etc.
            //
            //      Option 2 would be an efficient mode in multiple-pass rendering (because we
            //      apply the skinning only once, even if the object is rendered multiple times).
            //      But we need lots of temporary buffer space. Apparently in D3D11.1, we can 
            //      output to a buffer as a side-effect of vertex transformations, also. So a 
            //      first pass vertex shader could do depth-prepass and generate skinned
            //      positions for the second pass. (but there are some complications... might be
            //      worth experimenting with...)
            //
            //
            //      Let's create a number of buckets based on the number of weights attached
            //      to that vertex. Ideally we want a lot of vertices in the smaller buckets, 
            //      and few in the high buckets.
            //

        VertexWeightAttachmentBucket<4> bucket4;
        VertexWeightAttachmentBucket<2> bucket2;
        VertexWeightAttachmentBucket<1> bucket1;
        VertexWeightAttachmentBucket<0> bucket0;

        _positionIndexToBucketIndex.reserve(src._influenceCount.size());

		auto influenceI = src._influenceCount.begin();
        for (; influenceI!=src._influenceCount.end(); ++influenceI) {

            auto influenceCount = *influenceI;
			auto vertexIndex = std::distance(src._influenceCount.begin(), influenceI);

			if (influenceCount == ~0u) continue;

                //
                //      Sometimes the input data has joints attached at very low weight
                //      values. In these cases it's better to just ignore the influence.
                //
                //      So we need to calculate the normalized weights for all of the
                //      influences, first -- and then strip out the unimportant ones.
                //
            const unsigned AbsoluteMaxJointInfluenceCount = 256;
			float weights[AbsoluteMaxJointInfluenceCount];
            unsigned jointIndices[AbsoluteMaxJointInfluenceCount];

			for (unsigned c=0; c<influenceCount;) {
				auto subPartCount = std::min(4u, influenceCount-c);
				std::copy(src._attachmentGroups[c/4]._weights.begin() + vertexIndex*4, src._attachmentGroups[c/4]._weights.begin() + vertexIndex*4 + subPartCount, &weights[c]);
				std::copy(src._attachmentGroups[c/4]._jointIndices.begin() + vertexIndex*4, src._attachmentGroups[c/4]._jointIndices.begin() + vertexIndex*4 + subPartCount, &jointIndices[c]);
				c += subPartCount;
			}

            const float minWeightThreshold = 8.f / 255.f;
            float totalWeightValue = 0.f;
            for (size_t c=0; c<influenceCount;) {
                if (weights[c] < minWeightThreshold) {
                    std::move(&weights[c+1],        &weights[influenceCount],       &weights[c]);
                    std::move(&jointIndices[c+1],   &jointIndices[influenceCount],  &jointIndices[c]);
                    --influenceCount;
                } else {
                    totalWeightValue += weights[c];
                    ++c;
                }
            }

            uint8_t normalizedWeights[AbsoluteMaxJointInfluenceCount];
            for (size_t c=0; c<influenceCount; ++c) {
				assert(totalWeightValue!=0.0f);
                normalizedWeights[c] = (uint8_t)(Clamp(weights[c] / totalWeightValue, 0.f, 1.f) * 255.f + .5f);
            }

                //
                // \todo -- should we sort influcences by the strength of the influence, or by the joint
                //          index?
                //
                //          Sorting by weight could allow us to decrease the number of influences
                //          calculated smoothly.
                //
                //          Sorting by joint index might mean that adjacent vertices are more frequently
                //          calculating the same joint.
                //

            #if defined(_DEBUG)     // double check to make sure no joint is referenced twice!
                for (size_t c=1; c<influenceCount; ++c) {
                    assert(std::find(jointIndices, &jointIndices[c], jointIndices[c]) == &jointIndices[c]);
                }
            #endif

			if (_positionIndexToBucketIndex.size() <= (size_t)vertexIndex)
				_positionIndexToBucketIndex.resize(vertexIndex+1, ~0u);
			assert(_positionIndexToBucketIndex[vertexIndex] == ~0u);

            if (influenceCount >= 3) {
                if (influenceCount > 4) {
                    Log(Warning)
                        << "Warning -- Exceeded maximum number of joints affecting a single vertex in skinning controller"
                        << ". Only 4 joints can affect any given single vertex."
						<< std::endl;

                        // (When this happens, only use the first 4, and ignore the others)
					Log(Warning) << "After filtering:" << std::endl;
                    for (size_t c=0; c<influenceCount; ++c) {
						Log(Warning) << "  [" << c << "] Weight: " << normalizedWeights[c] << " Joint: " << jointIndices[c] << std::endl;
                    }
                }

                    // (we could do a separate bucket for 3, if it was useful)
                _positionIndexToBucketIndex[vertexIndex] = (0<<16) | (uint32_t(bucket4._vertexCount)&0xffff);
                ++bucket4._vertexCount;
                bucket4._weightAttachments.push_back(BuildWeightAttachment<4>(normalizedWeights, jointIndices, (unsigned)influenceCount));
            } else if (influenceCount == 2) {
                _positionIndexToBucketIndex[vertexIndex] = (1<<16) | (uint32_t(bucket2._vertexCount)&0xffff);
                ++bucket2._vertexCount;
                bucket2._weightAttachments.push_back(BuildWeightAttachment<2>(normalizedWeights, jointIndices, (unsigned)influenceCount));
            } else if (influenceCount == 1) {
                _positionIndexToBucketIndex[vertexIndex] = (2<<16) | (uint32_t(bucket1._vertexCount)&0xffff);
                ++bucket1._vertexCount;
                bucket1._weightAttachments.push_back(BuildWeightAttachment<1>(normalizedWeights, jointIndices, (unsigned)influenceCount));
            } else {
                _positionIndexToBucketIndex[vertexIndex] = (3<<16) | (uint32_t(bucket0._vertexCount)&0xffff);
                ++bucket0._vertexCount;
                bucket0._weightAttachments.push_back(BuildWeightAttachment<0>(normalizedWeights, jointIndices, (unsigned)influenceCount));
            }
        }

		// Compress the list of joints used by this controller by only
		// including joints that are actually referenced by weights
		const bool doJointUsageCompression = true;
		if (doJointUsageCompression) {
			std::vector<unsigned> jointUsage;
			jointUsage.resize(256);
			AccumulateJointUsage(bucket1, jointUsage);
			AccumulateJointUsage(bucket2, jointUsage);
			AccumulateJointUsage(bucket4, jointUsage);

			unsigned finalJointIndexCount = 0;
			for (unsigned c=0; c<jointUsage.size(); ++c) {
				if (jointUsage[c]) {
					if (_jointIndexRemapping.size() <= c)
						_jointIndexRemapping.resize(c+1, ~0u);
					_jointIndexRemapping[c] = finalJointIndexCount;
					++finalJointIndexCount;
				}
			}
			RemapJointIndices(bucket1, MakeIteratorRange(_jointIndexRemapping));
			RemapJointIndices(bucket2, MakeIteratorRange(_jointIndexRemapping));
			RemapJointIndices(bucket4, MakeIteratorRange(_jointIndexRemapping));
		}
            
        Bucket b4;
        b4._weightCount = 4;
        b4._vertexBufferSize = bucket4._weightAttachments.size() * sizeof(VertexWeightAttachment<4>);
        b4._vertexBufferData = std::make_unique<uint8_t[]>(b4._vertexBufferSize);
        XlCopyMemory(b4._vertexBufferData.get(), AsPointer(bucket4._weightAttachments.begin()), b4._vertexBufferSize);
        b4._vertexInputLayout.push_back(InputElementDesc(DefaultSemantic_Weights, 0, Format::R8G8B8A8_UNORM, 1, 0));
        b4._vertexInputLayout.push_back(InputElementDesc(DefaultSemantic_JointIndices, 0, Format::R8G8B8A8_UINT, 1, 4));

        Bucket b2;
        b2._weightCount = 2;
        b2._vertexBufferSize = bucket2._weightAttachments.size() * sizeof(VertexWeightAttachment<2>);
        b2._vertexBufferData = std::make_unique<uint8_t[]>(b2._vertexBufferSize);
        XlCopyMemory(b2._vertexBufferData.get(), AsPointer(bucket2._weightAttachments.begin()), b2._vertexBufferSize);
        b2._vertexInputLayout.push_back(InputElementDesc(DefaultSemantic_Weights, 0, Format::R8G8_UNORM, 1, 0));
        b2._vertexInputLayout.push_back(InputElementDesc(DefaultSemantic_JointIndices, 0, Format::R8G8_UINT, 1, 2));

        Bucket b1;
        b1._weightCount = 1;
        b1._vertexBufferSize = bucket1._weightAttachments.size() * sizeof(VertexWeightAttachment<1>);
        b1._vertexBufferData = std::make_unique<uint8_t[]>(b1._vertexBufferSize);
        XlCopyMemory(b1._vertexBufferData.get(), AsPointer(bucket1._weightAttachments.begin()), b1._vertexBufferSize);
        b1._vertexInputLayout.push_back(InputElementDesc(DefaultSemantic_Weights, 0, Format::R8_UNORM, 1, 0));
        b1._vertexInputLayout.push_back(InputElementDesc(DefaultSemantic_JointIndices, 0, Format::R8_UINT, 1, 1));

        Bucket b0;
        b0._weightCount = 0;
        b0._vertexBufferSize = bucket0._weightAttachments.size() * sizeof(VertexWeightAttachment<0>);
        if (b0._vertexBufferSize) {
            b0._vertexBufferData = std::make_unique<uint8_t[]>(b0._vertexBufferSize);
            XlCopyMemory(b0._vertexBufferData.get(), AsPointer(bucket0._weightAttachments.begin()), b0._vertexBufferSize);
        }

		_bucket[0] = std::move(b4);
		_bucket[1] = std::move(b2);
		_bucket[2] = std::move(b1);
		_bucket[3] = std::move(b0);
	}

	void BuckettedSkinController::RemapJoints(IteratorRange<const unsigned*> newJointIndices)
	{
		// Remap all of the joints through the given remapping indices
		// Useful for removing redundant duplicates (etc)
		for (unsigned b=0; b<4; ++b) {
			auto& bucket = _bucket[b];
			if (!bucket._vertexBufferSize) continue;
			auto ele = std::find_if(
				bucket._vertexInputLayout.begin(), bucket._vertexInputLayout.end(),
				[](const InputElementDesc& desc) { return desc._semanticName == "JOINTINDICES"; });
			auto range = MakeVertexIteratorRange(
				MakeIteratorRange(
					PtrAdd(bucket._vertexBufferData.get(), ele->_alignedByteOffset),
					PtrAdd(bucket._vertexBufferData.get(), bucket._vertexBufferSize)),
				CalculateVertexStrideForSlot(MakeIteratorRange(bucket._vertexInputLayout), ele->_inputSlot),
				ele->_nativeFormat);
			auto componentCount = GetComponentCount(RenderCore::GetComponents(ele->_nativeFormat));
			for (auto i=range.begin(); i<range.end(); ++i) {
				auto data = (uint8_t*)(*i)._data.begin();
				for (unsigned c=0; c<componentCount; ++c)
					data[c] = (uint8_t)newJointIndices[data[c]];
			}
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static uint32_t ControllerAndBucketIndex(unsigned controllerIdx, unsigned bucketIdx)
	{
		assert((bucketIdx & ~0x3ffff) == 0);
		return (controllerIdx << 18) | (bucketIdx & 0x3ffff);
	}

    NascentBoundSkinnedGeometry BindController(
        const NascentRawGeometry& sourceGeo,
        IteratorRange<const UnboundSkinControllerAndJointMatrices*> controllers,
        const char nodeName[])
    {
		assert(!controllers.empty());

        std::vector<std::string> elementsToBeSkinned;
        elementsToBeSkinned.push_back("POSITION");
        if (constant_expression<SkinNormals>::result()) {
            elementsToBeSkinned.push_back("NORMAL");
        }

            //
            //      Our instantiation of this geometry needs to be slightly different
            //      (but still similar) to the basic raw geometry case.
            //
            //      Basic geometry:
            //          vertex buffer
            //          index buffer
            //          input assembly setup
            //          draw calls
            //
            //      Skinned Geometry:
            //              (this part is mostly the same, except we've reordered the
            //              vertex buffers, and removed the part of the vertex buffer 
            //              that will be animated)
            //          unanimated vertex buffer
            //          index buffer
            //          input assembly setup (final draw calls)
            //          draw calls (final draw calls)
            //
            //              (this part is new)
            //          animated vertex buffer
            //          input assembly setup (skinning calculation pass)
            //          draw calls (skinning calculation pass)
            //
            //      Note that we need to massage the vertex buffers slightly. So the
            //      raw geometry input must be in a format that allows us to read from
            //      the vertex and index buffers.
            //
                            
        size_t unifiedVertexCount = sourceGeo._unifiedVertexCount;
		assert(sourceGeo._unifiedVertexIndexToPositionIndex.empty() || sourceGeo._unifiedVertexIndexToPositionIndex.size() >= unifiedVertexCount);

		std::vector<BuckettedSkinController> buckettedControllers;
		buckettedControllers.reserve(controllers.size());
		for (const auto&c:controllers)
			buckettedControllers.emplace_back(BuckettedSkinController{*c._controller});

		struct VertexIndices
		{
			uint32_t _inputUnifiedVertexIndex;
			uint32_t _bucketIndex;
			uint32_t _inputPositionIndex;
		};
        std::vector<VertexIndices> vertexMappingByFinalOrdering;
        vertexMappingByFinalOrdering.reserve(unifiedVertexCount);

        for (uint32_t c=0; c<unifiedVertexCount; ++c) {
            uint32_t positionIndex = c < sourceGeo._unifiedVertexIndexToPositionIndex.size() ? sourceGeo._unifiedVertexIndexToPositionIndex[c] : c;
			uint32_t bucketIndex = ~0u;

			unsigned controllerIdx=0;
			for (; controllerIdx!=controllers.size(); ++controllerIdx) {
				auto& buckettedController = buckettedControllers[controllerIdx];
				if (	positionIndex < buckettedController._positionIndexToBucketIndex.size()
					&&	buckettedController._positionIndexToBucketIndex[positionIndex] != ~0u) {
					bucketIndex = ControllerAndBucketIndex(controllerIdx, buckettedController._positionIndexToBucketIndex[positionIndex]);
					break;
				}
			}
			// If we did not get an assigment, our bucketIndex will still be ~0u
			assert(bucketIndex != ~0u);
			// A vertex can actually get associated with multiple controllers
			// In these cases, we always select the first controller that applies to the vertex
			// (basically under the assumption that all controllers will generate the same final position for the vertex)
			// In general, we don't want to split vertices for controllers -- because that could lead
			// to situations where the animation causes the mesh manifold to separate (which doesn't
			// seem like something that would be desireable)
			vertexMappingByFinalOrdering.push_back(VertexIndices{c, bucketIndex, positionIndex});
        }

            //
            //      Resort by bucket index...
            //

        std::stable_sort(
			vertexMappingByFinalOrdering.begin(), vertexMappingByFinalOrdering.end(), 
			[](const VertexIndices& lhs, const VertexIndices& rhs) { return lhs._bucketIndex < rhs._bucketIndex; });

            //
			//		We create a new reordering for the vertices based on the bucket assigment.
			//		This will reorganize the vertex buffer so that buckets are sequential in the VB
			//
            //      \todo --    it would better if we tried to maintain the vertex ordering within
            //                  the bucket. That is, the relative positions of vertices within the
            //                  bucket should be the same as the relative positions of those vertices
            //                  as they were in the original
            //

        const size_t bucketCount = dimof(BuckettedSkinController::_bucket) * controllers.size();
        std::vector<size_t> bucketStart(bucketCount, 0);
        std::vector<size_t> bucketEnd(bucketCount, 0);

		{
			uint32_t currentBucket = ~0u;
			for (size_t i=0; i<vertexMappingByFinalOrdering.size(); ++i) {
				auto thisBucket = (vertexMappingByFinalOrdering[i]._bucketIndex >> 16);
				if (thisBucket!=currentBucket) {
					if (currentBucket < bucketCount)
						bucketEnd[currentBucket] = i;
					assert(currentBucket == ~0u || thisBucket > currentBucket);
					currentBucket = thisBucket;
					bucketStart[currentBucket] = i;
				}
			}
			if (currentBucket < bucketCount)
				bucketEnd[currentBucket] = vertexMappingByFinalOrdering.size();
			for (unsigned b=currentBucket+1; b<bucketCount; ++b) {
				bucketStart[b] = bucketEnd[b] = vertexMappingByFinalOrdering.size();
			}
		}

            //
            //      Move vertex data for vertex elements that will be skinned into a separate vertex buffer
            //      Note that we don't really know which elements will be skinned. We can assume that at
            //      least "POSITION" will be skinned. But actually this is defined by the particular
            //      shader. We could wait until binding with the material to make this decision...?
            //
        auto unanimatedVertexLayout = sourceGeo._mainDrawInputAssembly._elements;
        decltype(unanimatedVertexLayout) animatedVertexLayout;

        for (auto i=unanimatedVertexLayout.begin(); i!=unanimatedVertexLayout.end();) {
            const bool mustBeSkinned = 
                std::find_if(   elementsToBeSkinned.begin(), elementsToBeSkinned.end(), 
                                [&](const std::string& s){ return !XlCompareStringI(i->_semanticName, s.c_str()); }) 
                        != elementsToBeSkinned.end();
            if (mustBeSkinned) {
                animatedVertexLayout.push_back(*i);
                i=unanimatedVertexLayout.erase(i);
            } else ++i;
        }

        {
            unsigned elementOffset = 0;     // reset the _alignedByteOffset members in the vertex layout
            for (auto i=unanimatedVertexLayout.begin(); i!=unanimatedVertexLayout.end();++i) {
                i->_alignedByteOffset = elementOffset;
                elementOffset += BitsPerPixel(i->_nativeFormat)/8;
            }
            elementOffset = 0;
            for (auto i=animatedVertexLayout.begin(); i!=animatedVertexLayout.end();++i) {
                i->_alignedByteOffset = elementOffset;
                elementOffset += BitsPerPixel(i->_nativeFormat)/8;
            }
        }

        unsigned unanimatedVertexStride  = CalculateVertexSize(MakeIteratorRange(unanimatedVertexLayout));
        unsigned animatedVertexStride    = CalculateVertexSize(MakeIteratorRange(animatedVertexLayout));

        if (!animatedVertexStride) {
            Throw(::Exceptions::BasicLabel("Could not find any animated vertex elements in skinning controller in node (%s). There must be a problem with vertex input semantics.", nodeName));
        }
                            
            //      Copy out those parts of the vertex buffer that are unanimated and animated
            //      (we also do the vertex reordering here)
		std::vector<uint8_t> unanimatedVertexBuffer(unanimatedVertexStride*unifiedVertexCount);
		std::vector<uint8_t> animatedVertexBuffer(animatedVertexStride*unifiedVertexCount);
		std::vector<uint8_t> newIndexBuffer(sourceGeo._indices.size());
		{
			std::vector<uint32_t> vertexOrdering;       // unifiedVertexReordering[oldIndex] = newIndex;
			vertexOrdering.resize(unifiedVertexCount, (uint32_t)~uint32_t(0x0));
			for (auto i=vertexMappingByFinalOrdering.cbegin(); i!=vertexMappingByFinalOrdering.cend(); ++i)
				vertexOrdering[i->_inputUnifiedVertexIndex] = (unsigned)std::distance(vertexMappingByFinalOrdering.cbegin(), i);
			
			CopyVertexElements(
				MakeIteratorRange(unanimatedVertexBuffer),	unanimatedVertexStride, 
				MakeIteratorRange(sourceGeo._vertices),		sourceGeo._mainDrawInputAssembly._vertexStride,
				MakeIteratorRange(unanimatedVertexLayout),
				MakeIteratorRange(sourceGeo._mainDrawInputAssembly._elements),
				MakeIteratorRange(vertexOrdering));

			CopyVertexElements(
				MakeIteratorRange(animatedVertexBuffer),	animatedVertexStride,
				MakeIteratorRange(sourceGeo._vertices),		sourceGeo._mainDrawInputAssembly._vertexStride,
				MakeIteratorRange(animatedVertexLayout),
				MakeIteratorRange(sourceGeo._mainDrawInputAssembly._elements),
				MakeIteratorRange(vertexOrdering));

				//      We have to remap the index buffer, also.
			if (sourceGeo._indexFormat == Format::R32_UINT) {
				std::transform(
					(const uint32_t*)AsPointer(sourceGeo._indices.begin()), (const uint32_t*)AsPointer(sourceGeo._indices.end()),
					(uint32_t*)newIndexBuffer.data(),
					[&vertexOrdering](uint32_t inputIndex) { return vertexOrdering[inputIndex]; });
			} else if (sourceGeo._indexFormat == Format::R16_UINT) {
				std::transform(
					(const uint16_t*)AsPointer(sourceGeo._indices.begin()), (const uint16_t*)AsPointer(sourceGeo._indices.end()),
					(uint16_t*)newIndexBuffer.data(),
					[&vertexOrdering](uint16_t inputIndex) -> uint16_t { auto result = vertexOrdering[inputIndex]; assert(result <= 0xffff); return (uint16_t)result; });
			} else if (sourceGeo._indexFormat == Format::R8_UINT) {
				std::transform(
					(const uint8_t*)AsPointer(sourceGeo._indices.begin()), (const uint8_t*)AsPointer(sourceGeo._indices.end()),
					(uint8_t*)newIndexBuffer.data(),
					[&vertexOrdering](uint8_t inputIndex) -> uint8_t { auto result = vertexOrdering[inputIndex]; assert(result <= 0xff); return (uint8_t)result; });
			} else {
				Throw(::Exceptions::BasicLabel("Unrecognised index format when instantiating skin controller in node (%s).", nodeName));
			}
		}

            //      Build the final vertex weights buffer (our weights are currently stored
            //      per vertex-position. So we need to expand to per-unified vertex -- blaggh!)
            //      This means the output weights vertex buffer is going to be larger than input ones combined.

        size_t destinationWeightVertexStride = 0;
        const std::vector<InputElementDesc>* finalWeightBufferFormat = nullptr;

        std::vector<unsigned> bucketVertexSizes(bucketCount);
        for (unsigned b=0; b<bucketCount; ++b) {
			auto& bucket = buckettedControllers[b>>2]._bucket[b&0x3];
            bucketVertexSizes[b] = CalculateVertexSize(MakeIteratorRange(bucket._vertexInputLayout));
            if (bucket._vertexBufferSize) {
                if (bucketVertexSizes[b] > destinationWeightVertexStride) {
                    destinationWeightVertexStride = bucketVertexSizes[b];
                    finalWeightBufferFormat = &bucket._vertexInputLayout;
                }
            }
        }

        if (destinationWeightVertexStride) {
            unsigned alignedDestinationWeightVertexStride = (unsigned)std::max(destinationWeightVertexStride, size_t(4));
            if (alignedDestinationWeightVertexStride != destinationWeightVertexStride) {
                LogLine(Warning, "vertex buffer had to be expanded for vertex alignment restrictions in node {}. This will leave some wasted space in the vertex buffer. This can be caused when using skinning when only 1 weight is really required.\n", nodeName);
                destinationWeightVertexStride = alignedDestinationWeightVertexStride;
            }
        }

		#if defined(_DEBUG)
			unsigned weightsOffset = 0;
            auto weightsFormat = Format::Unknown;
            for (auto i=finalWeightBufferFormat->cbegin(); i!=finalWeightBufferFormat->cend(); ++i) {
                if (!XlCompareStringI(i->_semanticName.c_str(), "WEIGHTS") && i->_semanticIndex == 0) {
                    weightsOffset = i->_alignedByteOffset;
                    weightsFormat = i->_nativeFormat;
                    break;
                }
            }
			unsigned indicesOffset = 0;
            auto indicesFormat = Format::Unknown;
            for (auto i=finalWeightBufferFormat->cbegin(); i!=finalWeightBufferFormat->cend(); ++i) {
                if (!XlCompareStringI(i->_semanticName.c_str(), "JOINTINDICES") && i->_semanticIndex == 0) {
                    indicesOffset = i->_alignedByteOffset;
                    indicesFormat = i->_nativeFormat;
                    break;
                }
            }
		#endif

        std::vector<uint8_t> skeletonBindingVertices;
        if (destinationWeightVertexStride && finalWeightBufferFormat) {
            skeletonBindingVertices = std::vector<uint8_t>(destinationWeightVertexStride*unifiedVertexCount, 0);

            for (size_t destinationVertexIndex=0; destinationVertexIndex<vertexMappingByFinalOrdering.size(); ++destinationVertexIndex) {
                unsigned sourceBucketIndex = vertexMappingByFinalOrdering[destinationVertexIndex]._bucketIndex;

				auto controllerIdx = sourceBucketIndex >> 18;
				auto bucketIdx = (sourceBucketIndex >> 16) & 0x3;
				auto sourceVertexInThisBucket = sourceBucketIndex & 0xffff;

				assert(controllerIdx < controllers.size());
				assert(bucketIdx < 4);
                                
				const auto& bucket = buckettedControllers[controllerIdx]._bucket[bucketIdx];

                    //
                    //      Note that sometimes we'll be expanding the vertex format in this process
                    //      If some buckets are using R8G8, and others are R8G8B8A8 (for example)
                    //      then they will all be expanded to the largest size
                    //

                auto sourceVertexStride = bucketVertexSizes[sourceBucketIndex>>16];
                void* destinationVertex = PtrAdd(skeletonBindingVertices.data(), destinationVertexIndex*destinationWeightVertexStride);
                assert((sourceVertexInThisBucket+1)*sourceVertexStride <= bucket._vertexBufferSize);
                const void* sourceVertex = PtrAdd(bucket._vertexBufferData.get(), sourceVertexInThisBucket*sourceVertexStride);

                if (sourceVertexStride == destinationWeightVertexStride) {
                    XlCopyMemory(destinationVertex, sourceVertex, sourceVertexStride);
                } else {
                    const InputElementDesc* dstElement = AsPointer(finalWeightBufferFormat->cbegin());
                    for (   auto srcElement=bucket._vertexInputLayout.cbegin(); 
                            srcElement!=bucket._vertexInputLayout.cend(); ++srcElement, ++dstElement) {

                            // (todo -- precalculate this min of element sizes)
						unsigned elementSize = std::min(BitsPerPixel(srcElement->_nativeFormat)/8, BitsPerPixel(dstElement->_nativeFormat)/8);
                        assert(PtrAdd(destinationVertex, dstElement->_alignedByteOffset+elementSize) <= PtrAdd(skeletonBindingVertices.data(), destinationWeightVertexStride*unifiedVertexCount));
                        assert(PtrAdd(sourceVertex, srcElement->_alignedByteOffset+elementSize) <= PtrAdd(bucket._vertexBufferData.get(), bucket._vertexBufferSize));
                        XlCopyMemory(   PtrAdd(destinationVertex, dstElement->_alignedByteOffset), 
                                        PtrAdd(sourceVertex, srcElement->_alignedByteOffset), 
                                        elementSize);
                    }
                }

				assert(destinationVertexIndex >= bucketStart[sourceBucketIndex>>16] && destinationVertexIndex < bucketEnd[sourceBucketIndex>>16]);

				#if defined(_DEBUG)
					for (unsigned c=0; c<GetComponentCount(GetComponents(indicesFormat)); ++c) {
						auto index = *(unsigned char*)PtrAdd(destinationVertex, indicesOffset+c);
						assert(index < (unsigned)controllers[controllerIdx]._jointMatrices.size());
					}
				#endif
            }
        }

        #if defined(_DEBUG)

            //  Double check that weights are normalized in the binding buffer
			/*{
                size_t stride = destinationWeightVertexStride;
                if (weightsFormat == Format::R8G8_UNORM) {
                    for (unsigned c=0; c<unifiedVertexCount; ++c) {
                        const void* p = PtrAdd(skeletonBindingVertices.get(), c*stride+weightsOffset);
                        unsigned char zero   = ((unsigned char*)p)[0];
                        unsigned char one    = ((unsigned char*)p)[1];
                        assert((zero+one) >= 0xfd);
                    }
                } else if (weightsFormat == Format::R8G8B8A8_UNORM) {
                    for (unsigned c=0; c<unifiedVertexCount; ++c) {
                        const void* p = PtrAdd(skeletonBindingVertices.get(), c*stride+weightsOffset);
                        unsigned char zero   = ((unsigned char*)p)[0];
                        unsigned char one    = ((unsigned char*)p)[1];
                        unsigned char two    = ((unsigned char*)p)[2];
                        unsigned char three  = ((unsigned char*)p)[3];
                        assert((zero+one+two+three) >= 0xfd);
                    }
                } else {
                    assert(weightsFormat == Format::R8_UNORM);
                }
            }*/

			// Ensure that the joint indices are never too large
			{
				size_t stride = destinationWeightVertexStride;

				for (unsigned controllerIdx=0; controllerIdx<controllers.size(); ++controllerIdx) {
					auto* bStart = &bucketStart[controllerIdx<<2];
					auto* bEnd = &bucketEnd[controllerIdx<<2];
					auto jointIndexMax = controllers[controllerIdx]._jointMatrices.size();
					for (unsigned b=0; b<4; ++b) {
						for (size_t v=bStart[b]; v<bEnd[b]; ++v) {

							const void* p = PtrAdd(skeletonBindingVertices.data(), v*stride+indicesOffset);
							if (indicesFormat == Format::R8G8_UINT) {
								for (unsigned c=0; c<unifiedVertexCount; ++c) {
									unsigned char zero   = ((unsigned char*)p)[0];
									unsigned char one    = ((unsigned char*)p)[1];
									assert(zero < jointIndexMax);
									assert(one < jointIndexMax);
								}
							} else if (indicesFormat == Format::R8G8B8A8_UINT) {
								for (unsigned c=0; c<unifiedVertexCount; ++c) {
									unsigned char zero   = ((unsigned char*)p)[0];
									unsigned char one    = ((unsigned char*)p)[1];
									unsigned char two    = ((unsigned char*)p)[2];
									unsigned char three  = ((unsigned char*)p)[3];
									assert(zero < jointIndexMax);
									assert(one < jointIndexMax);
									assert(two < jointIndexMax);
									assert(three < jointIndexMax);
								}
							} else {
								unsigned char zero   = ((unsigned char*)p)[0];
								assert(zero < jointIndexMax);
							}

						}
					}
				}
			}
                                
        #endif

            //      Calculate the local space bounding box for the input vertex buffer
            //      (assuming the position will appear in the animated vertex buffer)
        auto boundingBox = InvalidBoundingBox();
        auto positionDesc = FindPositionElement(AsPointer(animatedVertexLayout.begin()), animatedVertexLayout.size());
        if (positionDesc._nativeFormat != Format::Unknown) {
            AddToBoundingBox(
                boundingBox,
                animatedVertexBuffer.data(), animatedVertexStride, unifiedVertexCount,
                positionDesc, Identity<Float4x4>());
        }

            //      Build the final "BoundSkinnedGeometry" object
        NascentBoundSkinnedGeometry result;
		result._unanimatedVertexElements = std::move(unanimatedVertexBuffer);
		result._animatedVertexElements = std::move(animatedVertexBuffer);
		result._skeletonBinding = std::move(skeletonBindingVertices);
		result._indices = std::move(newIndexBuffer);

        result._skeletonBindingVertexStride = (unsigned)destinationWeightVertexStride;
        result._animatedVertexBufferSize = (unsigned)(animatedVertexStride*unifiedVertexCount);

        result._mainDrawCalls = sourceGeo._mainDrawCalls;
        result._mainDrawUnanimatedIA._vertexStride = unanimatedVertexStride;
        result._mainDrawUnanimatedIA._elements = std::move(unanimatedVertexLayout);
        result._indexFormat = sourceGeo._indexFormat;

        result._mainDrawAnimatedIA._vertexStride = animatedVertexStride;
        result._mainDrawAnimatedIA._elements = std::move(animatedVertexLayout);

		// Setup the per-section preskinning draw calls
		for (unsigned controllerIdx=0; controllerIdx<controllers.size(); ++controllerIdx) {
			std::vector<DrawCallDesc> preskinningDrawCalls;
			auto* bStart = &bucketStart[controllerIdx<<2];
			auto* bEnd = &bucketEnd[controllerIdx<<2];
			if (bEnd[0] > bStart[0]) {
				preskinningDrawCalls.push_back(
					DrawCallDesc{~unsigned(0x0), unsigned(bEnd[0] - bStart[0]), unsigned(bStart[0]), 4, Topology::PointList});
			}
			if (bEnd[1] > bStart[1]) {
				preskinningDrawCalls.push_back(
					DrawCallDesc{~unsigned(0x0), unsigned(bEnd[1] - bStart[1]), unsigned(bStart[1]), 2, Topology::PointList});
			}
			if (bEnd[2] > bStart[2]) {
				preskinningDrawCalls.push_back(
					DrawCallDesc{~unsigned(0x0), unsigned(bEnd[2] - bStart[2]), unsigned(bStart[2]), 1, Topology::PointList});
			}

			assert(bEnd[2] <= unifiedVertexCount);

			NascentBoundSkinnedGeometry::Section section;

			auto jointRemapping = MakeIteratorRange(buckettedControllers[controllerIdx]._jointIndexRemapping);
			if (jointRemapping.empty()) {
				section._bindShapeByInverseBindMatrices.reserve(controllers[controllerIdx]._controller->GetInverseBindMatrices().size());
				for (const auto&ibm:controllers[controllerIdx]._controller->GetInverseBindMatrices())
					section._bindShapeByInverseBindMatrices.push_back(Combine(controllers[controllerIdx]._controller->GetBindShapeMatrix(), ibm));
				section._jointMatrices = std::vector<uint16_t>(
					AsPointer(controllers[controllerIdx]._jointMatrices.begin()),
					AsPointer(controllers[controllerIdx]._jointMatrices.end()));
			} else {
				section._bindShapeByInverseBindMatrices.reserve(jointRemapping.size());
				section._jointMatrices.reserve(jointRemapping.size());

				unsigned outputCount = 0;
				for (auto i:jointRemapping) if (i!=~0u) outputCount = std::max(outputCount, i+1);
				section._bindShapeByInverseBindMatrices.resize(outputCount);
				section._jointMatrices.resize(outputCount);

				for (size_t srcIdx=0; srcIdx<jointRemapping.size(); ++srcIdx) {
					if (jointRemapping[srcIdx] == ~0u) continue;
					section._bindShapeByInverseBindMatrices[jointRemapping[srcIdx]] =
						Combine(
							controllers[controllerIdx]._controller->GetBindShapeMatrix(), 
							controllers[controllerIdx]._controller->GetInverseBindMatrices()[srcIdx]);
					section._jointMatrices[jointRemapping[srcIdx]] = controllers[controllerIdx]._jointMatrices[srcIdx];
				}
			}

			section._preskinningDrawCalls = std::move(preskinningDrawCalls);
			result._preskinningSections.emplace_back(std::move(section));
		}

        if (finalWeightBufferFormat) {
            result._preskinningIA = RenderCore::Assets::CreateGeoInputAssembly(
                *finalWeightBufferFormat, (unsigned)destinationWeightVertexStride);
        }

        result._localBoundingBox = boundingBox;
        return std::move(result);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	static void Serialize(Serialization::NascentBlockSerializer& outputSerializer, const NascentBoundSkinnedGeometry::Section& section)
	{
		Serialize(outputSerializer, section._bindShapeByInverseBindMatrices);
		Serialize(outputSerializer, section._preskinningDrawCalls);
		outputSerializer.SerializeSubBlock(MakeIteratorRange(section._jointMatrices));
        outputSerializer.SerializeValue(section._jointMatrices.size());
	}

    void NascentBoundSkinnedGeometry::SerializeWithResourceBlock(
        Serialization::NascentBlockSerializer& outputSerializer, 
        std::vector<uint8_t>& largeResourcesBlock) const
    {
        using namespace Serialization;

        auto vbOffset0 = largeResourcesBlock.size();
        auto vbSize0 = _unanimatedVertexElements.size();
        largeResourcesBlock.insert(largeResourcesBlock.end(), _unanimatedVertexElements.begin(), _unanimatedVertexElements.end());

        auto vbOffset1 = largeResourcesBlock.size();
        auto vbSize1 = _animatedVertexElements.size();
        largeResourcesBlock.insert(largeResourcesBlock.end(), _animatedVertexElements.begin(), _animatedVertexElements.end());

        auto vbOffset2 = largeResourcesBlock.size();
        auto vbSize2 = _skeletonBinding.size();
        largeResourcesBlock.insert(largeResourcesBlock.end(), _skeletonBinding.begin(), _skeletonBinding.end());

        auto ibOffset = largeResourcesBlock.size();
        auto ibSize = _indices.size();
        largeResourcesBlock.insert(largeResourcesBlock.end(), _indices.begin(), _indices.end());

            // first part is just like "NascentRawGeometry::SerializeMethod"
        Serialize(
            outputSerializer, 
            RenderCore::Assets::VertexData 
                { _mainDrawUnanimatedIA, unsigned(vbOffset0), unsigned(vbSize0) });

        Serialize(
            outputSerializer, 
            RenderCore::Assets::IndexData 
                { _indexFormat, unsigned(ibOffset), unsigned(ibSize) });

        Serialize(outputSerializer, _mainDrawCalls);

            // append skinning related information
        Serialize(
            outputSerializer, 
            RenderCore::Assets::VertexData 
                { _mainDrawAnimatedIA, unsigned(vbOffset1), unsigned(vbSize1) });
        Serialize(
            outputSerializer, 
            RenderCore::Assets::VertexData 
                { _preskinningIA, unsigned(vbOffset2), unsigned(vbSize2) });
        
		Serialize(outputSerializer, _preskinningSections);

        Serialize(outputSerializer, _localBoundingBox.first);
        Serialize(outputSerializer, _localBoundingBox.second);
    }





	void UnboundSkinController::AddInfluences(
		unsigned targetVertex,
		IteratorRange<const float*> weights,
		IteratorRange<const unsigned*> jointIndices)
	{
		assert(weights.size() == jointIndices.size());
		auto groupCount = (weights.size()+3)/4;
		if (_attachmentGroups.size() < groupCount)
			_attachmentGroups.resize(groupCount);

		unsigned influenceI = 0;
		for (unsigned g=0; g<groupCount; ++g) {
			if (_attachmentGroups[g]._weights.size() <= targetVertex*4)
				_attachmentGroups[g]._weights.resize((targetVertex+1)*4, 0);
			if (_attachmentGroups[g]._jointIndices.size() <= targetVertex*4)
				_attachmentGroups[g]._jointIndices.resize((targetVertex+1)*4, 0);

			auto subPartCount = std::min(unsigned(weights.size()) - influenceI, 4u);
			std::copy(weights.begin() + influenceI, weights.begin() + influenceI + subPartCount, _attachmentGroups[g]._weights.begin() + targetVertex*4);
			std::copy(jointIndices.begin() + influenceI, jointIndices.begin() + influenceI + subPartCount, _attachmentGroups[g]._jointIndices.begin() + targetVertex*4);

			influenceI += subPartCount;
		}

		if (_influenceCount.size() <= targetVertex)
			_influenceCount.resize(targetVertex+1, ~0u);
		assert(_influenceCount[targetVertex] == ~0u);
		_influenceCount[targetVertex] = (unsigned)weights.size();
	}

	void UnboundSkinController::ReserveInfluences(unsigned vertexCount, unsigned influencesPerVertex)
	{
		auto groupCount = (influencesPerVertex+3)/4;
		if (_attachmentGroups.size() < groupCount)
			_attachmentGroups.resize(groupCount);

		for (auto&g:_attachmentGroups) {
			g._weights.reserve(vertexCount);
			g._jointIndices.reserve(vertexCount);
		}

		_influenceCount.reserve(vertexCount);
	}

    UnboundSkinController::UnboundSkinController(   
        std::vector<Float4x4>&& inverseBindMatrices, const Float4x4& bindShapeMatrix,
        std::vector<std::string>&& jointNames)
    :       _inverseBindMatrices(std::move(inverseBindMatrices))
    ,       _bindShapeMatrix(bindShapeMatrix)
    ,       _jointNames(jointNames)
    {
    }

	template<unsigned WeightCount>
		void AccumulateJointUsage(
			const VertexWeightAttachmentBucket<WeightCount>& bucket,
			std::vector<unsigned>& accumulator)
	{
		assert(accumulator.size() >= 256);
		for (const auto&w:bucket._weightAttachments) {
			for (unsigned c=0; c<WeightCount; ++c) {
				++accumulator[w._jointIndex[c]];
			}
		}
	}

	template<unsigned WeightCount>
		void RemapJointIndices(
			VertexWeightAttachmentBucket<WeightCount>& bucket,
			IteratorRange<const unsigned*> remapping)
	{
		for (auto&w:bucket._weightAttachments) {
			for (unsigned c=0; c<WeightCount; ++c) {
				assert(w._jointIndex[c] < remapping.size());
				w._jointIndex[c] = (uint8_t)remapping[w._jointIndex[c]];
			}
		}
	}

	template void AccumulateJointUsage(const VertexWeightAttachmentBucket<1>&, std::vector<unsigned>&);
	template void AccumulateJointUsage(const VertexWeightAttachmentBucket<2>&, std::vector<unsigned>&);
	template void AccumulateJointUsage(const VertexWeightAttachmentBucket<4>&, std::vector<unsigned>&);
	template void RemapJointIndices(VertexWeightAttachmentBucket<1>&, IteratorRange<const unsigned*>);
	template void RemapJointIndices(VertexWeightAttachmentBucket<2>&, IteratorRange<const unsigned*>);
	template void RemapJointIndices(VertexWeightAttachmentBucket<4>&, IteratorRange<const unsigned*>);


    UnboundMorphController::UnboundMorphController()
    {}

    UnboundMorphController::UnboundMorphController(UnboundMorphController&& moveFrom) never_throws
    :       _source(std::move(moveFrom._source))
    {}

    UnboundMorphController& UnboundMorphController::operator=(UnboundMorphController&& moveFrom) never_throws
    {
        _source = std::move(moveFrom._source);
        return *this;
    }

    std::ostream& StreamOperator(std::ostream& stream, const NascentBoundSkinnedGeometry& geo)
    {
        using namespace RenderCore::Assets::Operators;
        stream << "   Unanimated VB bytes: " << ByteCount(geo._unanimatedVertexElements.size()) << " (" << geo._unanimatedVertexElements.size() / std::max(1u, geo._mainDrawUnanimatedIA._vertexStride) << "*" << geo._mainDrawUnanimatedIA._vertexStride << ")" << std::endl;
        stream << "     Animated VB bytes: " << ByteCount(geo._animatedVertexElements.size()) << " (" << geo._animatedVertexElements.size() / std::max(1u, geo._mainDrawAnimatedIA._vertexStride) << "*" << geo._mainDrawAnimatedIA._vertexStride << ")" << std::endl;
        stream << "Skele binding VB bytes: " << ByteCount(geo._skeletonBinding.size()) << " (" << geo._skeletonBinding.size() / std::max(1u, geo._skeletonBindingVertexStride) << "*" << geo._skeletonBindingVertexStride << ")" << std::endl;
        stream << "     Animated VB bytes: " << ByteCount(geo._animatedVertexBufferSize) << " (" << geo._animatedVertexBufferSize / std::max(1u, geo._mainDrawAnimatedIA._vertexStride) << "*" << geo._mainDrawAnimatedIA._vertexStride << ")" << std::endl;
        stream << "              IB bytes: " << ByteCount(geo._indices.size()) << " (" << (geo._indices.size()*8/BitsPerPixel(geo._indexFormat)) << "*" << BitsPerPixel(geo._indexFormat)/8 << ")" << std::endl;
        stream << " Unanimated IA: " << geo._mainDrawUnanimatedIA << std::endl;
        stream << "   Animated IA: " << geo._mainDrawAnimatedIA << std::endl;
        stream << "Preskinning IA: " << geo._preskinningIA << std::endl;
        stream << "Index fmt: " << AsString(geo._indexFormat) << std::endl;
        unsigned c=0;
        for(const auto& dc:geo._mainDrawCalls)
            stream << "Draw [" << c++ << "] " << dc << std::endl;
		for(unsigned sectionIdx=0; sectionIdx<geo._preskinningSections.size(); ++sectionIdx) {
			c=0;
			auto& section = geo._preskinningSections[sectionIdx];
			for(const auto& dc:section._preskinningDrawCalls)
				stream << "Preskinning Section [" << sectionIdx << "] Draw [" << c++ << "] " << dc << std::endl;

			stream << "Section [" << sectionIdx << "] Joint matrices: ";
			for (size_t q=0; q<section._jointMatrices.size(); ++q) {
				if (q != 0) stream << ", ";
				stream << section._jointMatrices[q];
			}
			stream << std::endl;
		}
        
        stream << std::endl;
        return stream;
    }


}}}

