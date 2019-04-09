// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderGenerator.h"
#include "NodeGraph.h"
#include "GUILayerUtil.h"
#include "UITypesBinding.h"
#include "MarshalString.h"
#include "../ToolsRig/MaterialVisualisation.h"
#include "../ToolsRig/MaterialOverridesDelegate.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../ShaderParser/ShaderPatcher.h"
#include "../../ShaderParser/GraphSyntax.h"
#include "../../ShaderParser/NodeGraphProvider.h"
#include "../../ShaderParser/ShaderInstantiation.h"
#include "../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderCore/ShaderService.h"
#include "../../RenderCore/MinimalShaderSource.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/IArtifact.h"
#include "../../Utility/Streams/FileUtils.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Core/Exceptions.h"
#include <sstream>
#include <regex>
#include <msclr/auto_gcroot.h>

#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/ObjectFactory.h"

#pragma warning (disable:4505) // 'ShaderPatcherLayer::StateTypeToString': unreferenced local function has been removed

using namespace System::Runtime::Serialization;

namespace GUILayer 
{
	String^ PreviewSettings::PreviewGeometryToString(PreviewGeometry geo)
	{
		switch (geo) {
		case PreviewGeometry::Chart: return "chart";
		default:
		case PreviewGeometry::Plane2D: return "plane2d";
		case PreviewGeometry::Box: return "box";
		case PreviewGeometry::Sphere: return "sphere";
		case PreviewGeometry::Model: return "model";
		}
	}

	PreviewGeometry PreviewSettings::PreviewGeometryFromString(String^ input)
	{
		if (!String::Compare(input, "chart", true)) return PreviewGeometry::Chart;
		if (!String::Compare(input, "box", true)) return PreviewGeometry::Box;
		if (!String::Compare(input, "sphere", true)) return PreviewGeometry::Sphere;
		if (!String::Compare(input, "model", true)) return PreviewGeometry::Model;
		return PreviewGeometry::Plane2D;
	}

	static ShaderSourceParser::InstantiatedShader
		GeneratePreviewShader(
			NodeGraph^ graph,
			UInt32 previewNodeId, 
			NodeGraphSignature^ signature,
			const std::shared_ptr<GraphLanguage::INodeGraphProvider>& graphProvider,
			PreviewSettings^ settings,
			IEnumerable<KeyValuePair<String^, String^>>^ variableRestrictions)
	{
		ConversionContext context;
        auto nativeGraph = graph->ConvertToNative(context);
		if (previewNodeId != ~0u) {
			nativeGraph.Trim(previewNodeId);
		}

		ShaderSourceParser::InstantiationParameters instantiationParams {};
		if (previewNodeId != ~0u)
			instantiationParams._generateDanglingOutputs = previewNodeId;
		instantiationParams._generateDanglingInputs = true;

		auto mainInstantiation = ShaderSourceParser::InstantiateShader(
			GraphLanguage::INodeGraphProvider::NodeGraph { "preview_graph", nativeGraph, signature->ConvertToNative(context), graphProvider },
			false,
			instantiationParams);

		ShaderSourceParser::PreviewOptions options { ShaderSourceParser::PreviewOptions::Type::Object, std::string{} };
		if (settings) {
			options._type = (settings->Geometry == PreviewGeometry::Chart) ? ShaderSourceParser::PreviewOptions::Type::Chart : ShaderSourceParser::PreviewOptions::Type::Object;
			options._outputToVisualize = String::IsNullOrEmpty(settings->OutputToVisualize) ? std::string() : clix::marshalString<clix::E_UTF8>(settings->OutputToVisualize);
		}

		if (variableRestrictions)
			for each(auto v in variableRestrictions)
				options._variableRestrictions.push_back(
					std::make_pair(
						clix::marshalString<clix::E_UTF8>(v.Key),
						clix::marshalString<clix::E_UTF8>(v.Value)));

		auto structureForPreview = GenerateStructureForPreview(
			"preview_graph", mainInstantiation._entryPointSignature, options);
		mainInstantiation._sourceFragments.insert(mainInstantiation._sourceFragments.begin(), "#include \"xleres/System/Prefix.h\"\n");
		mainInstantiation._sourceFragments.push_back(structureForPreview);
		return mainInstantiation;
	}

