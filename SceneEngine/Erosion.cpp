// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Erosion.h"
#include "ShallowWater.h"
#include "Ocean.h"
#include "DeepOceanSim.h"
#include "SceneEngineUtils.h"
#include "SurfaceHeightsProvider.h"
#include "MetalStubs.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Buffer.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../Assets/Assets.h"
#include "../Utility/BitUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Meta/ClassAccessorsImpl.h"

namespace SceneEngine
{
    using namespace RenderCore;

    class ErosionSimulation::Pimpl
    {
    public:
        using ResLocator = intrusive_ptr<BufferUploads::ResourceLocator>;
        using UAV = Metal::UnorderedAccessView;
        using SRV = Metal::ShaderResourceView;

        ResLocator _hardMaterials, _softMaterials, _softMaterialsCopy;

        UAV _hardMaterialsUAV;
        UAV _softMaterialsUAV;
        SRV _softMaterialsCopySRV;
        SRV _hardMaterialsSRV;
        SRV _softMaterialsSRV;

        std::unique_ptr<ShallowWaterSim> _waterSim;
        std::unique_ptr<ISurfaceHeightsProvider> _surfaceHeightsProvider;
        unsigned _bufferId;

        UInt2 _simSize;
        float _worldSpaceSpacing;
        std::vector<Int2> _pendingNewElements;

        Pimpl(UInt2 dimensions, float physicalSpacing);
    };

    namespace Internal
    {
        class ErosionSurfaceHeightsProvider : public ISurfaceHeightsProvider
        {
        public:
            virtual SRV         GetSRV();
            virtual Addressing  GetAddress(Float2 minCoordWorld, Float2 maxCoordWorld);
            virtual bool        IsFloatFormat() const;

            ErosionSurfaceHeightsProvider(SRV srv, UInt2 cacheDims, float worldSpaceSpacing);
            ~ErosionSurfaceHeightsProvider();
        private:
            UInt2 _cacheDims;
            float _worldSpaceSpacing;
            SRV _srv;
        };

        ErosionSurfaceHeightsProvider::ErosionSurfaceHeightsProvider(SRV srv, UInt2 cacheDims, float worldSpaceSpacing)
        : _cacheDims(cacheDims), _worldSpaceSpacing(worldSpaceSpacing), _srv(std::move(srv))
        {}

        ErosionSurfaceHeightsProvider::~ErosionSurfaceHeightsProvider() {}

        auto ErosionSurfaceHeightsProvider::GetSRV() -> SRV { return _srv; }
        auto ErosionSurfaceHeightsProvider::GetAddress(Float2 minCoordWorld, Float2 maxCoordWorld) -> Addressing
        {
            Addressing result;
            result._baseCoordinate = Int3(0,0,0);
            result._heightScale = 1.f;
            result._heightOffset = 0.f;

            if (    minCoordWorld[0] >= 0.f &&  unsigned(maxCoordWorld[0] / _worldSpaceSpacing) <= _cacheDims[0]
                &&  minCoordWorld[1] >= 0.f &&  unsigned(maxCoordWorld[1] / _worldSpaceSpacing) <= _cacheDims[1]) {

                result._minCoordOffset[0] = (int)(minCoordWorld[0] / _worldSpaceSpacing);
                result._minCoordOffset[1] = (int)(minCoordWorld[1] / _worldSpaceSpacing);

                result._maxCoordOffset[0] = (int)(maxCoordWorld[0] / _worldSpaceSpacing);
                result._maxCoordOffset[1] = (int)(maxCoordWorld[1] / _worldSpaceSpacing);

                result._valid = true;
            } else {
                result._minCoordOffset = result._maxCoordOffset = UInt2(0,0);
                result._valid = false;
            }

            return result;
        }

        bool ErosionSurfaceHeightsProvider::IsFloatFormat() const { return true; }
    }

    static const unsigned ErosionWaterTileDimension = 256;
    static const unsigned ErosionWaterTileScale = 1;            // scale relative to the terrain surface resolution. Eg, 4 means each terrain grid becomes 4x4 grid elements in the water simulation

