// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IManipulator.h"
#include "../../Math/Vector.h"

namespace SceneEngine
{
    class TerrainManager;
}

namespace ToolsRig
{
    class TerrainManipulatorContext;

    class TerrainManipulatorBase : public IManipulator
    {
    protected:
        std::shared_ptr<SceneEngine::TerrainManager>    _terrainManager;
        std::shared_ptr<TerrainManipulatorContext>      _manipulatorContext;

        Float2 TerrainToWorldSpace(const Float2& input) const;
        Float2 WorldSpaceToTerrain(const Float2& input) const;
        float WorldSpaceDistanceToTerrainCoords(float input) const;
        Float2 WorldSpaceToCoverage(unsigned layerId, const Float2& input) const;
        float WorldSpaceToCoverageDistance(unsigned layerId, float input) const;

		virtual void    Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext);

        TerrainManipulatorBase(
            std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
            std::shared_ptr<TerrainManipulatorContext> manipulatorContext);
    };

    class CommonManipulator : public TerrainManipulatorBase
    {
    public:
            // IManipulator interface
        virtual bool    OnInputEvent(
            const PlatformRig::InputSnapshot& evnt, 
            const SceneEngine::IntersectionTestContext2& hitTestContext,
            const SceneEngine::IntersectionTestScene& hitTestScene);
        virtual void    Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext);

        virtual void    PerformAction(const Float3& worldSpacePosition, float size, float strength) = 0;
        virtual void    SetActivationState(bool) {}
        virtual std::string GetStatusText() const { return std::string(); }

        CommonManipulator(
            std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
            std::shared_ptr<TerrainManipulatorContext> manipulatorContext);

    protected:
        std::pair<Float3, bool> _currentWorldSpaceTarget;
        std::pair<Float3, bool> _targetOnMouseDown;
        Int2        _mouseCoords;
        float       _strength;
        float       _size;
        unsigned    _lastPerform;
        unsigned    _lastRenderCount0, _lastRenderCount1;
    };

    class RectangleManipulator : public TerrainManipulatorBase
    {
    public:
            // IManipulator interface
        virtual bool    OnInputEvent(
            const PlatformRig::InputSnapshot& evnt, 
            const SceneEngine::IntersectionTestContext2& hitTestContext,
            const SceneEngine::IntersectionTestScene& hitTestScene);
        virtual void    Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext);

        virtual void    PerformAction(const Float3& anchor0, const Float3& anchor1) = 0;
        
		virtual IteratorRange<const FloatParameter*>  GetFloatParameters() const { return {}; }
		virtual IteratorRange<const BoolParameter*>   GetBoolParameters() const { return {}; }
        virtual void SetActivationState(bool) {}
        virtual std::string GetStatusText() const { return std::string(); }

        RectangleManipulator(
            std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
            std::shared_ptr<TerrainManipulatorContext> manipulatorContext);

    protected:
        Float3  _firstAnchor;
        bool    _isDragging;
        std::pair<Float3, bool> _secondAnchor;
    };
}

