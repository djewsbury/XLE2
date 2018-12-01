// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Scaffold.h"
#include "ScaffoldParsingUtil.h"    // for AsString
#include "ConversionConfig.h"

#include "SEffect.h"
#include "SCommandStream.h"
#include "SAnimation.h"
#include "SCommandStream.h"

#include "../RenderCore/GeoProc/NascentCommandStream.h"
#include "../RenderCore/GeoProc/NascentRawGeometry.h"
#include "../RenderCore/GeoProc/NascentAnimController.h"
#include "../RenderCore/GeoProc/NascentGeometryObjects.h"
#include "../RenderCore/GeoProc/NascentObjectsSerialize.h"
#include "../RenderCore/GeoProc/SkeletonRegistry.h"

#include "../RenderCore/Assets/ModelImmutableData.h"      // just for RenderCore::Assets::SkeletonBinding
#include "../RenderCore/Assets/AssetUtils.h"
#include "../RenderCore/Assets/RawMaterial.h"
#include "../Assets/ICompileOperation.h"
#include "../Assets/IFileSystem.h"

#include "../Utility/Streams/FileUtils.h"
#include "../Utility/Streams/XmlStreamFormatter.h"
#include "../Utility/Streams/Stream.h"
#include "../Utility/Streams/StreamTypes.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/FileSystemMonitor.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringFormat.h"
#include "../ConsoleRig/OutputStream.h"
#include "../ConsoleRig/AttachableLibrary.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../ConsoleRig/Log.h"
#include <memory>

namespace ColladaConversion
{

	static const auto ChunkType_Text = ConstHash64<'Text'>::Value;

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ColladaCompileOp : public ::Assets::ICompileOperation
    {
    public:
        std::string _name;
		::Assets::rstring _rootNode;
		ImportConfiguration _cfg;
        MemoryMappedFile _fileData;
        std::shared_ptr<DocumentScaffold> _doc;
        ::ColladaConversion::URIResolveContext _resolveContext;
		std::vector<TargetDesc> _targets;
		std::vector<::Assets::DependentFileState> _dependencies;

		std::vector<TargetDesc> GetTargets() const;
		std::vector<OperationResult> SerializeTarget(unsigned idx);
		std::vector<::Assets::DependentFileState> GetDependencies() const;

		ColladaCompileOp();
		~ColladaCompileOp();
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    static Float4x4 BuildCoordinateTransform(const AssetDesc& assetDesc)
    {
        // When the "up" vector, or the units specified in the collada header
        // don't match our expectations, we can apply an extra transform.
        // This should transform some given input coordinates into our expected
        // final output.
        // Here, we will convert to 1.f unit == 1.f meter, with +Z being up.
        float scale = assetDesc._metersPerUnit;
        Float3x3 axisTransform;
        switch (assetDesc._upAxis) {
        case AssetDesc::UpAxis::X:
                // -Y --> +X
                // +Z --> -Y
                // +X --> +Z
            axisTransform = Float3x3(
                 0.f, -1.f,  0.f,
                 0.f,  0.f, -1.f,
                 1.f,  0.f,  0.f);
            break;

        case AssetDesc::UpAxis::Y:
                // +X --> +X
                // +Z --> -Y
                // +Y --> +Z
                //  hmm... winding flip...?
            axisTransform = Float3x3(
                 1.f,  0.f,   0.f,
                 0.f,  0.f,  -1.f,
                 0.f,  1.f,   0.f);
            break;

        default:
        case AssetDesc::UpAxis::Z:
            axisTransform = Identity<Float3x3>();
            break;
        }

        return AsFloat4x4(Float3x3(scale * axisTransform));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class TransMachineOptimizer : public RenderCore::Assets::ITransformationMachineOptimizer
    {
    public:
        bool CanMergeIntoOutputMatrix(unsigned outputMatrixIndex) const;
        void MergeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform);
        Float4x4 GetMergedOutputMatrix(unsigned outputMatrixIndex) const;

        TransMachineOptimizer(
			ReferencedGeometries& refGeos, unsigned outputMatrixCount, const VisualScene& scene,
			const SkeletonRegistry& skeleReg, IteratorRange<uint64*> outputMatricesBindingValues);
        TransMachineOptimizer();
        ~TransMachineOptimizer();
    protected:
        std::vector<bool>       _canMergeIntoTransform;
        std::vector<Float4x4>   _mergedTransforms;
    };

    bool TransMachineOptimizer::CanMergeIntoOutputMatrix(unsigned outputMatrixIndex) const
    {
        if (outputMatrixIndex < unsigned(_canMergeIntoTransform.size()))
            return _canMergeIntoTransform[outputMatrixIndex];
        return false;
    }

    void TransMachineOptimizer::MergeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform)
    {
        assert(CanMergeIntoOutputMatrix(outputMatrixIndex));
        _mergedTransforms[outputMatrixIndex] = Combine(
            _mergedTransforms[outputMatrixIndex], transform);
    }