    ErosionSimulation::Pimpl::Pimpl(UInt2 dimensions, float worldSpaceSpacing)
    {
            //  We're going to do an erosion simulation over the given points
            //  First we need to allocate the buffers we need:
            //      * GPU heights cache
            //      * ShallowWaterSim object
            //      * ShallowWaterSim::ActiveElement elements

        /////////////////////////////////////////////////////////////////////////////////////

        auto& bufferUploads = GetBufferUploads();
        auto desc = CreateDesc(
            BindFlag::UnorderedAccess | BindFlag::ShaderResource,
            0, GPUAccess::Read|GPUAccess::Write,
            TextureDesc::Plain2D(dimensions[0], dimensions[1], Format::R32_FLOAT),
            "ErosionSim");
        auto hardMaterials = bufferUploads.Transaction_Immediate(desc);
        auto softMaterials = bufferUploads.Transaction_Immediate(desc);
        auto softMaterialsCopy = bufferUploads.Transaction_Immediate(desc);

        UAV hardMaterialsUAV(hardMaterials->GetUnderlying());
        UAV softMaterialsUAV(softMaterials->GetUnderlying());
        SRV hardMaterialsSRV(hardMaterials->GetUnderlying());
        SRV softMaterialsSRV(softMaterials->GetUnderlying());
        SRV softMaterialsCopySRV(softMaterialsCopy->GetUnderlying());

        /////////////////////////////////////////////////////////////////////////////////////

        UInt2 waterDims = dimensions * ErosionWaterTileScale;
        UInt2 waterGrids(
            CeilToMultiple(dimensions[0] * ErosionWaterTileScale, ErosionWaterTileDimension) / ErosionWaterTileDimension,
            CeilToMultiple(dimensions[1] * ErosionWaterTileScale, ErosionWaterTileDimension) / ErosionWaterTileDimension);

        static bool usePipeModel = true;
        auto newShallowWater = std::make_unique<ShallowWaterSim>(
            ShallowWaterSim::Desc(
                unsigned(ErosionWaterTileDimension), waterGrids[0] * waterGrids[1], usePipeModel, 
                true, false, true));
        
        for (unsigned y=0; y<waterGrids[1]; ++y)
            for (unsigned x=0; x<waterGrids[0]; ++x)
                _pendingNewElements.push_back(Int2(x, y));

        auto surfaceHeightsProvider = std::make_unique<Internal::ErosionSurfaceHeightsProvider>(
            hardMaterialsSRV, dimensions, worldSpaceSpacing);
        
        /////////////////////////////////////////////////////////////////////////////////////
        
        _surfaceHeightsProvider = std::move(surfaceHeightsProvider);
        _waterSim = std::move(newShallowWater);
        _bufferId = 0;
        _hardMaterials = std::move(hardMaterials);
        _softMaterials = std::move(softMaterials);
        _softMaterialsCopy = std::move(softMaterialsCopy);
        _hardMaterialsUAV = std::move(hardMaterialsUAV);
        _softMaterialsUAV = std::move(softMaterialsUAV);
        _hardMaterialsSRV = std::move(hardMaterialsSRV);
        _softMaterialsSRV = std::move(softMaterialsSRV);
        _softMaterialsCopySRV = std::move(softMaterialsCopySRV);
        _simSize = dimensions;
        _worldSpaceSpacing = worldSpaceSpacing;
    }

    void ErosionSimulation::InitHeights(
        RenderCore::Metal::DeviceContext& metalContext,
        Metal::ShaderResourceView& input, UInt2 topLeft, UInt2 bottomRight)
    {
        metalContext.ClearFloat(_pimpl->_softMaterialsUAV, { 0.f, 0.f, 0.f, 0.f });
        metalContext.ClearFloat(_pimpl->_hardMaterialsUAV, { 0.f, 0.f, 0.f, 0.f });

            // copy
        auto inputRes = input.GetResource();
        Metal::CopyPartial(
            metalContext,
            Metal::CopyPartial_Dest(Metal::AsResource(*_pimpl->_hardMaterials->GetUnderlying())),
			Metal::CopyPartial_Src(Metal::AsResource(*inputRes), {}, 
				{topLeft[0], topLeft[1], 0u}, 
				{bottomRight[0], bottomRight[1], 1u}));
    }

