// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/IDevice_Forward.h"
#include "../../RenderCore/IThreadContext_Forward.h"
#include "../../Math/Matrix.h"
#include "../../Core/Types.h"
#include <memory>

namespace PlatformRig { class IInputListener; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class ParsingContext; class TechniqueContext; } }

namespace SceneEngine
{
    class TerrainManager;
    class PlacementsManager;
    class PlacementsRenderer;
    class LightingParserContext;
    class PlacementsEditor;
    class PlacementCellSet;
    typedef std::pair<uint64, uint64> PlacementGUID;
    class IntersectionTestScene;
}

namespace ToolsRig
{
	class VisCameraSettings;

    /// <summary>Basic tools for placing and arranging objects<summary>
    /// To author a world, we need to be able to select, move and place
    /// objects. Normally this kind of work would be done in a complex
    /// gui program. But these tools are intended to help get started with
    /// some basic tools before we have everything we need.
    class PlacementsManipulatorsManager
    {
    public:
        void RenderWidgets(
            RenderCore::IThreadContext& device, 
            RenderCore::Techniques::ParsingContext& projectionDesc);
        void RenderToScene(
            RenderCore::IThreadContext& context, 
            RenderCore::Techniques::ParsingContext& parserContext);

        std::shared_ptr<PlatformRig::IInputListener> GetInputLister();

        PlacementsManipulatorsManager(
            const std::shared_ptr<SceneEngine::PlacementsManager>& placementsManager,
            const std::shared_ptr<SceneEngine::PlacementCellSet>& placementCellSet,
            const std::shared_ptr<SceneEngine::TerrainManager>& terrainManager,
            const std::shared_ptr<VisCameraSettings>& camera,
			const std::shared_ptr<RenderCore::Techniques::TechniqueContext>& techniqueContext);
        ~PlacementsManipulatorsManager();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    class IManipulator;

    class IPlacementManipulatorSettings
    {
    public:
        virtual std::string GetSelectedModel() const = 0;
        virtual std::string GetSelectedMaterial() const = 0;
        virtual void EnableSelectedModelDisplay(bool newState) = 0;
        virtual void SelectModel(const char newModelName[], const char materialName[]) = 0;

        struct Mode { enum Enum { Select, PlaceSingle }; };
        virtual void SwitchToMode(Mode::Enum newMode) = 0;

        virtual ~IPlacementManipulatorSettings();
    };

    std::vector<std::unique_ptr<IManipulator>> CreatePlacementManipulators(
        IPlacementManipulatorSettings* context,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
        std::shared_ptr<SceneEngine::PlacementsRenderer> renderer);

    void CalculateScatterOperation(
        std::vector<SceneEngine::PlacementGUID>& _toBeDeleted,
        std::vector<Float3>& _spawnPositions,
        SceneEngine::PlacementsEditor& editor,
        const SceneEngine::IntersectionTestScene& hitTestScene,
        const char* const* modelNames, unsigned modelCount,
        const Float3& centre, float radius, float density);
}