    Float4x4 TransMachineOptimizer::GetMergedOutputMatrix(unsigned outputMatrixIndex) const
    {
        if (outputMatrixIndex < unsigned(_mergedTransforms.size()))
            return _mergedTransforms[outputMatrixIndex];
        return Identity<Float4x4>();
    }

    TransMachineOptimizer::TransMachineOptimizer(
		ReferencedGeometries& refGeos, unsigned outputMatrixCount, const VisualScene& scene,
		const SkeletonRegistry& skeleReg, IteratorRange<uint64*> outputMatricesBindingValues)
    {
        _canMergeIntoTransform.resize(outputMatrixCount, false);
        _mergedTransforms.resize(outputMatrixCount, Identity<Float4x4>());

            // if there are any skin controllers at all, we need to prevent merging for now
        if (refGeos._skinControllers.empty()) {
            for (unsigned c=0; c<outputMatrixCount; ++c) {

					// find the NascentObjectGuid associated with this outputMatrixIndex
					//	 -- we'll use this to see if any objects are attached
				auto bindingHash = outputMatricesBindingValues[c];
				NascentObjectGuid attachmentPoint;
				bool foundAttachmentPoint = false;
				for (auto&i:skeleReg.GetImportantNodes())
					if (Hash64(i._bindingName) == bindingHash) {
						foundAttachmentPoint = true;
						attachmentPoint = i._id;
					}
				assert(foundAttachmentPoint);

                    // if we've got a skin controller attached, we can't do any merging
                auto skinI = std::find_if(
                    refGeos._skinControllers.cbegin(), refGeos._skinControllers.cend(),
                    [attachmentPoint](const ReferencedGeometries::AttachedObject& obj) { return obj._nodeGuid == attachmentPoint; });
                if (skinI != refGeos._skinControllers.cend()) continue;

                    // check to see if we have at least one mesh attached...
                auto geoI = std::find_if(
                    refGeos._meshes.cbegin(), refGeos._meshes.cend(),
                    [attachmentPoint](const ReferencedGeometries::AttachedObject& obj) { return obj._nodeGuid == attachmentPoint; });
                if (geoI == refGeos._meshes.cend()) continue;

                    // find all of the meshes attached, and check if any are attached in
                    // multiple places
                bool doublyAttachedObject = false;
                for (auto i=refGeos._meshes.cbegin(); i!=refGeos._meshes.cend() && !doublyAttachedObject; ++i) {
                    if (i->_nodeGuid == attachmentPoint) {
                        GuidReference refGuid(scene.GetInstanceGeometry(i->_objectIndex)._reference);
                        for (auto i2=refGeos._meshes.cbegin(); i2!=refGeos._meshes.cend(); ++i2) {
                            GuidReference refGuid2(scene.GetInstanceGeometry(i2->_objectIndex)._reference);
                            if (refGuid == refGuid2 && !(i2->_nodeGuid == i->_nodeGuid)) {
                                doublyAttachedObject = true;	// (ie, single object attached to multiple parents)
                                break;
                            }
                        }
                    }
                }

                _canMergeIntoTransform[c] = !doublyAttachedObject;
            }
        }
    }