    void ErosionSimulation::GetHeights(
        RenderCore::Metal::DeviceContext& metalContext,
        RenderCore::Metal::UnorderedAccessView& dest,
        UInt2 topLeft, UInt2 bottomRight)
    {
        auto destRes = dest.GetResource();
        Metal::CopyPartial(
            metalContext,
            Metal::CopyPartial_Dest(Metal::AsResource(*destRes)),
			Metal::CopyPartial_Src(Metal::AsResource(*_pimpl->_hardMaterials->GetUnderlying()), {}, 
				{},
				{bottomRight[0] - topLeft[0], bottomRight[1] - topLeft[1], 1u}));
    }

    void    ErosionSimulation::Tick(
        RenderCore::Metal::DeviceContext& metalContext,
        const Settings& params)
    {
            //      Update the shallow water simulation

        ShallowWaterSim::SimSettings settings;
        settings._rainQuantityPerFrame = params._rainQuantityPerFrame;
        settings._evaporationConstant = params._evaporationConstant;
        settings._pressureConstant = params._pressureConstant;

        ShallowWaterSim::SimulationContext simContext(
            metalContext, DeepOceanSimSettings(),
            _pimpl->_worldSpaceSpacing * ErosionWaterTileDimension / ErosionWaterTileScale, Zero<Float2>(),
            _pimpl->_surfaceHeightsProvider.get(), nullptr,
            ShallowWaterSim::BorderMode::Surface);

        if (!_pimpl->_pendingNewElements.empty()) {
            _pimpl->_waterSim->BeginElements(
                simContext,
                AsPointer(_pimpl->_pendingNewElements.cbegin()), AsPointer(_pimpl->_pendingNewElements.cend()));
            _pimpl->_pendingNewElements.clear();
        }

        _pimpl->_waterSim->ExecuteInternalSimulation(
            simContext, settings, _pimpl->_bufferId);

            //      We need to use the water movement information to change rock to dirt,
            //      and then move dirt along with the water movement
            //
            //      We really need to know velocity information for the water.
            //      We can try to calculate the velocity based on the change in height (but
            //      this won't give accurate results in all situations. Sometimes it might
            //      be a strong flow, but the height is staying the same).

        _pimpl->_waterSim->BindForErosionSimulation(metalContext, _pimpl->_bufferId);
        
        // metalContext.GetUnderlying()->CopyResource(_pimpl->_gpucache[1].get(), _pimpl->_gpucache[0].get());
        // Metal::ShaderResourceView terrainHeightsCopySRV(_pimpl->_gpucache[1].get());

        // UnorderedAccessView uav(_pimpl->_gpucache[0].get());
        metalContext.BindCS(RenderCore::MakeResourceList(1, _pimpl->_hardMaterialsUAV, _pimpl->_softMaterialsUAV));
        // metalContext.BindCS(RenderCore::MakeResourceList(terrainHeightsCopySRV));

        struct TickErosionSimConstats
        { 
            Int2 gpuCacheOffset, simulationSize;
            
            float KConstant;			// = 2.f		(effectively, max sediment that can be moved in one second)
	        float ErosionRate;			// = 0.03f;		hard to soft rate
	        float SettlingRate;			// = 0.05f;		soft to hard rate (deposition / settling)
	        float MaxSediment;			// = 2.f;		max sediment per cell (ie, max value in the soft materials array)
	        float DepthMax;				// = 25.f		Shallow water erodes more quickly, up to this depth
	        float SedimentShiftScalar; 	// = 1.f;		Amount of sediment that moves per frame

	        float ElementSpacing;
	        float TanSlopeAngle;
	        float ThermalErosionRate;	// = 0.05;		Speed of material shifting due to thermal erosion
            unsigned dummy[3];

        } constants = {
            Int2(0,0), _pimpl->_simSize,
            params._kConstant, 
            params._erosionRate,
            params._settlingRate,
            params._maxSediment,
            params._depthMax,
            params._sedimentShiftScalar,
            _pimpl->_worldSpaceSpacing,
            XlTan(params._thermalSlopeAngle * gPI / 180.f),
            params._thermalErosionRate
        };
        metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(RenderCore::MakeResourceList(5, MakeMetalCB(&constants, sizeof(constants))));
        metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(RenderCore::MakeResourceList(
            Techniques::CommonResources()._defaultSampler,
            Techniques::CommonResources()._linearClampSampler));

        char defines[256];
        _snprintf_s(defines, _TRUNCATE, 
            "SHALLOW_WATER_TILE_DIMENSION=%i;SURFACE_HEIGHTS_FLOAT=%i;USE_LOOKUP_TABLE=1", 
            _pimpl->_waterSim->GetGridDimension(),
            _pimpl->_surfaceHeightsProvider->IsFloatFormat());

        auto simSize = _pimpl->_simSize;

            // update sediment
        auto& updateShader = ::Assets::GetAssetDep<Metal::ComputeShader>("xleres/ocean/tickerosion.csh:UpdateSediment:cs_*", defines);
        metalContext.Bind(updateShader);
        metalContext.Dispatch(simSize[0]/16, simSize[1]/16, 1);

            // shift sediment
        Metal::Copy(metalContext, Metal::AsResource(*_pimpl->_softMaterialsCopy->GetUnderlying()), Metal::AsResource(*_pimpl->_softMaterials->GetUnderlying()));
        metalContext.GetNumericUniforms(ShaderStage::Compute).Bind(RenderCore::MakeResourceList(1, _pimpl->_softMaterialsCopySRV));

        auto& shiftShader = ::Assets::GetAssetDep<Metal::ComputeShader>("xleres/ocean/tickerosion.csh:ShiftSediment:cs_*", defines);
        metalContext.Bind(shiftShader);
        metalContext.Dispatch(simSize[0]/16, simSize[1]/16, 1);

            // "thermal" erosion
        Metal::Copy(metalContext, Metal::AsResource(*_pimpl->_softMaterialsCopy->GetUnderlying()), Metal::AsResource(*_pimpl->_hardMaterials->GetUnderlying()));

        auto& thermalShader = ::Assets::GetAssetDep<Metal::ComputeShader>("xleres/ocean/tickerosion.csh:ThermalErosion:cs_*", defines);
        metalContext.Bind(thermalShader);
        metalContext.Dispatch(simSize[0]/16, simSize[1]/16, 1);
        
        MetalStubs::UnbindCS<Metal::UnorderedAccessView>(metalContext, 0, 8);

        ++_pimpl->_bufferId;
    }

