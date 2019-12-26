// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ObjectPlaceholders.h"
#include "VisualisationGeo.h"
#include "../EntityInterface/RetainedEntities.h"
#include "../../FixedFunctionModel/ModelRunTime.h"
#include "../../FixedFunctionModel/PreboundShaders.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Assets/ModelScaffoldInternal.h"
#include "../../RenderCore/Assets/ModelImmutableData.h"
#include "../../RenderCore/Assets/AssetUtils.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Assets/PredefinedCBLayout.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/Buffer.h"
#include "../../RenderCore/Metal/ObjectFactory.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/ResourceList.h"
#include "../../RenderCore/Format.h"
#include "../../RenderCore/Types.h"
#include "../../RenderCore/IDevice.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../Assets/Assets.h"
#include "../../Assets/IFileSystem.h"
#include "../../ConsoleRig/Console.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Streams/FileUtils.h"
#include "../../Utility/StringFormat.h"

namespace ToolsRig
{
    using namespace RenderCore;

    namespace Parameters
    {
        static const auto Transform = ParameterBox::MakeParameterNameHash("Transform");
        static const auto Translation = ParameterBox::MakeParameterNameHash("Translation");
        static const auto Visible = ParameterBox::MakeParameterNameHash("Visible");
        static const auto ShowMarker = ParameterBox::MakeParameterNameHash("ShowMarker");
		static const auto Shape = ParameterBox::MakeParameterNameHash("Shape");
		static const auto Diffuse = ParameterBox::MakeParameterNameHash("Diffuse");
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	class SimpleModel
	{
	public:
		void Render(
			Metal::DeviceContext& devContext,
			Techniques::ParsingContext& parserContext,
			Metal::ConstantBufferPacket localTransform,
			unsigned techniqueIndex, StringSection<::Assets::ResChar> techniqueConfig,
			const ParameterBox& materialParams) const;

		const ::Assets::DepValPtr& GetDependencyValidation() const { return _depVal; }

		SimpleModel(const RenderCore::Assets::RawGeometry& geo, ::Assets::IFileInterface& largeBlocksFile);
		SimpleModel(StringSection<::Assets::ResChar> filename);
		~SimpleModel();
	private:
		IResourcePtr _vb;
		IResourcePtr _ib;
		std::vector<RenderCore::Assets::DrawCallDesc> _drawCalls;
		FixedFunctionModel::SimpleShaderVariationManager _material;
		unsigned _vbStride;
		Format _ibFormat;
		::Assets::DepValPtr _depVal;

		void Build(const RenderCore::Assets::RawGeometry& geo, ::Assets::IFileInterface& largeBlocksFile);
	};

	void SimpleModel::Render(
		Metal::DeviceContext& devContext,
		Techniques::ParsingContext& parserContext,
		Metal::ConstantBufferPacket localTransform,
		unsigned techniqueIndex, StringSection<::Assets::ResChar> techniqueConfig,
		const ParameterBox& materialParams) const
	{
		auto shader = _material.FindVariation(parserContext, techniqueIndex, techniqueConfig);
		if (shader._shader._shaderProgram) {
			auto matParams0 = shader._cbLayout->BuildCBDataAsPkt(materialParams, RenderCore::Techniques::GetDefaultShaderLanguage());
			Float3 col0 = Float3(.66f, 0.2f, 0.f);
			Float3 col1 = Float3(0.44f, 0.6f, 0.1f);
			static float time = 0.f;
			time += 3.14159f / 3.f / 60.f;
			ParameterBox p; p.SetParameter(u("MaterialDiffuse"), LinearInterpolate(col0, col1, 0.5f + 0.5f * XlCos(time)));
			auto matParams1 = shader._cbLayout->BuildCBDataAsPkt(p, RenderCore::Techniques::GetDefaultShaderLanguage());

			if (_drawCalls.size() >= 2) {
				devContext.Bind(*(Metal::Resource*)_ib->QueryInterface(typeid(Metal::Resource).hash_code()), _ibFormat);

				VertexBufferView vbv { _vb, 0 };
				shader._shader.Apply(devContext, parserContext, {vbv});

				ConstantBufferView cbvs0[] = { {localTransform}, {matParams0} };
				shader._shader._boundUniforms->Apply(devContext, 1, UniformsStream{ MakeIteratorRange(cbvs0) });
				devContext.DrawIndexed(_drawCalls[0]._indexCount, _drawCalls[0]._firstIndex, _drawCalls[0]._firstVertex);

				ConstantBufferView cbvs1[] = { {localTransform}, {matParams0} };
				shader._shader._boundUniforms->Apply(devContext, 1, UniformsStream{ MakeIteratorRange(cbvs1) });
				devContext.DrawIndexed(_drawCalls[1]._indexCount, _drawCalls[1]._firstIndex, _drawCalls[1]._firstVertex);
			}
		}
	}