    TransMachineOptimizer::TransMachineOptimizer() {}

    TransMachineOptimizer::~TransMachineOptimizer()
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PreparedSkinFile
    {
    public:
        NascentModelCommandStream _cmdStream;
        NascentGeometryObjects _geoObjects;
        NascentSkeleton _skeleton;

        PreparedSkinFile(const ColladaCompileOp&, const VisualScene&, StringSection<utf8>);
    };

    PreparedSkinFile::PreparedSkinFile(const ColladaCompileOp& input, const VisualScene& scene, StringSection<utf8> rootNode)
    {
        SkeletonRegistry jointRefs;

        ReferencedGeometries refGeos;
        bool gatherSuccess = refGeos.Gather(scene.GetRootNode(), rootNode, jointRefs);

        if (!gatherSuccess) {
            StringMeld<1024> meld;
            using ::operator<<;
            meld << "Error while looking for root node: " << rootNode.AsString().c_str() << ". Known nodes: ";

            auto nodes = scene.GetRootNode().FindAllBreadthFirst([](const Node& n) { return true;});
            for (unsigned c=0; c<nodes.size(); ++c) {
                if (c!=0) meld << ", ";
                meld << nodes[c].GetName().AsString().c_str();
            }

            Throw(::Exceptions::BasicLabel(meld.get()));
        }

            // The skeleton joints won't be included in the skeleton
            // until we call FindSkinJoints. We don't really need the
            // full skeleton embedded in the skin file... When the skeleton
            // is not there, we will just see the model in the bind pose.
            // But it can be handy to be there for previewing models quickly.
        refGeos.FindSkinJoints(scene, input._resolveContext, jointRefs);

            // We can now build the skeleton (because ReferencedGeometries::Gather 
            // has initialised jointRefs.

        unsigned topLevelPops = 0;
        auto coordinateTransform = BuildCoordinateTransform(input._doc->GetAssetDesc());
        if (!Equivalent(coordinateTransform, Identity<Float4x4>(), 1e-5f)) {
                // Push on the coordinate transform (if there is one)
                // This should be optimised into other matrices (or even into
                // the geometry) when we perform the skeleton optimisation steps.
            topLevelPops = _skeleton.GetSkeletonMachine().PushTransformation(
                coordinateTransform);
        }

            // When extracting an internal node, we ignore the transform 
            // on that internal node
        BuildSkeleton(_skeleton, scene.GetRootNode(), rootNode, jointRefs, false);
        _skeleton.GetSkeletonMachine().Pop(topLevelPops);

            // For each output matrix, we want to know if we can merge a transformation into it.
            // We can only do this if (unskinned) geometry instances are attached -- and those
            // geometry instances must be attached in only one place. If the output transform does
            // not have a geometry instance attached, or if any of the geometry instances are
            // attached to more than one matrix, or if something other than a geometry instance is
            // attached, then we cannot do any merging.
        
		auto skelOutputInterface = _skeleton.GetInterface().GetOutputInterface();
        TransMachineOptimizer optimizer(refGeos, _skeleton.GetSkeletonMachine().GetOutputMatrixCount(), scene, jointRefs, MakeIteratorRange(skelOutputInterface));
        _skeleton.GetSkeletonMachine().Optimize(optimizer);

            // We can try to optimise the skeleton here. We should collect the list
            // of meshes that we can optimise transforms into (ie, meshes that aren't
            // used in multiple places, and that aren't skinned).
            // We need to collect that list of transforms before we actually instantiate
            // the geometry -- so that merging in the changes can be done in the instantiate
            // step.

        for (auto c:refGeos._meshes) {
            TRY {
				auto bindingName = jointRefs.GetBindingName(c._nodeGuid);
				assert(!bindingName.empty());
				auto outputMatrixMarker = _cmdStream.RegisterInputInterfaceMarker(bindingName);

				auto geo = InstantiateGeometry(
					scene.GetInstanceGeometry(c._objectIndex), input._resolveContext,
					optimizer.GetMergedOutputMatrix(outputMatrixMarker),
					_geoObjects, input._cfg);

                _cmdStream.Add(NascentModelCommandStream::GeometryInstance { geo._geoId, outputMatrixMarker, std::move(geo._materials), c._levelOfDetail });
            } CATCH(const std::exception& e) {
                Log(Warning) << "Got exception while instantiating geometry (" << scene.GetInstanceGeometry(c._objectIndex)._reference.AsString().c_str() << "). Exception details:" << std::endl;
                Log(Warning) << e.what() << std::endl;
            } CATCH(...) {
                Log(Warning) << "Got unknown exception while instantiating geometry (" << scene.GetInstanceGeometry(c._objectIndex)._reference.AsString().c_str() << ")." << std::endl;
            } CATCH_END
        }

        for (auto c:refGeos._skinControllers) {

			auto bindingName = jointRefs.GetBindingName(c._nodeGuid);
			assert(!bindingName.empty());
			auto outputMatrixMarker = _cmdStream.RegisterInputInterfaceMarker(bindingName);

            bool skinSuccessful = false;
            TRY {
				auto* cmdStream = &_cmdStream;
                auto geo =
                    InstantiateController(
                        scene.GetInstanceController(c._objectIndex), input._resolveContext, 
						[&jointRefs, cmdStream](const NascentObjectGuid& guid) -> unsigned
						{
							auto bindingName = jointRefs.GetBindingName(guid);
							return cmdStream->RegisterInputInterfaceMarker(bindingName);
						},
                        _geoObjects, input._cfg);
				_cmdStream.Add(NascentModelCommandStream::SkinControllerInstance { geo._geoId, outputMatrixMarker, std::move(geo._materials), c._levelOfDetail });
                skinSuccessful = true;
            } CATCH(const std::exception& e) {
                Log(Warning) << "Got exception while instantiating controller (" << scene.GetInstanceController(c._objectIndex)._reference.AsString().c_str() << "). Exception details:" << std::endl;
                Log(Warning) << e.what() << std::endl;
            } CATCH(...) {
                Log(Warning) << "Got unknown exception while instantiating controller (" << scene.GetInstanceController(c._objectIndex)._reference.AsString().c_str() << ")." << std::endl;
            } CATCH_END

            if (!skinSuccessful) {
                    // if we failed to instantiate this object as a skinned controller,
                    // we can try to fall back to a static geometry object. This fallback
                    // can be required for some controller objects that use rigid animation
                    //  -- they can have a skin controller with no joints (meaning at the 
                    //      only transform that can affect them is the parent node -- or maybe the skeleton root?)
                Log(Warning) << "Could not instantiate controller as a skinned object. Falling back to rigid object." << std::endl;
                TRY {
                    auto geo =
                        InstantiateGeometry(
                            scene.GetInstanceController(c._objectIndex), input._resolveContext, 
                            Identity<Float4x4>(),
                            _geoObjects, input._cfg);
					_cmdStream.Add(NascentModelCommandStream::GeometryInstance { geo._geoId, outputMatrixMarker, std::move(geo._materials), c._levelOfDetail });
                } CATCH(const std::exception& e) {
                    Log(Warning) << "Got exception while instantiating geometry (after controller failed) (" << scene.GetInstanceController(c._objectIndex)._reference.AsString().c_str() << "). Exception details:" << std::endl;
                    Log(Warning) << e.what() << std::endl;
                } CATCH(...) {
                    Log(Warning) << "Got unknown exception while instantiating geometry (after controller failed) (" << scene.GetInstanceController(c._objectIndex)._reference.AsString().c_str() << ")." << std::endl;
                } CATCH_END
            }
        }

            // register the names so the skeleton and command stream can be bound together
        // RegisterNodeBindingNames(_skeleton, jointRefs);
        RegisterNodeBindingNames(_cmdStream, jointRefs);
    }

