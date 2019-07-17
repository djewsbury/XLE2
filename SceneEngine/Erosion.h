// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Metal/Forward.h"
#include "../Math/Vector.h"
#include <memory>

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; } }

namespace SceneEngine
{
    class ErosionSimulation
    {
    public:
        struct Settings
        {
                // water movement
            float _rainQuantityPerFrame;
            float _evaporationConstant;
            float _pressureConstant;

                // rainfall erosion
            float _kConstant;
            float _erosionRate;
            float _settlingRate;
            float _maxSediment;
            float _depthMax;
            float _sedimentShiftScalar;

                // thermal erosion
            float _thermalSlopeAngle;
            float _thermalErosionRate;

            Settings();
        };

        void Tick(
            RenderCore::Metal::DeviceContext& metalContext,
            const Settings& settings);

        enum class RenderDebugMode
        {
            WaterVelocity3D,
            HardMaterials, SoftMaterials
        };
        void RenderDebugging(
            RenderCore::Metal::DeviceContext& metalContext,
            RenderCore::Techniques::ParsingContext& parserContext,
            RenderDebugMode mode,
            const Float2& worldSpaceOffset = Float2(0.f, 0.f));

        void InitHeights(
            RenderCore::Metal::DeviceContext& metalContext,
            RenderCore::Metal::ShaderResourceView& source, 
            UInt2 topLeft, UInt2 bottomRight);
        void GetHeights(
            RenderCore::Metal::DeviceContext& metalContext,
            RenderCore::Metal::UnorderedAccessView& dest, 
            UInt2 topLeft, UInt2 bottomRight);

        UInt2 GetDimensions() const;
        float GetWorldSpaceSpacing() const;

        static UInt2 DefaultTileSize();
        
        ErosionSimulation(UInt2 dimensions, float worldSpaceSpacing);
        ~ErosionSimulation();

    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}