	static std::vector<uint8_t> ReadFromFile(::Assets::IFileInterface& file, size_t size, size_t offset)
	{
		file.Seek(offset);
		std::vector<uint8_t> result(size);
		file.Read(result.data(), size, 1);
		return result;
	}

	SimpleModel::SimpleModel(const RenderCore::Assets::RawGeometry& geo, ::Assets::IFileInterface& largeBlocksFile)
	{
		Build(geo, largeBlocksFile);
	}

	SimpleModel::SimpleModel(StringSection<::Assets::ResChar> filename)
	{
		auto& scaffold = ::Assets::GetAssetComp<RenderCore::Assets::ModelScaffold>(filename);
		if (scaffold.ImmutableData()._geoCount > 0) {
			auto largeBlocksFile = scaffold.OpenLargeBlocks();
			Build(*scaffold.ImmutableData()._geos, *largeBlocksFile);
		}
		_depVal = scaffold.GetDependencyValidation();
	}

	void SimpleModel::Build(const RenderCore::Assets::RawGeometry& geo, ::Assets::IFileInterface& largeBlocksFile)
	{
		// load the vertex buffer & index buffer from the geo input, and copy draw calls data
		auto largeBlocksOffset = largeBlocksFile.TellP();
		auto vbData = ReadFromFile(largeBlocksFile, geo._vb._size, geo._vb._offset + largeBlocksOffset);
		auto ibData = ReadFromFile(largeBlocksFile, geo._ib._size, geo._ib._offset + largeBlocksOffset);
		_vb = RenderCore::Assets::CreateStaticVertexBuffer(MakeIteratorRange(vbData));
		_ib = RenderCore::Assets::CreateStaticIndexBuffer(MakeIteratorRange(vbData));
		_drawCalls.insert(_drawCalls.begin(), geo._drawCalls.cbegin(), geo._drawCalls.cend());
		_vbStride = geo._vb._ia._vertexStride;
		_ibFormat = geo._ib._format;

		// also construct a technique material for the geometry format
		std::vector<InputElementDesc> eles;
		for (const auto&i:geo._vb._ia._elements)
			eles.push_back(InputElementDesc(i._semanticName, i._semanticIndex, i._nativeFormat, 0, i._alignedByteOffset));
		_material = FixedFunctionModel::SimpleShaderVariationManager(
			MakeIteratorRange(eles),
			{ Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants }, ParameterBox());
	}

	SimpleModel::~SimpleModel() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    using EntityInterface::RetainedEntities;
    using EntityInterface::RetainedEntity;

    class VisGeoBox
    {
    public:
        class Desc {};

        RenderCore::IResourcePtr	_cubeVB;
        unsigned					_cubeVBCount;
        unsigned					_cubeVBStride;
		FixedFunctionModel::SimpleShaderVariationManager	_material;
		FixedFunctionModel::SimpleShaderVariationManager	_materialP;
		FixedFunctionModel::SimpleShaderVariationManager	_materialGenSphere;
		FixedFunctionModel::SimpleShaderVariationManager	_materialGenTube;
		FixedFunctionModel::SimpleShaderVariationManager	_materialGenRectangle;