    UInt2 ErosionSimulation::GetDimensions() const { return _pimpl->_simSize; }
    float ErosionSimulation::GetWorldSpaceSpacing() const { return _pimpl->_worldSpaceSpacing; }

    UInt2 ErosionSimulation::DefaultTileSize()
    {
        return UInt2(
            ErosionWaterTileDimension/ErosionWaterTileScale,
            ErosionWaterTileDimension/ErosionWaterTileScale);
    }

    // void    ErosionSimulation::Pimpl::End()
    // {
    //         //      Finish the erosion sim, and delete all of the related objects
    //     _surfaceHeightsProvider.reset();
    //     _waterSim.reset();
    //     _bufferId = 0;
    //     _hardMaterials.reset();
    //     _softMaterials.reset();
    //     _softMaterialsCopy.reset();
    //     _hardMaterialsUAV = UAV();
    //     _softMaterialsUAV = UAV();
    //     _hardMaterialsSRV = SRV();
    //     _softMaterialsSRV = SRV();
    //     _softMaterialsCopySRV = SRV();
    //     _simSize = UInt2(0,0);
    // }

    void    ErosionSimulation::RenderDebugging(
        RenderCore::Metal::DeviceContext& metalContext,
        RenderCore::Techniques::ParsingContext& parserContext,
        RenderDebugMode mode,
        const Float2& worldSpaceOffset)
    {
        CATCH_ASSETS_BEGIN
            const float terrainScale = _pimpl->_worldSpaceSpacing;
            metalContext.GetNumericUniforms(ShaderStage::Pixel).Bind(RenderCore::MakeResourceList(2, 
                _pimpl->_hardMaterialsSRV, _pimpl->_softMaterialsSRV));

            if (mode == RenderDebugMode::WaterVelocity3D) {
                _pimpl->_waterSim->RenderVelocities(
                    metalContext, parserContext,
                    DeepOceanSimSettings(), terrainScale * ErosionWaterTileDimension / ErosionWaterTileScale, 
                    worldSpaceOffset, _pimpl->_bufferId-1, 
                    ShallowWaterSim::BorderMode::Surface, true);
            } else {

                const ::Assets::ResChar* pixelShader;
                if (mode == RenderDebugMode::HardMaterials) {
                    pixelShader = "xleres/ocean/erosiondebug.sh:ps_hardMaterials:ps_*";
                } else {
                    pixelShader = "xleres/ocean/erosiondebug.sh:ps_softMaterials:ps_*";
                }

                auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                    "xleres/basic3D.vsh:PT:vs_*", pixelShader);

                Float2 wsDims = _pimpl->_simSize * _pimpl->_worldSpaceSpacing;

                struct Vertex { Float3 position; Float2 texCoord; } 
                vertices[] = 
                {
                    { Float3(0.f, 0.f, 0.f), Float2(0.f, 0.f) },
                    { Float3(wsDims[0], 0.f, 0.f), Float2(wsDims[0], 0.f) },
                    { Float3(0.f, wsDims[1], 0.f), Float2(0.f, wsDims[1]) },
                    { Float3(wsDims[0], wsDims[1], 0.f), Float2(wsDims[0], wsDims[1]) }
                };

                Metal::BoundInputLayout inputLayout(GlobalInputLayouts::PT, shader);
                Metal::BoundUniforms uniforms(
					shader,
					Metal::PipelineLayoutConfig{},
					Techniques::TechniqueContext::GetGlobalUniformsStreamInterface());
                
                uniforms.Apply(metalContext, 0, parserContext.GetGlobalUniformsStream());
                metalContext.Bind(shader);

				auto vb = MakeMetalVB(vertices, sizeof(vertices));
				VertexBufferView vbvs[] = {&vb};
				inputLayout.Apply(metalContext, MakeIteratorRange(vbvs));

                metalContext.Bind(Techniques::CommonResources()._cullDisable);
                metalContext.Bind(Topology::TriangleStrip);
                metalContext.Draw(4);
            }
        CATCH_ASSETS_END(parserContext)
    }

    ErosionSimulation::ErosionSimulation(UInt2 dimensions, float worldSpaceSpacing)
    {
        _pimpl = std::make_unique<Pimpl>(dimensions, worldSpaceSpacing);
    }

    ErosionSimulation::~ErosionSimulation()
    {}


    ErosionSimulation::Settings::Settings()
    {
        _rainQuantityPerFrame = 0.001f;
        _evaporationConstant = 0.99f;
        _pressureConstant = 200.f;
        _kConstant = 3.f;
        _erosionRate = 0.15f;
        _settlingRate = 0.25f;
        _maxSediment = 1.f;
        _depthMax = 25.f;
        _sedimentShiftScalar = 1.f;
        _thermalSlopeAngle = 40.f;
        _thermalErosionRate = 0.05f;
    }

}


