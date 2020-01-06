// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GUILayerUtil.h"
#include "MarshalString.h"
#include "ExportedNativeTypes.h"
#include "../ToolsRig/MaterialVisualisation.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../ConsoleRig/IProgress.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/FileSystemMonitor.h"
#include <msclr/auto_gcroot.h>

namespace GUILayer
{
    System::UInt64 Utils::HashID(System::String^ string)
    {
        return Hash64(clix::marshalString<clix::E_UTF8>(string));
    }

	System::String^ Utils::MakeAssetName(System::String^ value)
	{
		auto nativeName = clix::marshalString<clix::E_UTF8>(value);
        ::Assets::ResolvedAssetFile resName;
        ::Assets::MakeAssetName(resName, nativeName.c_str());
		return clix::marshalString<clix::E_UTF8>(resName._fn);
	}

	static System::Collections::Generic::IEnumerable<Utils::AssetExtension^>^ ToManaged(
		IteratorRange<const std::pair<std::string, std::string>*> range)
	{
		auto result = gcnew System::Collections::Generic::List<Utils::AssetExtension^>();
		for (const auto&i:range) {
			auto ext = gcnew Utils::AssetExtension();
			ext->Extension = clix::marshalString<clix::E_UTF8>(i.first);
			ext->Description = clix::marshalString<clix::E_UTF8>(i.second);
			result->Add(ext);
		}
		return result;
	}

	System::Collections::Generic::IEnumerable<Utils::AssetExtension^>^ Utils::GetModelExtensions()
	{
		auto exts = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers().GetExtensionsForType(
			RenderCore::Assets::ModelScaffold::CompileProcessType);
		return ToManaged(MakeIteratorRange(exts));
	}