        const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const   { return _depVal; }
        VisGeoBox(const Desc&);
        ~VisGeoBox();
    protected:
        std::shared_ptr<::Assets::DependencyValidation> _depVal;
    };
    
    VisGeoBox::VisGeoBox(const Desc&)
    : _material(
        ToolsRig::Vertex3D_InputLayout, 
        { Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants },
        ParameterBox())
    , _materialP(
        GlobalInputLayouts::P,
        { Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants },
        ParameterBox())
	, _materialGenSphere(
		InputLayout{},
		{ Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants },
		ParameterBox({ std::make_pair(u("SHAPE"), "1") }))
	, _materialGenTube(
		InputLayout{},
		{ Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants },
		ParameterBox({ std::make_pair(u("SHAPE"), "2") }))
	, _materialGenRectangle(
		InputLayout{},
		{ Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants },
		ParameterBox({ std::make_pair(u("SHAPE"), "3") }))
    {
        auto cubeVertices = ToolsRig::BuildCube();
        _cubeVBCount = (unsigned)cubeVertices.size();
        _cubeVBStride = (unsigned)sizeof(decltype(cubeVertices)::value_type);
        _cubeVB = RenderCore::Assets::CreateStaticVertexBuffer(MakeIteratorRange(cubeVertices));
        _depVal = std::make_shared<::Assets::DependencyValidation>();
    }

    VisGeoBox::~VisGeoBox() {}

    static Float4x4 GetTransform(const RetainedEntity& obj)
    {
        auto xform = obj._properties.GetParameter<Float4x4>(Parameters::Transform);
        if (xform.has_value()) return Transpose(xform.value());

        auto transl = obj._properties.GetParameter<Float3>(Parameters::Translation);
        if (transl.has_value()) {
            return AsFloat4x4(transl.value());
        }
        return Identity<Float4x4>();
    }

    static bool GetShowMarker(const RetainedEntity& obj)
    {
        return obj._properties.GetParameter(Parameters::ShowMarker, true);
    }

	class ObjectParams
	{
	public:
		Metal::ConstantBufferPacket _localTransform;
		ParameterBox				_matParams;

		ObjectParams(const RetainedEntity& obj, Techniques::ParsingContext& parserContext, bool directionalTransform = false)
		{
			auto trans = GetTransform(obj);
			if (directionalTransform) {
					// reorient the transform similar to represent the orientation of directional lights
				auto translation = ExtractTranslation(trans);
				trans = MakeObjectToWorld(-Normalize(translation), Float3(0.f, 0.f, 1.f), translation);
			}
			_localTransform = Techniques::MakeLocalTransformPacket(
				trans, ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld));

			// bit of a hack -- copy from the "Diffuse" parameter to the "MaterialDiffuse" shader constant
			auto c = obj._properties.GetParameter(Parameters::Diffuse, ~0u);
			_matParams.SetParameter(u("MaterialDiffuse"), Float3(((c >> 16) & 0xff) / 255.f, ((c >> 8) & 0xff) / 255.f, ((c >> 0) & 0xff) / 255.f));
		}
	};

    static void DrawSphereStandIn(
        Metal::DeviceContext& devContext, Techniques::ParsingContext& parserContext, 
		const ObjectParams& params, unsigned techniqueIndex, StringSection<::Assets::ResChar> technique,
		const VisGeoBox& visBox, const FixedFunctionModel::SimpleShaderVariationManager::Variation& fallbackShader)
    {
		CATCH_ASSETS_BEGIN
			auto& asset = ::Assets::GetAssetDep<SimpleModel>("game/model/simple/spherestandin.dae");
			asset.Render(devContext, parserContext, params._localTransform, techniqueIndex, technique, params._matParams);
			return;
		CATCH_ASSETS_END(parserContext)

		// after an asset exception, we can just render some simple stand-in
		if (fallbackShader._shader._shaderProgram) {
			VertexBufferView vbv { visBox._cubeVB, 0 };
			fallbackShader._shader.Apply(devContext, parserContext, {vbv});
			ConstantBufferView cbvs[] = { {params._localTransform}, {fallbackShader._cbLayout->BuildCBDataAsPkt(params._matParams, RenderCore::Techniques::GetDefaultShaderLanguage())} };
			fallbackShader._shader._boundUniforms->Apply(devContext, 1, {MakeIteratorRange(cbvs)});
			devContext.Draw(visBox._cubeVBCount);
		}
    }