	static ShaderSourceParser::InstantiatedShader
		GeneratePreviewShader(
			NodeGraphFile^ nodeGraphFile,
			String^ subGraphName,
			UInt32 previewNodeId, 
			PreviewSettings^ settings,
			IEnumerable<KeyValuePair<String^, String^>>^ variableRestrictions)
	{
		NodeGraphFile::SubGraph^ subGraph = nullptr;
		if (nodeGraphFile->SubGraphs->TryGetValue(subGraphName, subGraph))
			return GUILayer::GeneratePreviewShader(subGraph->Graph, previewNodeId, subGraph->Signature, nodeGraphFile->MakeNodeGraphProvider(), settings, variableRestrictions);
		return {};
	}

	String^ ShaderGeneratorLayer::GeneratePreviewShader(
		NodeGraphFile^ nodeGraphFile,
		String^ subGraphName, UInt32 previewNodeId,
		PreviewSettings^ settings, IEnumerable<KeyValuePair<String^, String^>>^ variableRestrictions)
	{
		auto inst = GUILayer::GeneratePreviewShader(nodeGraphFile, subGraphName, previewNodeId, settings, variableRestrictions);

		std::stringstream str;
		for (const auto&s : inst._sourceFragments)
			str << s;

		return clix::marshalString<clix::E_UTF8>(str.str());
	}

	static RenderCore::CompiledShaderByteCode MakeCompiledShaderByteCode(
		RenderCore::ShaderService::IShaderSource& shaderSource,
		StringSection<> sourceCode, StringSection<> definesTable,
		RenderCore::ShaderStage stage)
	{
		const char* entryPoint = nullptr, *shaderModel = nullptr;
		switch (stage) {
		case RenderCore::ShaderStage::Vertex:
			entryPoint = "vs_main"; shaderModel = VS_DefShaderModel;
			break;
		case RenderCore::ShaderStage::Pixel:
			entryPoint = "ps_main"; shaderModel = PS_DefShaderModel;
			break;
		default:
			break;
		}
		if (!entryPoint || !shaderModel) return {};

		auto future = shaderSource.CompileFromMemory(sourceCode, entryPoint, shaderModel, definesTable);
		auto state = future->GetAssetState();
		auto artifacts = future->GetArtifacts();
		assert(!artifacts.empty());
		if (state == ::Assets::AssetState::Invalid)
			Throw(::Assets::Exceptions::InvalidAsset(entryPoint, artifacts[0].second->GetDependencyValidation(), ::Assets::GetErrorMessage(*future)));

		return RenderCore::CompiledShaderByteCode{
			artifacts[0].second->GetBlob(), artifacts[0].second->GetDependencyValidation(), artifacts[0].second->GetRequestParameters()};
	}

	class TechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
	{
	public:
		virtual RenderCore::Metal::ShaderProgram* GetShader(
			RenderCore::Techniques::ParsingContext& context,
			StringSection<::Assets::ResChar> techniqueCfgFile,
			const ParameterBox* shaderSelectors[],
			unsigned techniqueIndex)
		{
			std::string definesTable, shaderCode;

			{
				std::vector<std::pair<const utf8*, std::string>> defines;
				for (unsigned c=0; c<RenderCore::Techniques::ShaderSelectors::Source::Max; ++c)
					BuildStringTable(defines, *shaderSelectors[c]);
				std::stringstream str;
				for (auto&d:defines) {
					str << d.first;
					if (!d.second.empty())
						str << "=" << d.second;
					str << ";";
				}
				if (_pretransformedFlag) str << "GEO_PRETRANSFORMED=1;";

				// Many node graphs need the world position as input. Ideally we would be able to detect
				// this directly from the graph structure -- but until then, we'll just have to force it on
				str << "OUTPUT_WORLD_POSITION=1;";

				definesTable = str.str();
			}
			{
				std::stringstream str;
				for (const auto&s : _previewShader._sourceFragments)
					str << s;
				shaderCode = str.str();
			}

			auto vsCode = MakeCompiledShaderByteCode(*_shaderSource, MakeStringSection(shaderCode), MakeStringSection(definesTable), RenderCore::ShaderStage::Vertex);
			auto psCode = MakeCompiledShaderByteCode(*_shaderSource, MakeStringSection(shaderCode), MakeStringSection(definesTable), RenderCore::ShaderStage::Pixel);
			if (vsCode.GetStage() != RenderCore::ShaderStage::Vertex || psCode.GetStage() != RenderCore::ShaderStage::Pixel) return nullptr;

			static RenderCore::Metal::ShaderProgram result;
			result = RenderCore::Metal::ShaderProgram { RenderCore::Metal::GetObjectFactory(), vsCode, psCode };
			return &result;
		}