	System::Collections::Generic::IEnumerable<Utils::AssetExtension^>^ Utils::GetAnimationSetExtensions()
	{
		auto exts = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers().GetExtensionsForType(
			RenderCore::Assets::AnimationSetScaffold::CompileProcessType);
		return ToManaged(MakeIteratorRange(exts));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class MessageRelayWrapper_Helper : public Utility::OnChangeCallback
	{
	public:
		virtual void    OnChange()
		{
			if (_managed.get() && _managed->OnChangeEvent)
				_managed->OnChangeEvent(_managed.get(), nullptr);
		}

		msclr::auto_gcroot<MessageRelayWrapper^> _managed;
		MessageRelayWrapper_Helper(MessageRelayWrapper^ managed) : _managed(managed) {}
	};

	System::String^ MessageRelayWrapper::Messages::get()
	{
		auto nativeMsgs = _native->GetMessages();
		return clix::marshalString<clix::E_UTF8>(nativeMsgs);
	}

	MessageRelayWrapper::MessageRelayWrapper(const std::shared_ptr<ToolsRig::MessageRelay>& techniqueDelegate)
	{
		_native = techniqueDelegate;
		_callbackId = _native->AddCallback(
			std::shared_ptr<Utility::OnChangeCallback>(new MessageRelayWrapper_Helper(this)));
	}

	MessageRelayWrapper::MessageRelayWrapper(ToolsRig::MessageRelay* techniqueDelegate)
	: MessageRelayWrapper(std::shared_ptr<ToolsRig::MessageRelay>(techniqueDelegate))
	{
	}

	MessageRelayWrapper::MessageRelayWrapper()
	: MessageRelayWrapper(std::make_shared<ToolsRig::MessageRelay>())
	{}

    MessageRelayWrapper::~MessageRelayWrapper()
	{
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    TechniqueContextWrapper::TechniqueContextWrapper(const std::shared_ptr<RenderCore::Techniques::TechniqueContext>& techniqueContext)
	: _techniqueContext(techniqueContext)
    {}

    TechniqueContextWrapper::~TechniqueContextWrapper()
    {
        _techniqueContext.reset();
    }

	TechniqueDelegateWrapper::TechniqueDelegateWrapper(const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate_Old>& techniqueDelegate)
	: _techniqueDelegate(techniqueDelegate)
	{}

	TechniqueDelegateWrapper::TechniqueDelegateWrapper(RenderCore::Techniques::ITechniqueDelegate_Old* techniqueDelegate)
	: _techniqueDelegate(techniqueDelegate)
	{
	}

    TechniqueDelegateWrapper::~TechniqueDelegateWrapper()
	{
		_techniqueDelegate.reset();
	}

	MaterialDelegateWrapper::MaterialDelegateWrapper(const std::shared_ptr<RenderCore::Techniques::IMaterialDelegate>& materialDelegate)
		: _materialDelegate(materialDelegate)
	{}

	MaterialDelegateWrapper::MaterialDelegateWrapper(RenderCore::Techniques::IMaterialDelegate* materialDelegate)
	: _materialDelegate(materialDelegate)
	{
	}

	MaterialDelegateWrapper::~MaterialDelegateWrapper()
	{
		_materialDelegate.reset();
	}

	IntersectionTestSceneWrapper::IntersectionTestSceneWrapper(const std::shared_ptr<SceneEngine::IntersectionTestScene>& scene)
	{
		_scene = std::move(scene);
	}

    IntersectionTestSceneWrapper::IntersectionTestSceneWrapper(
        const std::shared_ptr<SceneEngine::TerrainManager>& terrainManager,
        const std::shared_ptr<SceneEngine::PlacementCellSet>& placements,
        const std::shared_ptr<SceneEngine::PlacementsEditor>& placementsEditor,
        std::initializer_list<std::shared_ptr<SceneEngine::IIntersectionTester>> extraTesters)
    {
		_scene = std::make_shared<SceneEngine::IntersectionTestScene>(
            terrainManager, 
            placements, placementsEditor, extraTesters);
    }

    IntersectionTestSceneWrapper::~IntersectionTestSceneWrapper()
    {
        _scene.reset();
    }

    IntersectionTestSceneWrapper::!IntersectionTestSceneWrapper()
    {
        System::Diagnostics::Debug::Assert(false, "IntersectionTestSceneWrapper finalizer used");
    }

	SceneEngine::IntersectionTestScene& IntersectionTestSceneWrapper::GetNative()
	{
		return *_scene.get();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    PlacementsEditorWrapper::PlacementsEditorWrapper(
		std::shared_ptr<SceneEngine::PlacementsEditor> scene)
	{
		_editor = std::move(scene);
	}

    PlacementsEditorWrapper::~PlacementsEditorWrapper()
    {
        _editor.reset();
    }

    PlacementsEditorWrapper::!PlacementsEditorWrapper()
    {
        System::Diagnostics::Debug::Assert(false, "PlacementsEditorWrapper finalizer used");
    }

	SceneEngine::PlacementsEditor& PlacementsEditorWrapper::GetNative()
	{
		return *_editor.get();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    PlacementsRendererWrapper::PlacementsRendererWrapper(
		std::shared_ptr<SceneEngine::PlacementsRenderer> scene)
	{
		_renderer = std::move(scene);
	}

    PlacementsRendererWrapper::~PlacementsRendererWrapper()
    {
        _renderer.reset();
    }

    PlacementsRendererWrapper::!PlacementsRendererWrapper()
    {
        System::Diagnostics::Debug::Assert(false, "PlacementsEditorWrapper finalizer used");
    }

	SceneEngine::PlacementsRenderer& PlacementsRendererWrapper::GetNative()
	{
		return *_renderer.get();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class StepAdapter : public ConsoleRig::IStep
    {
    public:
        void SetProgress(unsigned progress);
        void Advance();
        bool IsCancelled() const;

        StepAdapter(GUILayer::IStep^ adapted);
        ~StepAdapter();
    protected:
        msclr::gcroot<GUILayer::IStep^> _adapted;
    };

    void StepAdapter::SetProgress(unsigned progress)
    {
        _adapted->SetProgress(progress);
    }

    void StepAdapter::Advance()
    {
        _adapted->Advance();
    }

    bool StepAdapter::IsCancelled() const
    {
        return _adapted->IsCancelled();
    }

    StepAdapter::StepAdapter(GUILayer::IStep^ adapted)
    : _adapted(adapted) {}

    StepAdapter::~StepAdapter() 
    {
        _adapted->EndStep();
    }

    class ProgressAdapter : public ConsoleRig::IProgress
    {
    public:
        virtual std::shared_ptr<ConsoleRig::IStep> BeginStep(const char name[], unsigned progressMax, bool cancellable);

        ProgressAdapter(GUILayer::IProgress^ adapted);
        ~ProgressAdapter();
    protected:
        msclr::gcroot<GUILayer::IProgress^> _adapted;
    };

    std::shared_ptr<ConsoleRig::IStep> ProgressAdapter::BeginStep(const char name[], unsigned progressMax, bool cancellable)
    {
        auto n = clix::marshalString<clix::E_UTF8>(name);
        return std::shared_ptr<StepAdapter>(new StepAdapter(_adapted->BeginStep(n, progressMax, cancellable)));
    }

    ProgressAdapter::ProgressAdapter(GUILayer::IProgress^ adapted)
    : _adapted(adapted) {}

    ProgressAdapter::~ProgressAdapter() {}


    auto IProgress::CreateNative(IProgress^ managed) -> ProgressPtr
    {
        return ProgressPtr(new ProgressAdapter(managed), IProgress::DeleteNative);
    }

    void IProgress::DeleteNative(ConsoleRig::IProgress* native)
    {
        delete native;
    }
}