	static void DrawPointerStandIn(
		Metal::DeviceContext& devContext, Techniques::ParsingContext& parserContext,
		const ObjectParams& params, unsigned techniqueIndex, StringSection<::Assets::ResChar> technique,
		const VisGeoBox& visBox, const FixedFunctionModel::SimpleShaderVariationManager::Variation& fallbackShader)
	{
		CATCH_ASSETS_BEGIN
			auto& asset = ::Assets::GetAssetDep<SimpleModel>("game/model/simple/pointerstandin.dae");
		asset.Render(devContext, parserContext, params._localTransform, techniqueIndex, technique, params._matParams);
		return;
		CATCH_ASSETS_END(parserContext)

			// after an asset exception, we can just render some simple stand-in
			if (fallbackShader._shader._shaderProgram) {
				VertexBufferView vbv { visBox._cubeVB, 0 };
				fallbackShader._shader.Apply(devContext, parserContext, {vbv});
				ConstantBufferView cbvs[] = { {params._localTransform}, {fallbackShader._cbLayout->BuildCBDataAsPkt(params._matParams, RenderCore::Techniques::GetDefaultShaderLanguage())} };
				fallbackShader._shader._boundUniforms->Apply(devContext, 1, {MakeIteratorRange(cbvs)});
				devContext.Draw(visBox._cubeVBCount);
			}
	}

	static void DrawGenObject(
		Metal::DeviceContext& devContext, Techniques::ParsingContext& parserContext, 
		const ObjectParams& params,
		const FixedFunctionModel::SimpleShaderVariationManager::Variation& generatorShader, unsigned vertexCount,
		const VisGeoBox& visBox, const FixedFunctionModel::SimpleShaderVariationManager::Variation& fallbackShader)
	{
		if (generatorShader._shader._shaderProgram) {
			generatorShader._shader.Apply(devContext, parserContext, {});
			ConstantBufferView cbvs[] = { {params._localTransform}, {generatorShader._cbLayout->BuildCBDataAsPkt(params._matParams, RenderCore::Techniques::GetDefaultShaderLanguage())} };
			fallbackShader._shader._boundUniforms->Apply(devContext, 1, {MakeIteratorRange(cbvs)});
			devContext.Bind(Topology::TriangleList);
			devContext.Draw(vertexCount);
			return;
		}

		if (fallbackShader._shader._shaderProgram) {
			VertexBufferView vbv { visBox._cubeVB, 0 };
			fallbackShader._shader.Apply(devContext, parserContext, {vbv});
			ConstantBufferView cbvs[] = { {params._localTransform}, {fallbackShader._cbLayout->BuildCBDataAsPkt(params._matParams, RenderCore::Techniques::GetDefaultShaderLanguage())} };
			fallbackShader._shader._boundUniforms->Apply(devContext, 1, {MakeIteratorRange(cbvs)});
			devContext.Draw(visBox._cubeVBCount);
		}
	}