template<> const ClassAccessors& GetAccessors<SceneEngine::ErosionSimulation::Settings>()
{
    using Obj = SceneEngine::ErosionSimulation::Settings;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("RainQuantityPerFrame", DefaultGet(Obj, _rainQuantityPerFrame),  DefaultSet(Obj, _rainQuantityPerFrame));
        props.Add("EvaporationConstant", DefaultGet(Obj, _evaporationConstant),  DefaultSet(Obj, _evaporationConstant));
        props.Add("PressureConstant", DefaultGet(Obj, _pressureConstant),  DefaultSet(Obj, _pressureConstant));
        props.Add("KConstant", DefaultGet(Obj, _kConstant),  DefaultSet(Obj, _kConstant));
        props.Add("ErosionRate", DefaultGet(Obj, _erosionRate),  DefaultSet(Obj, _erosionRate));
        props.Add("SettlingRate", DefaultGet(Obj, _settlingRate),  DefaultSet(Obj, _settlingRate));
        props.Add("MaxSediment", DefaultGet(Obj, _maxSediment),  DefaultSet(Obj, _maxSediment));
        props.Add("DepthMax", DefaultGet(Obj, _depthMax),  DefaultSet(Obj, _depthMax));
        props.Add("SedimentShiftScalar", DefaultGet(Obj, _sedimentShiftScalar),  DefaultSet(Obj, _sedimentShiftScalar));
        props.Add("ThermalSlopeAngle", DefaultGet(Obj, _thermalSlopeAngle),  DefaultSet(Obj, _thermalSlopeAngle));
        props.Add("ThermalErosionRate", DefaultGet(Obj, _thermalErosionRate),  DefaultSet(Obj, _thermalErosionRate));
        init = true;
    }
    return props;
}