    std::vector<::Assets::ICompileOperation::OperationResult> SerializeSkin(const ColladaCompileOp& model, const char startingNode[])
    {
        const auto* scene = model._doc->FindVisualScene(
            GuidReference(model._doc->_visualScene)._id);
        if (!scene)
            Throw(::Exceptions::BasicLabel("No visual scene found"));

        StringSection<utf8> startingNodeName;
        if (startingNode) startingNodeName = (const utf8*)startingNode;
        PreparedSkinFile skinFile(model, *scene, startingNodeName);

            // Serialize the prepared skin file data to a BlockSerializer
		return SerializeSkinToChunks(
			model._name.c_str(),
			skinFile._geoObjects, skinFile._cmdStream, skinFile._skeleton);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PreparedSkeletonFile
    {
    public:
        NascentSkeleton _skeleton;

        PreparedSkeletonFile(const ColladaCompileOp&);
    };

    PreparedSkeletonFile::PreparedSkeletonFile(const ColladaCompileOp& input)
    {
        const auto* scene = input._doc->FindVisualScene(
            GuidReference(input._doc->_visualScene)._id);
        if (!scene)
            Throw(::Exceptions::BasicLabel("No visual scene found"));

        SkeletonRegistry jointRefs;
        BuildSkeleton(_skeleton, scene->GetRootNode(), StringSection<utf8>(), jointRefs, true);
        RegisterNodeBindingNames(_skeleton, jointRefs);
        TransMachineOptimizer optimizer;
        _skeleton.GetSkeletonMachine().Optimize(optimizer);
    }

    std::vector<::Assets::ICompileOperation::OperationResult> SerializeSkeleton(const ColladaCompileOp& model, const char[])
    {
        PreparedSkeletonFile skeleFile(model);
        return SerializeSkeletonToChunks(model._name.c_str(), skeleFile._skeleton);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static void SerializeMatTable(OutputStream& stream, const ColladaCompileOp& model)
    {
        OutputStreamFormatter formatter(stream);

        std::vector<std::pair<NascentObjectGuid, RenderCore::Assets::RawMaterial>> compiledEffects;

        const auto& effects = model._doc->_effects;
        for (auto i=effects.cbegin(); i!=effects.cend(); ++i) {
            TRY
            {
                NascentObjectGuid id = i->GetId().GetHash();
                compiledEffects.insert(
                    LowerBound(compiledEffects, id), 
                    std::make_pair(id, Convert(*i, model._resolveContext, model._cfg)));
            } CATCH (...) {
            } CATCH_END
        }

        const auto& mats = model._doc->_materials;
        for (auto m=mats.cbegin(); m!=mats.cend(); ++m) {
            GuidReference effect(m->_effectReference);
            auto i = LowerBound(compiledEffects, NascentObjectGuid(effect._id, effect._fileHash));
            if (i == compiledEffects.end() || !(i->first == NascentObjectGuid(effect._id, effect._fileHash)))
                continue;

            auto ele = formatter.BeginElement(m->_name.AsString());
            i->second.Serialize(formatter);
            formatter.EndElement(ele);
        }
    }

    std::vector<::Assets::ICompileOperation::OperationResult> SerializeMaterials(const ColladaCompileOp& model, const char[])  
    { 
        // std::string matSettingsFile;
        // {
        //     ::Assets::ResChar settingsName[MaxPath];
        //     XlBasename(settingsName, dimof(settingsName), model._name.c_str());
        //     XlChopExtension(settingsName);
        //     XlCatString(settingsName, dimof(settingsName), ".material");
        //     matSettingsFile = settingsName;
        // }

        MemoryOutputStream<uint8> strm;
        SerializeMatTable(strm, model);
        return {
			::Assets::ICompileOperation::OperationResult{
				ChunkType_Text, 0, model._name,
				::Assets::AsBlob(MakeIteratorRange(strm.GetBuffer().Begin(), strm.GetBuffer().End()))}
		};
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PreparedAnimationFile
    {
    public:
        NascentAnimationSet _animationSet;
        std::vector<RenderCore::Assets::RawAnimationCurve> _curves;

        PreparedAnimationFile(const ColladaCompileOp&);
    };

    PreparedAnimationFile::PreparedAnimationFile(const ColladaCompileOp& input)
    {
        SkeletonRegistry jointRefs;

        std::vector<UnboundAnimation> anims;
        const auto& animations = input._doc->_animations;
        for (auto i=animations.cbegin(); i!=animations.cend(); ++i) {
            TRY {
                auto anim = Convert(*i, input._resolveContext, jointRefs); 

                for (auto c=anim._curves.begin(); c!=anim._curves.end(); ++c) {
                    _curves.emplace_back(std::move(c->_curve));
                    _animationSet.AddAnimationDriver(
                        c->_parameterName, unsigned(_curves.size()-1),
                        c->_samplerType, c->_samplerOffset);
                }
            } CATCH (...) {
            } CATCH_END
        }
    }

	std::vector<::Assets::ICompileOperation::OperationResult> SerializeAnimations(const ColladaCompileOp& model, const char[])  
	{
		PreparedAnimationFile animFile(model);
		return SerializeAnimationsToChunks(
			model._name.c_str(), animFile._animationSet,
			MakeIteratorRange(animFile._curves));
	}

#if 0
    class WorkingAnimationSet
    {
    public:
        NascentAnimationSet _animationSet;
        std::vector<RenderCore::Assets::RawAnimationCurve> _curves;
        std::string _name;

        WorkingAnimationSet(const char name[]) : _name(name) {}
    };

    static void DestroyWorkingAnimSet(const void* model) { delete (WorkingAnimationSet*)model; }

    dll_export std::shared_ptr<WorkingAnimationSet> CreateAnimationSet(const char name[])
    {
        return std::shared_ptr<WorkingAnimationSet>(
            new WorkingAnimationSet(name),
            &DestroyWorkingAnimSet);
    }

	

	dll_export void ExtractAnimations(WorkingAnimationSet& dest, const ::Assets::ICompileOperation& source, const char animationName[])
    {
        PreparedAnimationFile animFile((ColladaCompileOp&)source);
        dest._animationSet.MergeAnimation(
            animFile._animationSet, animationName, 
            animFile._curves, dest._curves);
    }
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

	static const uint64 Type_Model = ConstHash64<'Mode', 'l'>::Value;
	static const uint64 Type_AnimationSet = ConstHash64<'Anim', 'Set'>::Value;
	static const uint64 Type_Skeleton = ConstHash64<'Skel', 'eton'>::Value;
	static const uint64 Type_RawMat = ConstHash64<'RawM', 'at'>::Value;

	auto ColladaCompileOp::GetTargets() const -> std::vector<TargetDesc> 
	{
		return _targets;
	}

	auto	ColladaCompileOp::SerializeTarget(unsigned idx) -> std::vector<OperationResult>
	{
		if (idx >= _targets.size()) return {};

		switch (_targets[idx]._type) {
		case Type_Model:			return SerializeSkin(*this, _rootNode.c_str());
		case Type_Skeleton:			return SerializeSkeleton(*this, _rootNode.c_str());
		case Type_RawMat:			return SerializeMaterials(*this, _rootNode.c_str());
		case Type_AnimationSet:		return SerializeAnimations(*this, _rootNode.c_str());
		default:
			Throw(::Exceptions::BasicLabel("Cannot serialize target (%s)", _targets[idx]._name));
		}
	}

	std::vector<::Assets::DependentFileState> ColladaCompileOp::GetDependencies() const
	{
		return _dependencies;
	}

	ColladaCompileOp::ColladaCompileOp() {}
	ColladaCompileOp::~ColladaCompileOp() {}

	std::shared_ptr<::Assets::ICompileOperation> CreateCompileOperation(StringSection<::Assets::ResChar> identifier)
	{
#pragma comment(linker, "/EXPORT:CreateCompileOperation=" __FUNCDNAME__)

		std::shared_ptr<ColladaCompileOp> result = std::make_shared<ColladaCompileOp>();

		auto split = MakeFileNameSplitter(identifier);
		auto filePath = split.AllExceptParameters().AsString();

		result->_dependencies.push_back({ MakeStringSection("colladaimport.cfg"), ::Assets::MainFileSystem::TryGetDesc("colladaimport.cfg")._modificationTime });
		result->_dependencies.push_back({ filePath, ::Assets::MainFileSystem::TryGetDesc(filePath)._modificationTime });

		result->_cfg = ImportConfiguration("colladaimport.cfg");
		result->_fileData = ::Assets::MainFileSystem::OpenMemoryMappedFile(MakeStringSection(filePath), 0, "r", FileShareMode::Read);
		XmlInputStreamFormatter<utf8> formatter(
			MemoryMappedInputStream(result->_fileData.GetData()));
		formatter._allowCharacterData = true;

		result->_name = identifier.AsString();
		result->_rootNode = split.Parameters().AsString();
		result->_doc = std::make_shared<ColladaConversion::DocumentScaffold>();
		result->_doc->Parse(formatter);

		result->_resolveContext = ::ColladaConversion::URIResolveContext(result->_doc);

		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_Model, "Model"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_RawMat, "RawMat"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_Skeleton, "Skeleton"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_AnimationSet, "Animations"});

		return std::move(result);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static uint64_t s_knownAssetTypes[] = { Type_Model, Type_RawMat, Type_Skeleton, Type_AnimationSet };

	class CompilerDesc : public ::Assets::ICompilerDesc
	{
	public:
		const char*			Description() const { return "Compiler and converter for Collada asset files"; }

		virtual unsigned	FileKindCount() const { return 1; }
		virtual FileKind	GetFileKind(unsigned index) const
		{
			assert(index==0);
			return FileKind { MakeIteratorRange(s_knownAssetTypes), R"(.*\.dae)", "Collada XML asset" };
		}

		CompilerDesc() {}
		~CompilerDesc() {}
	};

	std::shared_ptr<::Assets::ICompilerDesc> GetCompilerDesc() 
	{
#pragma comment(linker, "/EXPORT:GetCompilerDesc=" __FUNCDNAME__)
		return std::make_shared<CompilerDesc>();
	}

}

extern "C" 
{

	dll_export ConsoleRig::LibVersionDesc GetVersionInformation()
	{
		return ConsoleRig::GetLibVersionDesc();
	}

	static ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> s_attachRef;

	dll_export void AttachLibrary(ConsoleRig::CrossModule& crossModule)
	{
		ConsoleRig::CrossModule::SetInstance(crossModule);
		s_attachRef = ConsoleRig::GetAttachablePtr<ConsoleRig::GlobalServices>();
		auto versionDesc = ConsoleRig::GetLibVersionDesc();
		Log(Verbose) << "Attached Collada Compiler DLL: {" << versionDesc._versionString << "} -- {" << versionDesc._buildDateString << "}" << std::endl;
	}

	dll_export void DetachLibrary()
	{
		s_attachRef.reset();
		ConsoleRig::CrossModule::ReleaseInstance();
	}

}