    static void DrawTriMeshMarker(
        Metal::DeviceContext& devContext,
        Techniques::ParsingContext& parserContext,
        const VisGeoBox& visBox,
        const FixedFunctionModel::SimpleShaderVariationManager::Variation& shader, const RetainedEntity& obj,
        EntityInterface::RetainedEntities& objs)
    {
        static auto IndexListHash = ParameterBox::MakeParameterNameHash("IndexList");

        if (!obj._properties.GetParameter(Parameters::Visible, true) || !GetShowMarker(obj)) return;

        // we need an index list with at least 3 indices (to make at least one triangle)
        auto indexListType = obj._properties.GetParameterType(IndexListHash);
        if (indexListType._type == ImpliedTyping::TypeCat::Void || indexListType._arrayCount < 3)
            return;

        auto ibData = std::make_unique<unsigned[]>(indexListType._arrayCount);
        bool success = obj._properties.GetParameter(
            IndexListHash, ibData.get(), 
            ImpliedTyping::TypeDesc(ImpliedTyping::TypeCat::UInt32, indexListType._arrayCount));
        if (!success) return;

        const auto& chld = obj._children;
        if (!chld.size()) return;

        auto vbData = std::make_unique<Float3[]>(chld.size());
        for (size_t c=0; c<chld.size(); ++c) {
            const auto* e = objs.GetEntity(obj._doc, chld[c].second);
            if (e) {
                vbData[c] = ExtractTranslation(GetTransform(*e));
            } else {
                vbData[c] = Zero<Float3>();
            }
        }
        
        devContext.Bind(Techniques::CommonResources()._blendAdditive);
        devContext.Bind(Techniques::CommonResources()._dssReadOnly);
        devContext.Bind(Techniques::CommonResources()._cullDisable);
        
        auto vb = Metal::MakeVertexBuffer(Metal::GetObjectFactory(), MakeIteratorRange(vbData.get(), PtrAdd(vbData.get(), sizeof(Float3)*chld.size())));
        auto ib = Metal::MakeIndexBuffer(Metal::GetObjectFactory(), MakeIteratorRange(ibData.get(), PtrAdd(ibData.get(), sizeof(unsigned)*indexListType._arrayCount)));

		VertexBufferView vbv { &vb, 0 };
		shader._shader.Apply(devContext, parserContext, {vbv});

		ConstantBufferView cbvs[] = {
			{ Techniques::MakeLocalTransformPacket(
                GetTransform(obj),
				ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld)) },
			{ shader._cbLayout->BuildCBDataAsPkt(ParameterBox{}, RenderCore::Techniques::GetDefaultShaderLanguage()) }
        };

		shader._shader._boundUniforms->Apply(devContext, 1, {MakeIteratorRange(cbvs)});