		TechniqueDelegate(
			ShaderSourceParser::InstantiatedShader&& previewShader,
			bool pretransformedFlag)
		: _previewShader(std::move(previewShader))
		, _pretransformedFlag(pretransformedFlag)
		{
			_shaderSource = std::make_shared<RenderCore::MinimalShaderSource>(
				RenderCore::Metal::CreateLowLevelShaderCompiler(RenderCore::Assets::Services::GetDevice()));
		}
				
	private:
		ShaderSourceParser::InstantiatedShader _previewShader;
		std::shared_ptr<RenderCore::ShaderService::IShaderSource> _shaderSource;
		bool _pretransformedFlag;
	};

	GUILayer::TechniqueDelegateWrapper^ ShaderGeneratorLayer::MakeTechniqueDelegate(
		NodeGraphMetaData^ doc, 
		NodeGraphPreviewConfiguration^ nodeGraphFile)
	{
		auto previewShader = GUILayer::GeneratePreviewShader(
			nodeGraphFile->_nodeGraph,
			nodeGraphFile->_subGraphName,
			nodeGraphFile->_previewNodeId,
			nodeGraphFile->_settings,
			nodeGraphFile->_variableRestrictions);

		bool pretransformed = false;
		return gcnew TechniqueDelegateWrapper(new TechniqueDelegate(std::move(previewShader), pretransformed));
	}

	GUILayer::TechniqueDelegateWrapper^ ShaderGeneratorLayer::MakeTechniqueDelegate(
		NodeGraphFile^ nodeGraph,
		String^ subGraphName,
		MessageRelayWrapper^ logMessages)
	{
		auto nativeSubgraph = clix::marshalString<clix::E_UTF8>(subGraphName);
		return gcnew TechniqueDelegateWrapper(ToolsRig::MakeNodeGraphPreviewDelegate(nodeGraph->MakeNodeGraphProvider(), nativeSubgraph, logMessages->_native.GetNativePtr()).release());
	}

	static std::shared_ptr<RenderCore::Assets::RawMaterial> RawMaterialFromRestriction(
		IEnumerable<KeyValuePair<String^, String^>>^ restrictions)
	{
		auto previewMat = std::make_shared<RenderCore::Assets::RawMaterial>();

		auto e = restrictions->GetEnumerator();
		while (e->MoveNext())
		{
			auto kv = e->Current;

			// We sometimes encode special formatting in these values (such as referencing a function).
			// In those cases, the will always be a colon present. So we can check for that to see if this
			// value is some just a constant, or something more complicated
			if (kv.Value->IndexOf(':') == -1)
			{
				auto nativeName = clix::marshalString<clix::E_UTF8>(kv.Key);
				previewMat->_constants.SetParameter(
					MakeStringSection(nativeName).Cast<utf8>(),
					MakeStringSection(clix::marshalString<clix::E_UTF8>(kv.Value)));
			}
		}

		return previewMat;
	}

