set(Src
    AmbientOcclusion.cpp
    CloudsForm.cpp
    DeepOceanSim.cpp
    DepthWeightedTransparency.cpp
    DualContour.cpp
    DualContourRender.cpp
    DynamicImposters.cpp
    Erosion.cpp
    FluidAdvection.cpp
    Fluid.cpp
    FluidHelper.cpp
    GenericGridPartitioning.cpp
    GenericQuadTree.cpp
    GestaltResource.cpp
    IntersectionTest.cpp
    LightingParser.cpp
    LightingParserResolve.cpp
    LightingParserStandardPlugin.cpp
    LightingTargets.cpp
    LightInternal.cpp
    MetalStubs.cpp
    MetricsBox.cpp
    Noise.cpp
    Ocean.cpp
    OrderIndependentTransparency.cpp
    PlacementsManager.cpp
    PlacementsQuadTree.cpp
    PreparedScene.cpp
    Rain.cpp
    RayTracedShadows.cpp
    RayVsModel.cpp
    RefractionsBuffer.cpp
    RenderingUtils.cpp
    RenderStep_Forward.cpp
    RenderStep_GBuffer.cpp
    RenderStep_PrepareShadows.cpp
    RenderStep_ResolveHDR.cpp
    RenderStepUtils.cpp
    SceneEngineUtils.cpp
    ScreenspaceReflections.cpp
    ShadowResources.cpp
    ShallowSurface.cpp
    ShallowWater.cpp
    Sky.cpp
    StochasticTransparency.cpp
    SunFlare.cpp
    TerrainCollisions.cpp
    TerrainConfig.cpp
    Terrain.cpp
    TerrainFormat.cpp
    TerrainManager.cpp
    TerrainMaterial.cpp
    TerrainMaterialTextures.cpp
    TerrainRender.cpp
    TerrainShortCircuit.cpp
    TerrainUberSurface.cpp
    TextureTileSet.cpp
    TiledLighting.cpp
    Tonemap.cpp
    VegetationSpawnConfig.cpp
    VegetationSpawn.cpp
    VolumetricFog.cpp
	CommonTechniqueDelegates.cpp)

set(Headers
    AmbientOcclusion.h
    CloudsForm.h
    DeepOceanSim.h
    DepthWeightedTransparency.h
    Documentation.h
    DualContour.h
    DualContourRender.h
    DynamicImposters.h
    Erosion.h
    FluidAdvection.h
    Fluid.h
    FluidHelper.h
    GenericGridPartitioning.h
    GenericQuadTree.h
    GestaltResource.h
    GradientFlagSettings.h
    IntersectionTest.h
    LightDesc.h
    LightingParserContext.h
    LightingParser.h
    LightingParserStandardPlugin.h
    LightingTargets.h
    LightInternal.h
    MetalStubs.h
    MetricsBox.h
    Noise.h
    Ocean.h
    OITInternal.h
    OrderIndependentTransparency.h
    PlacementsManager.h
    PlacementsQuadTreeDebugger.h
    PlacementsQuadTree.h
    PreparedScene.h
    Rain.h
    RayTracedShadows.h
    RayVsModel.h
    RefractionsBuffer.h
    RenderingUtils.h
    RenderStep.h
    RenderStepUtils.h
    SceneEngineUtils.h
    SceneParser.h
    ScreenspaceReflections.h
    ShaderLightDesc.h
    ShadowResources.h
    ShallowSurface.h
    ShallowWater.h
    SimplePatchBox.h
    Sky.h
    StochasticTransparency.h
    SunFlare.h
    SurfaceHeightsProvider.h
    TerrainConfig.h
    TerrainCoverageId.h
    TerrainFormat.h
    Terrain.h
    TerrainMaterial.h
    TerrainMaterialTextures.h
    TerrainRender.h
    TerrainScaffold.h
    TerrainShortCircuit.h
    TerrainUberSurface.h
    TextureTileSet.h
    TiledLighting.h
    Tonemap.h
    VegetationSpawn.h
    VolumetricFog.h
	CommonTechniqueDelegates.h)

BasicLibrary(SceneEngine "${Src};${Headers}")
target_link_libraries(SceneEngine ForeignMisc)  # required to pull in interface include directories (etc)