        devContext.Bind(ib, Format::R32_UINT);
        devContext.Bind(Topology::TriangleList);
        devContext.DrawIndexed(indexListType._arrayCount);
    }

    void ObjectPlaceholders::Render(
        Metal::DeviceContext& metalContext, 
        Techniques::ParsingContext& parserContext,
        unsigned techniqueIndex)
    {
        auto& visBox = ConsoleRig::FindCachedBoxDep<VisGeoBox>(VisGeoBox::Desc());

		const auto* baseTechnique = "xleres/techniques/illum.tech";
		if (Tweakable("DrawMarkers", true)) {

			auto fallbackShader = visBox._material.FindVariation(parserContext, techniqueIndex, baseTechnique); 
			for (const auto& a:_cubeAnnotations) {
				auto objects = _objects->FindEntitiesOfType(a._typeId);
				for (const auto&o:objects) {
					if (!o->_properties.GetParameter(Parameters::Visible, true) || !GetShowMarker(*o)) continue;
					DrawSphereStandIn(metalContext, parserContext, ObjectParams(*o, parserContext), techniqueIndex, baseTechnique, visBox, fallbackShader);
				}
			}

			for (const auto& a:_directionalAnnotations) {
				auto objects = _objects->FindEntitiesOfType(a._typeId);
				for (const auto&o : objects) {
					if (!o->_properties.GetParameter(Parameters::Visible, true) || !GetShowMarker(*o)) continue;
					DrawPointerStandIn(metalContext, parserContext, ObjectParams(*o, parserContext, true), techniqueIndex, baseTechnique, visBox, fallbackShader);
				}
			}

			if (!_areaLightAnnotation.empty()) {
				auto sphereShader = visBox._materialGenSphere.FindVariation(parserContext, techniqueIndex, "xleres/ui/objgen/arealight.tech"); 
				auto tubeShader = visBox._materialGenTube.FindVariation(parserContext, techniqueIndex, "xleres/ui/objgen/arealight.tech"); 
				auto rectangleShader = visBox._materialGenRectangle.FindVariation(parserContext, techniqueIndex, "xleres/ui/objgen/arealight.tech");
				for (auto a = _areaLightAnnotation.cbegin(); a != _areaLightAnnotation.cend(); ++a) {
					auto objects = _objects->FindEntitiesOfType(a->_typeId);
					for (const auto&o : objects) {
						if (!o->_properties.GetParameter(Parameters::Visible, true) || !GetShowMarker(*o)) continue;

						auto shape = o->_properties.GetParameter(Parameters::Shape, 0u);
						FixedFunctionModel::SimpleShaderVariationManager::Variation* var;
						unsigned vertexCount = 12 * 12 * 6;	// (must agree with the shader!)
						switch (shape) { 
						case 2: var = &tubeShader; break;
						case 3: var = &rectangleShader; vertexCount = 6*6; break;
						default: var = &sphereShader; break;
						}
						DrawGenObject(
							metalContext, parserContext, 
							ObjectParams(*o, parserContext), *var, vertexCount,
							visBox, fallbackShader);
					}
				}
			}

		}

        auto shaderP = visBox._materialP.FindVariation(parserContext, techniqueIndex, "meshmarker.tech");
        if (shaderP._shader._shaderProgram) {
            for (const auto&a:_triMeshAnnotations) {
                auto objects = _objects->FindEntitiesOfType(a._typeId);
                for (auto o=objects.cbegin(); o!=objects.cend(); ++o) {
                    DrawTriMeshMarker(metalContext, parserContext, visBox, shaderP, **o, *_objects);
                }
            }
        }
    }

    void ObjectPlaceholders::AddAnnotation(EntityInterface::ObjectTypeId typeId, const std::string& geoType)
    {
        Annotation newAnnotation;
        newAnnotation._typeId = typeId;

        if (XlEqStringI(geoType, "TriMeshMarker")) {
            _triMeshAnnotations.push_back(newAnnotation);
        } else if (XlEqStringI(geoType, "AreaLight")) {
			_areaLightAnnotation.push_back(newAnnotation);
		} else if (XlEqStringI(geoType, "PointToOrigin")) {
			_directionalAnnotations.push_back(newAnnotation);
		} else {
            _cubeAnnotations.push_back(newAnnotation);
        }
    }

    class ObjectPlaceholders::IntersectionTester : public SceneEngine::IIntersectionTester
    {
    public:
        Result FirstRayIntersection(
            const SceneEngine::IntersectionTestContext& context,
            std::pair<Float3, Float3> worldSpaceRay) const;

        void FrustumIntersection(
            std::vector<Result>& results,
            const SceneEngine::IntersectionTestContext& context,
            const Float4x4& worldToProjection) const;

        IntersectionTester(std::shared_ptr<ObjectPlaceholders> placeHolders);
        ~IntersectionTester();
    protected:
        std::shared_ptr<ObjectPlaceholders> _placeHolders;
    };

	static SceneEngine::IIntersectionTester::Result AsResult(const Float3& worldSpaceCollision, const RetainedEntity& o)
	{
		SceneEngine::IIntersectionTester::Result result;
		result._type = SceneEngine::IntersectionTestScene::Type::Extra;
		result._worldSpaceCollision = worldSpaceCollision;
		result._distance = 0.f;
		result._objectGuid = std::make_pair(o._doc, o._id);
		result._drawCallIndex = 0;
		result._materialGuid = 0;
		return result;
	}

    auto ObjectPlaceholders::IntersectionTester::FirstRayIntersection(
        const SceneEngine::IntersectionTestContext& context,
        std::pair<Float3, Float3> worldSpaceRay) const -> Result
    {
        using namespace SceneEngine;

		// note -- we always return the first intersection encountered. We should be finding the intersection
		//		closest to the start of the ray!

        for (const auto& a:_placeHolders->_cubeAnnotations) {
            for (const auto& o: _placeHolders->_objects->FindEntitiesOfType(a._typeId))
                if (RayVsAABB(worldSpaceRay, GetTransform(*o), Float3(-1.f, -1.f, -1.f), Float3(1.f, 1.f, 1.f)))
					return AsResult(worldSpaceRay.first, *o);
        }

		for (const auto& a : _placeHolders->_directionalAnnotations) {
			for (const auto& o : _placeHolders->_objects->FindEntitiesOfType(a._typeId))
				if (RayVsAABB(worldSpaceRay, GetTransform(*o), Float3(-1.f, -1.f, -1.f), Float3(1.f, 1.f, 1.f)))
					return AsResult(worldSpaceRay.first, *o);
		}

		for (const auto& a : _placeHolders->_areaLightAnnotation) {
			for (const auto& o : _placeHolders->_objects->FindEntitiesOfType(a._typeId)) {
				const auto shape = o->_properties.GetParameter(Parameters::Shape, 0);
				auto trans = GetTransform(*o); 
				if (shape == 2) {
					// Tube... We can ShortestSegmentBetweenLines to calculate if this ray
					// intersects the tube
					auto axis = ExtractForward(trans);
					auto origin = ExtractTranslation(trans);
					auto tube = std::make_pair(Float3(origin - axis), Float3(origin + axis));
					float mua, mub;
					if (ShortestSegmentBetweenLines(mua, mub, worldSpaceRay, tube)) {
						mua = Clamp(mua, 0.f, 1.f);
						mub = Clamp(mub, 0.f, 1.f);
						float distanceSq = 
							MagnitudeSquared(
									LinearInterpolate(worldSpaceRay.first, worldSpaceRay.second, mua)
								-	LinearInterpolate(tube.first, tube.second, mub));
						float radiusSq = MagnitudeSquared(ExtractRight(trans));
						if (distanceSq <= radiusSq) {
								// (not correct intersection pt)
							return AsResult(LinearInterpolate(worldSpaceRay.first, worldSpaceRay.second, mua), *o);
						}
					}
				} else if (shape == 3)  {
					// Rectangle. We treat it as a box with some small width
					const float boxWidth = 0.01f;		// 1cm
					SetUp(trans, boxWidth * ExtractUp(trans));
					if (RayVsAABB(worldSpaceRay, trans, Float3(-1.f, -1.f, -1.f), Float3(1.f, 1.f, 1.f)))
						return AsResult(worldSpaceRay.first, *o);
				} else {
					// Sphere
					float radiusSq = MagnitudeSquared(ExtractRight(trans));
					float dist;
					if (DistanceToSphereIntersection(
						dist, worldSpaceRay.first - ExtractTranslation(trans), 
						Normalize(worldSpaceRay.second - worldSpaceRay.first), radiusSq))
						return AsResult(worldSpaceRay.first, *o);
				}
			}
		}

        return Result();
    }

    void ObjectPlaceholders::IntersectionTester::FrustumIntersection(
        std::vector<Result>& results,
        const SceneEngine::IntersectionTestContext& context,
        const Float4x4& worldToProjection) const
    {}

    ObjectPlaceholders::IntersectionTester::IntersectionTester(std::shared_ptr<ObjectPlaceholders> placeHolders)
    : _placeHolders(placeHolders)
    {}

    ObjectPlaceholders::IntersectionTester::~IntersectionTester() {}

    std::shared_ptr<SceneEngine::IIntersectionTester> ObjectPlaceholders::CreateIntersectionTester()
    {
        return std::make_shared<IntersectionTester>(shared_from_this());
    }

    ObjectPlaceholders::ObjectPlaceholders(std::shared_ptr<RetainedEntities> objects)
    : _objects(std::move(objects))
    {}

    ObjectPlaceholders::~ObjectPlaceholders() {}

}