	MaterialDelegateWrapper^ ShaderGeneratorLayer::MakeMaterialDelegate(
		NodeGraphPreviewConfiguration^ nodeGraphFile,
		RawMaterial^ materialOverrides)
	{
		auto previewShader = GUILayer::GeneratePreviewShader(
			nodeGraphFile->_nodeGraph,
			nodeGraphFile->_subGraphName,
			nodeGraphFile->_previewNodeId,
			nodeGraphFile->_settings,
			nodeGraphFile->_variableRestrictions);

		for (const auto&cb : previewShader._constantBuffers) {
			if (XlEqString(cb._name, "BasicMaterialConstants")) {
				auto previewMat = RawMaterialFromRestriction(nodeGraphFile->_variableRestrictions);
				materialOverrides->GetUnderlyingPtr()->MergeInto(*previewMat);

				auto nativeDelegate = ToolsRig::MakeMaterialMergeDelegate(previewMat, cb._layout);
				return gcnew MaterialDelegateWrapper(std::move(nativeDelegate));
			}
		}
		return nullptr;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename Type>
		static void SaveToXML(System::IO::Stream^ stream, Type^ obj)
	{
		DataContractSerializer^ serializer = nullptr;
        System::Xml::XmlWriterSettings^ settings = nullptr;
        System::Xml::XmlWriter^ writer = nullptr;

        try
        {
            serializer = gcnew DataContractSerializer(Type::typeid);
            settings = gcnew System::Xml::XmlWriterSettings();
            settings->Indent = true;
            settings->IndentChars = "\t";
            settings->Encoding = System::Text::Encoding::UTF8;

            writer = System::Xml::XmlWriter::Create(stream, settings);
            serializer->WriteObject(writer, obj);
        }
        finally
        {
            delete writer;
            delete serializer;
            delete settings;
        }
	}

    static bool IsNodeGraphChunk(const ::Assets::TextChunk<char>& chunk)        { return XlEqString(chunk._type, "NodeGraph"); }
    static bool IsNodeGraphMetaDataChunk(const ::Assets::TextChunk<char>& chunk) { return XlEqString(chunk._type, "NodeGraphMetaData"); }

    static array<Byte>^ AsManagedArray(const ::Assets::TextChunk<char>* chunk)
    {
        // marshall the native string into a managed array, and from there into
        // a stream... We need to strip off leading whitespace, however (usually
        // there is a leading newline, which confuses the xml loader
        auto begin = chunk->_content.begin();
        while (begin != chunk->_content.end() && *begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')
            ++begin;

        size_t contentSize = size_t(chunk->_content.end()) - size_t(begin);
        array<Byte>^ managedArray = gcnew array<Byte>((int)contentSize);
        {
            cli::pin_ptr<Byte> pinned = &managedArray[managedArray->GetLowerBound(0)];
            XlCopyMemory(pinned, begin, contentSize);
        }
        return managedArray;
    }

    static NodeGraphMetaData^ LoadMetaData(System::IO::Stream^ stream)
    {
        auto serializer = gcnew DataContractSerializer(NodeGraphMetaData::typeid);
        try
        {
            auto o = serializer->ReadObject(stream);
            return dynamic_cast<NodeGraphMetaData^>(o);
        }
        finally { delete serializer; }
    }

    void ShaderGeneratorLayer::LoadNodeGraphFile(String^ filename, [Out] NodeGraphFile^% nodeGraph, [Out] NodeGraphMetaData^% context)
    {
        // Load from a graph model compound text file (that may contain other text chunks)
        // We're going to use a combination of native and managed stuff -- so it's easier
        // if the caller just passes in a filename
        auto nativeFilename = clix::marshalString<clix::E_UTF8>(filename);
		size_t size = 0;
        auto block = ::Assets::TryLoadFileAsMemoryBlock(MakeStringSection(nativeFilename), &size);
        if (!block.get() || !size)
            throw gcnew System::Exception(System::String::Format("Missing or empty file {0}", filename));

        auto chunks = ::Assets::ReadCompoundTextDocument(
            MakeStringSection((const char*)block.get(), (const char*)PtrAdd(block.get(), size)));

		// Attempt to read the entire file in "graph syntax"
		auto nativeGraph = ::GraphLanguage::ParseGraphSyntax(MakeStringSection((const char*)block.get(), (const char*)PtrAdd(block.get(), size)));
		nodeGraph = NodeGraphFile::ConvertFromNative(nativeGraph, ::Assets::DefaultDirectorySearchRules(MakeStringSection(nativeFilename)));

            // now load the context chunk (if it exists)
		auto contextChunk = std::find_if(chunks.cbegin(), chunks.cend(), IsNodeGraphMetaDataChunk);
        if (contextChunk != chunks.end()) {
            array<Byte>^ managedArray = nullptr;
            System::IO::MemoryStream^ memStream = nullptr;
            try
            {
                managedArray = AsManagedArray(AsPointer(contextChunk));
                memStream = gcnew System::IO::MemoryStream(managedArray);
                context = LoadMetaData(memStream);
            }
            finally
            {
                delete memStream;
                delete managedArray;
            }
        } else {
            context = gcnew NodeGraphMetaData();
        }
    }

	static void WriteTechniqueConfigSection(
		System::IO::StreamWriter^ sw,
		String^ section, String^ entryPoint, 
		Dictionary<String^, String^>^ shaderParams)
	{
		sw->Write("~"); sw->Write(section); sw->WriteLine();

        // Sometimes we can attach restrictions or defaults to shader parameters -- 
        //      take care of those here...
        if (shaderParams->Count > 0) {
            sw->Write("    ~Parameters"); sw->WriteLine();
            sw->Write("        ~Material"); sw->WriteLine();
            for each(auto i in shaderParams) {
                sw->Write("            ");
                sw->Write(i.Key);
                if (i.Value && i.Value->Length > 0) {
                    sw->Write("=");
                    sw->Write(i.Value);
                }
                sw->WriteLine();
            }
        }

        sw->Write("    PixelShader=<.>:" + entryPoint); sw->WriteLine();
	}

    void ShaderGeneratorLayer::Serialize(System::IO::Stream^ stream, String^ name, NodeGraphFile^ nodeGraphFile, NodeGraphMetaData^ context)
    {
        // We want to write this node graph to the given stream.
        // But we're going to write a compound text document, which will include
        // the graph in multiple forms.
        // One form will be the XML serialized nodes. Another form will be the
        // HLSL output.

        // note --  shader compiler doesn't support the UTF8 BOM properly.
        //          We we have to use an ascii mode
        System::IO::StreamWriter^ sw = gcnew System::IO::StreamWriter(stream, System::Text::Encoding::ASCII);
        
        sw->Write("// CompoundDocument:1"); sw->WriteLine();

		{
			std::stringstream str;
			GraphLanguage::Serialize(str, nodeGraphFile->ConvertToNative());
			sw->Write(clix::marshalString<clix::E_UTF8>(str.str()));
			sw->Flush();
		}

        // embed the node graph context
        sw->Write("/* <<Chunk:NodeGraphMetaData:" + name + ">>--("); sw->WriteLine();
        sw->Flush();
        SaveToXML(stream, context); sw->WriteLine();
        sw->Write(")-- */"); sw->WriteLine();
        sw->Flush();

        // also embedded a technique config, if requested
		NodeGraphFile::SubGraph^ subGraphForTechConfig = nullptr;
		if (nodeGraphFile->SubGraphs->TryGetValue("main", subGraphForTechConfig)) {
			ConversionContext convContext;
			auto nativeGraph = subGraphForTechConfig->Graph->ConvertToNative(convContext);
			auto interf = subGraphForTechConfig->Signature->ConvertToNative(convContext);
				
			if (context->HasTechniqueConfig) {
				sw->WriteLine();

				try {
					auto str = ShaderSourceParser::GenerateStructureForTechniqueConfig(interf, "main");
					sw->Write(clix::marshalString<clix::E_UTF8>(str));
				} catch (const std::exception& e) {
					sw->Write("Exception while generating technique entry points: " + clix::marshalString<clix::E_UTF8>(e.what()));
				} catch (...) {
					sw->Write("Unknown exception while generating technique entry points");
				}

				sw->WriteLine();
				sw->Write("/* <<Chunk:TechniqueConfig:main>>--("); sw->WriteLine();
				sw->Write("~Inherit; xleres/techniques/illum.tech"); sw->WriteLine();

				WriteTechniqueConfigSection(sw, "Forward", "forward_main", context->ShaderParameters);
				WriteTechniqueConfigSection(sw, "Deferred", "deferred_main", context->ShaderParameters);
				WriteTechniqueConfigSection(sw, "OrderIndependentTransparency", "oi_main", context->ShaderParameters);
				WriteTechniqueConfigSection(sw, "StochasticTransparency", "stochastic_main", context->ShaderParameters);
				WriteTechniqueConfigSection(sw, "DepthOnly", "depthonly_main", context->ShaderParameters);
                
				sw->Write(")--*/"); sw->WriteLine();
				sw->WriteLine();
				sw->Flush();
			}
		}
    }



}
