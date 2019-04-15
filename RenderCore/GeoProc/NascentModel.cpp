// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentModel.h"
#include "NascentAnimController.h"
#include "GeometryAlgorithm.h"
#include "NascentObjectsSerialize.h"
#include "NascentCommandStream.h"
#include "GeoProcUtil.h"
#include "MeshDatabase.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/ModelImmutableData.h"
#include "../../Assets/NascentChunk.h"
#include "../../Core/Exceptions.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{
	static const unsigned ModelScaffoldVersion = 1;
	static const unsigned ModelScaffoldLargeBlocksVersion = 0;

	auto NascentModel::FindGeometryBlock(NascentObjectGuid id) const -> const GeometryBlock*
	{
		for (auto&g:_geoBlocks)
			if (g.first._srcObject == id)
				return &g.second;
		return nullptr;
	}

	auto NascentModel::FindSkinControllerBlock(NascentObjectGuid id) const -> const SkinControllerBlock*
	{
		for (auto&g:_skinBlocks)
			if (g.first._srcObject == id)
				return &g.second;
		return nullptr;
	}

	auto NascentModel::FindCommand(NascentObjectGuid id) const ->  const Command* 
	{
		for (auto&g:_commands)
			if (g.first._srcObject == id)
				return &g.second;
		return nullptr;
	}

	void NascentModel::Add(NascentObjectGuid id, const std::string& name, GeometryBlock&& object)
	{
		if (FindGeometryBlock(id))
			Throw(std::runtime_error("Attempting to register a GeometryBlock for a id that is already in use"));
		_geoBlocks.push_back(std::make_pair(Indexor{id, name}, std::move(object)));
	}

	void NascentModel::Add(NascentObjectGuid id, const std::string& name, SkinControllerBlock&& object)
	{
		if (FindSkinControllerBlock(id))
			Throw(std::runtime_error("Attempting to register a SkinControllerBlock for a id that is already in use"));
		_skinBlocks.push_back(std::make_pair(Indexor{id, name}, std::move(object)));
	}

	void NascentModel::Add(NascentObjectGuid id, const std::string& name, Command&& object)
	{
		if (FindCommand(id))
			Throw(std::runtime_error("Attempting to register a Command for a id that is already in use"));
		_commands.push_back(std::make_pair(Indexor{id, name}, std::move(object)));
	}

	void NascentModel::ApplyTransform(const std::string& bindingPoint, const Float4x4& transform)
	{
		for (auto&cmd:_commands) {
			if (cmd.second._localToModel == bindingPoint) {
				auto* geo = FindGeometryBlock(cmd.second._geometryBlock);
				assert(geo);
				Transform(*geo->_mesh, transform);
				cmd.second._localToModel = "identity";
			}
		}
	}

	std::vector<std::pair<std::string, std::string>> NascentModel::BuildSkeletonInterface() const
	{
		std::vector<std::pair<std::string, std::string>> result;
		for (const auto&cmd:_commands) {
			auto j = std::make_pair(std::string{}, cmd.second._localToModel);
			auto i = std::find(result.begin(), result.end(), j);
			if (i == result.end())
				result.push_back(j);
		}

		for (const auto&controller:_skinBlocks) {
			for (const auto&joint:controller.second._controller->_jointNames) {
				auto j = std::make_pair(controller.second._skeleton, joint);
				auto i = std::find(result.begin(), result.end(), j);
				if (i == result.end())
					result.push_back(j);
			}
		}
		return result;
	}

	static NascentRawGeometry CompleteInstantiation(const NascentModel::GeometryBlock& geoBlock, const NativeVBSettings& nativeVBSettings)
	{
		const bool generateMissingTangentsAndNormals = true;
        if (constant_expression<generateMissingTangentsAndNormals>::result()) {
			auto indexCount = geoBlock._indices.size() * 8 / BitsPerPixel(geoBlock._indexFormat);
            GenerateNormalsAndTangents(
                *geoBlock._mesh, 0,
				1e-3f,
                geoBlock._indices.data(), indexCount, geoBlock._indexFormat);
        }

            // If we have normals, tangents & bitangents... then we can remove one of them
            // (it will be implied by the remaining two). We can choose to remove the 
            // normal or the bitangent... Lets' remove the binormal, because it makes it 
            // easier to do low quality rendering with normal maps turned off.
        const bool removeRedundantBitangents = true;
        if (constant_expression<removeRedundantBitangents>::result())
            RemoveRedundantBitangents(*geoBlock._mesh);

        NativeVBLayout vbLayout = BuildDefaultLayout(*geoBlock._mesh, nativeVBSettings);
        auto nativeVB = geoBlock._mesh->BuildNativeVertexBuffer(vbLayout);

		std::vector<DrawCallDesc> drawCalls;
		for (const auto&d:geoBlock._drawCalls) {
			drawCalls.push_back(DrawCallDesc{d._firstIndex, d._indexCount, 0, (unsigned)drawCalls.size(), d._topology});
		}

        return NascentRawGeometry {
            nativeVB, geoBlock._indices,
            RenderCore::Assets::CreateGeoInputAssembly(vbLayout._elements, (unsigned)vbLayout._vertexStride),
            geoBlock._indexFormat,
			drawCalls,
			geoBlock._mesh->GetUnifiedVertexCount(),
			geoBlock._meshVertexIndexToSrcIndex };
	}

	class NascentGeometryObjects
	{
	public:
		std::vector<std::pair<NascentObjectGuid, NascentRawGeometry>> _rawGeos;
		std::vector<std::pair<NascentObjectGuid, NascentBoundSkinnedGeometry>> _skinnedGeos;
	};

	static std::pair<Float3, Float3> CalculateBoundingBox
		(
			const NascentGeometryObjects& geoObjects,
			const NascentModelCommandStream& scene,
			IteratorRange<const Float4x4*> transforms
		);

	static ::Assets::Blob SerializeSkin(
		Serialization::NascentBlockSerializer& serializer, 
		const NascentGeometryObjects& objs)
	{
		auto result = std::make_shared<std::vector<uint8>>();
		{
			Serialization::NascentBlockSerializer tempBlock;
			for (auto i = objs._rawGeos.begin(); i!=objs._rawGeos.end(); ++i) {
				i->second.Serialize(tempBlock, *result);
			}
			serializer.SerializeSubBlock(tempBlock);
			::Serialize(serializer, objs._rawGeos.size());
		}

		{
			Serialization::NascentBlockSerializer tempBlock;
			for (auto i = objs._skinnedGeos.begin(); i!=objs._skinnedGeos.end(); ++i) {
				i->second.Serialize(tempBlock, *result);
			}
			serializer.SerializeSubBlock(tempBlock);
			::Serialize(serializer, objs._skinnedGeos.size());
		}
		return result;
	}

    class DefaultPoseData
    {
    public:
        std::vector<Float4x4>       _defaultTransforms;
        std::pair<Float3, Float3>   _boundingBox;
    };

    static DefaultPoseData CalculateDefaultPoseData(
        const NascentSkeletonMachine& skeleton,
		const TransformationParameterSet& parameters,
        const NascentModelCommandStream& cmdStream,
        const NascentGeometryObjects& geoObjects)
    {
        DefaultPoseData result;

        auto skeletonOutput = skeleton.GenerateOutputTransforms(parameters);

        auto skelOutputInterface = skeleton.BuildHashedOutputInterface();
        auto streamInputInterface = cmdStream.BuildHashedInputInterface();
        RenderCore::Assets::SkeletonBinding skelBinding(
            RenderCore::Assets::SkeletonMachine::OutputInterface
                {AsPointer(skelOutputInterface.begin()), skelOutputInterface.size()},
            RenderCore::Assets::ModelCommandStream::InputInterface
                {	AsPointer(streamInputInterface.begin()), 
					streamInputInterface.size()});

        auto finalMatrixCount = (unsigned)streamInputInterface.size(); // immData->_visualScene.GetInputInterface()._jointCount;
        result._defaultTransforms.resize(finalMatrixCount);
        for (unsigned c=0; c<finalMatrixCount; ++c) {
            auto machineOutputIndex = skelBinding.ModelJointToMachineOutput(c);
            if (machineOutputIndex == ~unsigned(0x0)) {
                result._defaultTransforms[c] = Identity<Float4x4>();
            } else {
                result._defaultTransforms[c] = skeletonOutput[machineOutputIndex];
            }
        }

            // if we have any non-identity internal transforms, then we should 
            // write a default set of transformations. But many models don't have any
            // internal transforms -- in this case all of the generated transforms
            // will be identity. If we find this case, they we should write zero
            // default transforms.
        bool hasNonIdentity = false;
        const float tolerance = 1e-6f;
        for (unsigned c=0; c<finalMatrixCount; ++c)
            hasNonIdentity |= !Equivalent(result._defaultTransforms[c], Identity<Float4x4>(), tolerance);
        if (!hasNonIdentity) {
            finalMatrixCount = 0u;
            result._defaultTransforms.clear();
        }

        result._boundingBox = CalculateBoundingBox(
            geoObjects, cmdStream, MakeIteratorRange(result._defaultTransforms));

        return result;
    }

	#include "../Utility/ExposeStreamOp.h"

    std::ostream& operator<<(std::ostream& stream, const NascentGeometryObjects& geos)
    {
        using namespace Operators;

        stream << " --- Geos:" << std::endl;
        unsigned c=0;
        for (const auto& g:geos._rawGeos)
            stream << "[" << c++ << "] (0x" << std::hex << g.first._objectId << std::dec << ") Geo --- " << std::endl << g.second << std::endl;

        stream << " --- Skinned Geos:" << std::endl;
        c=0;
        for (const auto& g:geos._skinnedGeos)
            stream << "[" << c++ << "] (0x" << std::hex << g.first._objectId << std::dec << ") Skinned geo --- " << std::endl << g.second << std::endl;
        return stream;
    }

	static void TraceMetrics(std::ostream& stream, const NascentGeometryObjects& geoObjects, const NascentModelCommandStream& cmdStream, const NascentSkeleton& skeleton)
	{
		stream << "============== Geometry Objects ==============" << std::endl;
		stream << geoObjects;
		stream << std::endl;
		stream << "============== Command stream ==============" << std::endl;
		stream << cmdStream;
		stream << std::endl;
		stream << "============== Transformation Machine ==============" << std::endl;
		StreamOperator(stream, skeleton.GetSkeletonMachine(), skeleton.GetDefaultParameters());
	}

	std::vector<::Assets::ICompileOperation::OperationResult> SerializeSkinToChunks(const std::string& name, const NascentGeometryObjects& geoObjects, const NascentModelCommandStream& cmdStream, const NascentSkeleton& skeleton)
	{
		Serialization::NascentBlockSerializer serializer;

		::Serialize(serializer, cmdStream);
		auto largeResourcesBlock = SerializeSkin(serializer, geoObjects);
		::Serialize(serializer, skeleton);

			// Generate the default transforms and serialize them out
			// unfortunately this requires we use the run-time types to
			// work out the transforms.
			// And that requires a bit of hack to get pointers to those 
			// run-time types
		{
			auto defaultPoseData = CalculateDefaultPoseData(skeleton.GetSkeletonMachine(), skeleton.GetDefaultParameters(), cmdStream, geoObjects);
			serializer.SerializeSubBlock(MakeIteratorRange(defaultPoseData._defaultTransforms));
			serializer.SerializeValue(size_t(defaultPoseData._defaultTransforms.size()));
			::Serialize(serializer, defaultPoseData._boundingBox.first);
			::Serialize(serializer, defaultPoseData._boundingBox.second);
		}

		// Find the max LOD value, and serialize that
		::Serialize(serializer, cmdStream.GetMaxLOD());

		// Serialize human-readable metrics information
		std::stringstream metricsStream;
		TraceMetrics(metricsStream, geoObjects, cmdStream, skeleton);

		auto scaffoldBlock = ::Assets::AsBlob(serializer);
		auto metricsBlock = ::Assets::AsBlob(metricsStream);

		return
			{
				::Assets::ICompileOperation::OperationResult{
					RenderCore::Assets::ChunkType_ModelScaffold, ModelScaffoldVersion, name,
					std::move(scaffoldBlock)},
				::Assets::ICompileOperation::OperationResult{
					RenderCore::Assets::ChunkType_ModelScaffoldLargeBlocks, ModelScaffoldLargeBlocksVersion, name,
					std::move(largeResourcesBlock)},
				::Assets::ICompileOperation::OperationResult{
					RenderCore::Assets::ChunkType_Metrics, 0, "metrics", 
					std::move(metricsBlock)}
			};
	}

	std::vector<::Assets::ICompileOperation::OperationResult> NascentModel::SerializeToChunks(const std::string& name, const NascentSkeleton& embeddedSkeleton, const NativeVBSettings& nativeSettings) const
	{
		NascentGeometryObjects geoObjects;
		NascentModelCommandStream cmdStream;

		for (const auto&cmd:_commands) {
			auto* geoBlock = FindGeometryBlock(cmd.second._geometryBlock);
			if (!geoBlock) continue;

			std::vector<uint64_t> materialGuid;
			materialGuid.reserve(cmd.second._materialBindingSymbols.size());
			for (const auto&mat:cmd.second._materialBindingSymbols) {
				const char* end = nullptr;
				auto guid = XlAtoUI64(mat.c_str(), &end, 16);
				if (end != nullptr || end == '\0') {
					materialGuid.push_back(guid);
				} else
					materialGuid.push_back(Hash64(mat));
			}

			auto* skinController = FindSkinControllerBlock(cmd.second._skinControllerBlock);
			if (!skinController) {
				auto i = std::find_if(geoObjects._rawGeos.begin(), geoObjects._rawGeos.end(),
					[&cmd](const std::pair<NascentObjectGuid, NascentRawGeometry>& p) { return p.first == cmd.second._geometryBlock; });
				if (i == geoObjects._rawGeos.end()) {
					auto rawGeo = CompleteInstantiation(*geoBlock, nativeSettings);
					geoObjects._rawGeos.emplace_back(
						std::make_pair(cmd.second._geometryBlock, std::move(rawGeo)));
					i = geoObjects._rawGeos.end ()-1;
				}

				cmdStream.Add(
					NascentModelCommandStream::GeometryInstance {
						(unsigned)std::distance(geoObjects._rawGeos.begin(), i),
						cmdStream.RegisterInputInterfaceMarker({}, cmd.second._localToModel),
						std::move(materialGuid),
						cmd.second._levelOfDetail
					});
			} else {
				auto i = std::find_if(geoObjects._skinnedGeos.begin(), geoObjects._skinnedGeos.end(),
					[&cmd](const std::pair<NascentObjectGuid, NascentBoundSkinnedGeometry>& p) { return p.first == cmd.second._skinControllerBlock; });
				if (i == geoObjects._skinnedGeos.end()) {
					auto rawGeo = CompleteInstantiation(*geoBlock, nativeSettings);
					DynamicArray<uint16> jointMatrices(
						std::make_unique<uint16[]>(skinController->_controller->_jointNames.size()),
						skinController->_controller->_jointNames.size());
					for (unsigned c=0; c<skinController->_controller->_jointNames.size(); ++c) {
						jointMatrices[c] = (uint16)cmdStream.RegisterInputInterfaceMarker(
							skinController->_skeleton,
							skinController->_controller->_jointNames[c]);
					}
					auto boundController = BindController(
						rawGeo,
						*skinController->_controller,
						std::move(jointMatrices),
						"");
					geoObjects._skinnedGeos.emplace_back(
						std::make_pair(cmd.second._skinControllerBlock, std::move(boundController)));
					i = geoObjects._skinnedGeos.end()-1;
				}

				cmdStream.Add(
					NascentModelCommandStream::SkinControllerInstance {
						(unsigned)std::distance(geoObjects._skinnedGeos.begin(), i),
						cmdStream.RegisterInputInterfaceMarker({}, cmd.second._localToModel),
						std::move(materialGuid),
						cmd.second._levelOfDetail
					});
			}
		}

		return SerializeSkinToChunks(name, geoObjects, cmdStream, embeddedSkeleton);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static std::pair<Float3, Float3> CalculateBoundingBox
		(
			const NascentGeometryObjects& geoObjects,
			const NascentModelCommandStream& scene,
			IteratorRange<const Float4x4*> transforms
		)
	{
		//
		//      For all the parts of the model, calculate the bounding box.
		//      We just have to go through each vertex in the model, and
		//      transform it into model space, and calculate the min and max values
		//      found;
		//
		auto result = InvalidBoundingBox();
		// const auto finalMatrices = 
		//     _skeleton.GetTransformationMachine().GenerateOutputTransforms(
		//         _animationSet.BuildTransformationParameterSet(0.f, nullptr, _skeleton, _objects));

		//
		//      Do the unskinned geometry first
		//

		for (auto i=scene._geometryInstances.cbegin(); i!=scene._geometryInstances.cend(); ++i) {
			const NascentModelCommandStream::GeometryInstance& inst = *i;

			if (inst._id >= geoObjects._rawGeos.size()) continue;
			const auto* geo = &geoObjects._rawGeos[inst._id].second;

			Float4x4 localToWorld = Identity<Float4x4>();
			if (inst._localToWorldId < transforms.size())
				localToWorld = transforms[inst._localToWorldId];

			const void*         vertexBuffer = geo->_vertices.data();
			const unsigned      vertexStride = geo->_mainDrawInputAssembly._vertexStride;

			auto positionDesc = FindPositionElement(
				AsPointer(geo->_mainDrawInputAssembly._elements.begin()),
				geo->_mainDrawInputAssembly._elements.size());

			if (positionDesc._nativeFormat != Format::Unknown && vertexStride) {
				AddToBoundingBox(
					result, vertexBuffer, vertexStride, 
					geo->_vertices.size() / vertexStride, positionDesc, localToWorld);
			}
		}

		//
		//      Now also do the skinned geometry. But use the default pose for
		//      skinned geometry (ie, don't apply the skinning transforms to the bones).
		//      Obvious this won't give the correct result post-animation.
		//

		for (auto i=scene._skinControllerInstances.cbegin(); i!=scene._skinControllerInstances.cend(); ++i) {
			const NascentModelCommandStream::SkinControllerInstance& inst = *i;

			if (inst._id >= geoObjects._skinnedGeos.size()) continue;
			const auto* controller = &geoObjects._skinnedGeos[inst._id].second;
			if (!controller) continue;

			Float4x4 localToWorld = Identity<Float4x4>();
			if (inst._localToWorldId < transforms.size())
				localToWorld = transforms[inst._localToWorldId];

			//  We can't get the vertex position data directly from the vertex buffer, because
			//  the "bound" object is already using an opaque hardware object. However, we can
			//  transform the local space bounding box and use that.

			const unsigned indices[][3] = 
			{
				{0,0,0}, {0,1,0}, {1,0,0}, {1,1,0},
				{0,0,1}, {0,1,1}, {1,0,1}, {1,1,1}
			};

			const Float3* A = (const Float3*)&controller->_localBoundingBox.first;
			for (unsigned c=0; c<dimof(indices); ++c) {
				Float3 position(A[indices[c][0]][0], A[indices[c][1]][1], A[indices[c][2]][2]);
				AddToBoundingBox(result, position, localToWorld);
			}
		}

		assert(!isinf(result.first[0]) && !isinf(result.first[1]) && !isinf(result.first[2]));
		assert(!isinf(result.second[0]) && !isinf(result.second[1]) && !isinf(result.second[2]));

		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	bool ModelTransMachineOptimizer::CanMergeIntoOutputMatrix(unsigned outputMatrixIndex) const
    {
        if (outputMatrixIndex < unsigned(_canMergeIntoTransform.size()))
            return _canMergeIntoTransform[outputMatrixIndex];
        return false;
    }

    void ModelTransMachineOptimizer::MergeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform)
    {
        assert(CanMergeIntoOutputMatrix(outputMatrixIndex));
        _mergedTransforms[outputMatrixIndex] = Combine(
            _mergedTransforms[outputMatrixIndex], transform);
    }

    ModelTransMachineOptimizer::ModelTransMachineOptimizer(
		const NascentModel& model,
		IteratorRange<const std::pair<std::string, std::string>*> bindingNameInterface)
	: _bindingNameInterface(bindingNameInterface.begin(), bindingNameInterface.end())
    {
		auto outputMatrixCount = bindingNameInterface.size();
        _canMergeIntoTransform.resize(outputMatrixCount, false);
        _mergedTransforms.resize(outputMatrixCount, Identity<Float4x4>());

        for (unsigned c=0; c<outputMatrixCount; ++c) {

			if (!bindingNameInterface[c].first.empty()) continue;

			bool skinAttached = false;
			bool doublyAttachedObject = false;
			bool atLeastOneAttached = false;
			for (const auto&cmd:model.GetCommands())
				if (cmd.second._localToModel == bindingNameInterface[c].second) {
					atLeastOneAttached = true;

					// if we've got a skin controller attached, we can't do any merging
					skinAttached |= cmd.second._skinControllerBlock != NascentObjectGuid{};

					// find all of the meshes attached, and check if any are attached in
					// multiple places
					for (const auto&cmd2:model.GetCommands())
						doublyAttachedObject |= cmd2.second._geometryBlock == cmd.second._geometryBlock && cmd2.second._localToModel != cmd.second._localToModel;
				}

            _canMergeIntoTransform[c] = atLeastOneAttached && !skinAttached && !doublyAttachedObject;
        }
    }

    ModelTransMachineOptimizer::ModelTransMachineOptimizer() {}

    ModelTransMachineOptimizer::~ModelTransMachineOptimizer()
    {}

	void OptimizeSkeleton(NascentSkeleton& embeddedSkeleton, NascentModel& model)
	{
		{
			auto filteringSkeleInterface = model.BuildSkeletonInterface();
			filteringSkeleInterface.insert(filteringSkeleInterface.begin(), std::make_pair(std::string{}, "identity"));
			embeddedSkeleton.GetSkeletonMachine().FilterOutputInterface(MakeIteratorRange(filteringSkeleInterface));
		}

		{
			auto finalSkeleInterface = embeddedSkeleton.GetSkeletonMachine().GetOutputInterface();
			ModelTransMachineOptimizer optimizer(model, finalSkeleInterface);
			embeddedSkeleton.GetSkeletonMachine().Optimize(optimizer);
			assert(embeddedSkeleton.GetSkeletonMachine().GetOutputMatrixCount() == finalSkeleInterface.size());

			for (unsigned c=0; c<finalSkeleInterface.size(); ++c) {
				const auto& mat = optimizer.GetMergedOutputMatrices()[c];
				if (!Equivalent(mat, Identity<Float4x4>(), 1e-3f)) {
					assert(finalSkeleInterface[c].first.empty());	// this operation only makes sense for the basic structure skeleton
					model.ApplyTransform(finalSkeleInterface[c].second, mat);
				}
			}
		}
	}

}}}
