// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainManipulatorsInterface.h"
#include "TerrainManipulators.h"
#include "IManipulator.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../Tools/ToolsRig/IManipulator.h"
#include "../../Tools/ToolsRig/ManipulatorsUtil.h"
#include "../../RenderOverlays/Font.h"
#include "../../RenderOverlays/OverlayUtils.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/IntrusivePtr.h"

namespace ToolsRig
{

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //      I N T E R F A C E           //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class ManipulatorsInterface::InputListener : public RenderOverlays::DebuggingDisplay::IInputListener
    {
    public:
        bool OnInputEvent(
			const RenderOverlays::DebuggingDisplay::InputContext& context,
			const RenderOverlays::DebuggingDisplay::InputSnapshot& evnt);
        InputListener(std::shared_ptr<ManipulatorsInterface> parent);
    private:
        std::weak_ptr<ManipulatorsInterface> _parent;
    };

    bool    ManipulatorsInterface::InputListener::OnInputEvent(
		const RenderOverlays::DebuggingDisplay::InputContext& context,
		const RenderOverlays::DebuggingDisplay::InputSnapshot& evnt)
    {
        auto p = _parent.lock();
        if (p) {
            if (auto a = p->GetActiveManipulator()) {
                return a->OnInputEvent(evnt, *p->_intersectionTestContext, *p->_intersectionTestScene);
            }
        }
        return false;
    }

    ManipulatorsInterface::InputListener::InputListener(std::shared_ptr<ManipulatorsInterface> parent)
        : _parent(std::move(parent))
    {}

    void    ManipulatorsInterface::Render(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext)
    {
		auto a = GetActiveManipulator();
        if (a)
            a->Render(context, parserContext);
    }

    void    ManipulatorsInterface::Update()
    {}

    void    ManipulatorsInterface::SelectManipulator(signed relativeIndex)
    {
        _activeManipulatorIndex = unsigned(_activeManipulatorIndex + relativeIndex + _manipulators.size()) % unsigned(_manipulators.size());
    }

    std::shared_ptr<RenderOverlays::DebuggingDisplay::IInputListener>   ManipulatorsInterface::CreateInputListener()
    {
        return std::make_shared<InputListener>(shared_from_this());
    }

    ManipulatorsInterface::ManipulatorsInterface(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
        std::shared_ptr<TerrainManipulatorContext> terrainManipulatorContext,
        std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionTestContext)
    {
        _activeManipulatorIndex = 0;
        _manipulators = CreateTerrainManipulators(terrainManager, terrainManipulatorContext);

        auto intersectionTestScene = std::make_shared<SceneEngine::IntersectionTestScene>(terrainManager);

        _terrainManager = std::move(terrainManager);
        _intersectionTestContext = std::move(intersectionTestContext);
        _intersectionTestScene = std::move(intersectionTestScene);
    }

    ManipulatorsInterface::~ManipulatorsInterface()
    {
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //      G U I   E L E M E N T S           //
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    static const auto Id_TotalRect = InteractableId_Make("TerrainManipulators");
    static const auto Id_SelectedManipulator = InteractableId_Make("SelectedManipulator");
    static const auto Id_SelectedManipulatorLeft = InteractableId_Make("SelectedManipulatorLeft");
    static const auto Id_SelectedManipulatorRight = InteractableId_Make("SelectedManipulatorRight");

    static const auto Id_CurFloatParameters = InteractableId_Make("CurrentManipulatorParameters");
    static const auto Id_CurFloatParametersLeft = InteractableId_Make("CurrentManipulatorParametersLeft");
    static const auto Id_CurFloatParametersRight = InteractableId_Make("CurrentManipulatorParametersRight");

    static const auto Id_CurBoolParameters = InteractableId_Make("CurrentManipulatorBoolParameters");

    static void DrawAndRegisterLeftRight(IOverlayContext* context, Interactables&interactables, InterfaceState& interfaceState, const Rect& rect, InteractableId left, InteractableId right)
    {
        Rect manipulatorLeft(rect._topLeft, Coord2(LinearInterpolate(rect._topLeft[0], rect._bottomRight[0], 0.5f), rect._bottomRight[1]));
        Rect manipulatorRight(Coord2(LinearInterpolate(rect._topLeft[0], rect._bottomRight[0], 0.5f), rect._topLeft[1]), rect._bottomRight);
        interactables.Register(Interactables::Widget(manipulatorLeft, left));
        interactables.Register(Interactables::Widget(manipulatorRight, right));

        if (interfaceState.HasMouseOver(left)) {
                // draw a little triangle pointing to the left. It's only visible on mouse-over
            const Float2 centerPoint(
                float(rect._topLeft[0] + 16.f),
                float(LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f)-1.f));
            float width = XlTan(60.f * float(M_PI) / 180.f) * 5.f;    // building a equalteral triangle
            Float3 pts[] = 
            {
                Expand(Float2(centerPoint + Float2(-width,  0.f)), 0.f),
                Expand(Float2(centerPoint + Float2( 0.f,   -5.f)), 0.f),
                Expand(Float2(centerPoint + Float2( 0.f,    5.f)), 0.f)
            };
            context->DrawTriangle(ProjectionMode::P2D, pts[0], ColorB(0xffffffff), pts[1], ColorB(0xffffffff), pts[2], ColorB(0xffffffff));
        }

        if (interfaceState.HasMouseOver(right)) {
            const Float2 centerPoint(
                float(rect._bottomRight[0] - 16.f),
                float(LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f)-1.f));

            float width = XlTan(60.f * float(M_PI) / 180.f) * 5.f;    // building a equalteral triangle
            Float3 pts[] = 
            {
                Expand(Float2(centerPoint + Float2(width,  0.f)), 0.f),
                Expand(Float2(centerPoint + Float2(  0.f, -5.f)), 0.f),
                Expand(Float2(centerPoint + Float2(  0.f,  5.f)), 0.f)
            };
            context->DrawTriangle(ProjectionMode::P2D, pts[0], ColorB(0xffffffff), pts[1], ColorB(0xffffffff), pts[2], ColorB(0xffffffff));
        }
    }

    class WidgetResources
    {
    public:
        class Desc {};

		std::shared_ptr<RenderOverlays::Font> _headingFont;
        WidgetResources(const Desc&);
    };

    WidgetResources::WidgetResources(const Desc&)
    {
        _headingFont = GetX2Font("Raleway", 20);
    }

    Rect DrawManipulatorControls(
        IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState,
        IManipulator& manipulator, const char title[])
    {
        auto mainLayoutSize = layout.GetMaximumSize();
        static float desiredWidthPercentage = 40.f/100.f;
        static unsigned screenEdgePadding = 16;

        static ColorB backgroundRectangleColour (  64,   96,   64,  127);
        static ColorB backgroundOutlineColour   ( 192,  192,  192, 0xff);
        static ColorB headerColourNormal        ( 192,  192,  192, 0xff);
        static ColorB headerColourHighlight     (0xff, 0xff, 0xff, 0xff);
        static unsigned lineHeight = 20;

        auto floatParameters = manipulator.GetFloatParameters();
        auto boolParameters = manipulator.GetBoolParameters();
        auto statusText = manipulator.GetStatusText();

        auto& res = ConsoleRig::FindCachedBox<WidgetResources>(WidgetResources::Desc());

        unsigned parameterCount = unsigned(1 + floatParameters.second + boolParameters.second); // (+1 for the selector control)
        if (!statusText.empty()) { ++parameterCount; }
        Coord desiredHeight = 
            parameterCount * lineHeight + (std::max(0u, parameterCount-1) * layout._paddingBetweenAllocations)
            + 25 + layout._paddingBetweenAllocations + 2 * layout._paddingInternalBorder;
        
        Coord width = unsigned(mainLayoutSize.Width() * desiredWidthPercentage);
        Rect controlsRect(
            Coord2(mainLayoutSize._bottomRight[0] - screenEdgePadding - width, mainLayoutSize._bottomRight[1] - screenEdgePadding - desiredHeight),
            Coord2(mainLayoutSize._bottomRight[0] - screenEdgePadding, mainLayoutSize._bottomRight[1] - screenEdgePadding));

        Layout internalLayout(controlsRect);
        
        DrawRectangle(&context, controlsRect, backgroundRectangleColour);
        DrawRectangleOutline(&context, Rect(controlsRect._topLeft + Coord2(2,2), controlsRect._bottomRight - Coord2(2,2)), 0.f, backgroundOutlineColour);
        interactables.Register(Interactables::Widget(controlsRect, Id_TotalRect));

        const auto headingRect = internalLayout.AllocateFullWidth(25);
        context.DrawText(
            std::make_tuple(Float3(float(headingRect._topLeft[0]), float(headingRect._topLeft[1]), 0.f), Float3(float(headingRect._bottomRight[0]), float(headingRect._bottomRight[1]), 0.f)),
			res._headingFont, TextStyle{}, interfaceState.HasMouseOver(Id_TotalRect)?headerColourHighlight:headerColourNormal, TextAlignment::Center, 
            title);

            //
            //      Draw controls for parameters. Starting with the float parameters
            //
        
        for (size_t c=0; c<floatParameters.second; ++c) {
            auto parameter = floatParameters.first[c];
            const auto rect = internalLayout.AllocateFullWidth(lineHeight);
            float* p = (float*)PtrAdd(&manipulator, parameter._valueOffset);

            interactables.Register(Interactables::Widget(rect, Id_CurFloatParameters+c));
            auto formatting = FormatButton(interfaceState, Id_CurFloatParameters+c);

                // background (with special shader)
            float alpha;
            if (parameter._scaleType == IManipulator::FloatParameter::Linear) {
                alpha = Clamp((*p - parameter._min) / (parameter._max - parameter._min), 0.f, 1.f);
            } else {
                alpha = Clamp((std::log(*p) - std::log(parameter._min)) / (std::log(parameter._max) - std::log(parameter._min)), 0.f, 1.f);
            }
            context.DrawQuad(
                ProjectionMode::P2D, 
                AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])),
                AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1])),
                ColorB(0xffffffff), ColorB(0xffffffff),
                Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(alpha, 0.f), Float2(alpha, 0.f),
                "Utility\\DebuggingShapes.psh:SmallGridBackground");

                // text label (name and value)
            char buffer[256];
            _snprintf_s(buffer, _TRUNCATE, "%s = %5.1f", parameter._name, *p);
            context.DrawText(
                std::make_tuple(Float3(float(rect._topLeft[0]), float(rect._topLeft[1]), 0.f), Float3(float(rect._bottomRight[0]), float(rect._bottomRight[1]), 0.f)),
				nullptr, TextStyle{}, formatting._foreground, TextAlignment::Center, buffer);
            
            DrawAndRegisterLeftRight(&context, interactables, interfaceState, rect, Id_CurFloatParametersLeft+c, Id_CurFloatParametersRight+c);
        }

            //
            //      Also draw controls for the bool parameters
            //

        for (size_t c=0; c<boolParameters.second; ++c) {
            auto parameter = boolParameters.first[c];
            const auto rect = internalLayout.AllocateFullWidth(lineHeight);
            unsigned* p = (unsigned*)PtrAdd(&manipulator, parameter._valueOffset);
            bool value = !!((*p) & (1<<parameter._bitIndex));

            interactables.Register(Interactables::Widget(rect, Id_CurBoolParameters+c));
            auto formatting = FormatButton(interfaceState, Id_CurBoolParameters+c);

            char buffer[256];
            if (value) {
                _snprintf_s(buffer, _TRUNCATE, "<%s>", parameter._name);
            } else 
                _snprintf_s(buffer, _TRUNCATE, "%s", parameter._name);

            context.DrawText(
                std::make_tuple(Float3(float(rect._topLeft[0]), float(rect._topLeft[1]), 0.f), Float3(float(rect._bottomRight[0]), float(rect._bottomRight[1]), 0.f)),
				nullptr, TextStyle{}, formatting._foreground, TextAlignment::Center, buffer);
        }

            //
            //      Also status text (if any set)
            //

        if (!statusText.empty()) {
            const auto rect = internalLayout.AllocateFullWidth(lineHeight);
            context.DrawText(
                std::make_tuple(AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight)),
				nullptr, TextStyle{}, headerColourNormal, TextAlignment::Center, statusText.c_str());
        }

            //
            //      Draw manipulator left/right button
            //          (selects next or previous manipulator tool)
            //

        Rect selectedManipulatorRect = internalLayout.AllocateFullWidth(lineHeight);
        interactables.Register(Interactables::Widget(selectedManipulatorRect, Id_SelectedManipulator));
        DrawButtonBasic(
            &context, selectedManipulatorRect, manipulator.GetName(),
            FormatButton(interfaceState, Id_SelectedManipulator));

            //  this button is a left/right selector. Create interactable rectangles for the left and right sides
        DrawAndRegisterLeftRight(
            &context, interactables, interfaceState, selectedManipulatorRect, 
            Id_SelectedManipulatorLeft, Id_SelectedManipulatorRight);

        return controlsRect;
    }

    static void AdjustFloatParameter(IManipulator& manipulator, const IManipulator::FloatParameter& parameter, float increaseAmount)
    {
        const float clicksFromEndToEnd = 100.f;
        if (parameter._scaleType == IManipulator::FloatParameter::Linear) {
            float adjustment = (parameter._max - parameter._min) / clicksFromEndToEnd;
            float newValue = *(float*)PtrAdd(&manipulator, parameter._valueOffset) + increaseAmount * adjustment;
            newValue = Clamp(newValue, parameter._min, parameter._max);
            *(float*)PtrAdd(&manipulator, parameter._valueOffset) = newValue;
        }
        else 
        if (parameter._scaleType == IManipulator::FloatParameter::Logarithmic) {
            auto p = (float*)PtrAdd(&manipulator, parameter._valueOffset);
            float scale = (std::log(parameter._max) - std::log(parameter._min)) / clicksFromEndToEnd;
            float a = std::log(*p);
            a += increaseAmount * scale;
            *p = Clamp(std::exp(a), parameter._min, parameter._max);
        }
    }

    bool HandleManipulatorsControls(InterfaceState& interfaceState, const InputSnapshot& input, IManipulator& manipulator)
    {
        if (input.IsHeld_LButton()) {
            auto topMost = interfaceState.TopMostWidget();

                //  increase or decrease the parameter values
                    //      stay inside the min/max bounds. How far we go depends on the scale type of the parameter
                    //          * linear -- simple, it's just constant increase or decrease
                    //          * logarithmic -- it's more complex. We must increase by larger amounts as the number gets bigger

            auto floatParameters = manipulator.GetFloatParameters();
            if (topMost._id >= Id_CurFloatParametersLeft && topMost._id <= (Id_CurFloatParametersLeft + floatParameters.second)) {
                auto& parameter = floatParameters.first[topMost._id - Id_CurFloatParametersLeft];
                AdjustFloatParameter(manipulator, parameter, -1.f);
                return true;
            } else if (topMost._id >= Id_CurFloatParametersRight && topMost._id <= (Id_CurFloatParametersRight + floatParameters.second)) {
                auto& parameter = floatParameters.first[topMost._id - Id_CurFloatParametersRight];
                AdjustFloatParameter(manipulator, parameter, 1.f);
                return true;
            }

            auto boolParameters = manipulator.GetBoolParameters();
            if (topMost._id >= Id_CurBoolParameters && topMost._id <= (Id_CurBoolParameters + boolParameters.second)) {
                auto& parameter = boolParameters.first[topMost._id - Id_CurBoolParameters];
                
                unsigned* p = (unsigned*)PtrAdd(&manipulator, parameter._valueOffset);
                *p ^= 1<<parameter._bitIndex;

                return true;
            }
        }

        return false;
    }

    void    ManipulatorsDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
    {
        auto* activeManipulator = _manipulatorsInterface->GetActiveManipulator();
        DrawManipulatorControls(context, layout, interactables, interfaceState, *activeManipulator, "Terrain tools");
    }

    bool    ManipulatorsDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input)
    {
        auto topMost = interfaceState.TopMostWidget();
        if (input.IsRelease_LButton()) {
            if (topMost._id == Id_SelectedManipulatorLeft) {
                    // go back one manipulator
                _manipulatorsInterface->SelectManipulator(-1);
                return true;
            }
            else if (topMost._id == Id_SelectedManipulatorRight) {
                    // go forward one manipulator
                _manipulatorsInterface->SelectManipulator(1);
                return true;
            }
        }

        return 
            HandleManipulatorsControls(interfaceState, input, *_manipulatorsInterface->GetActiveManipulator())
            || !interfaceState.GetMouseOverStack().empty();
    }


    ManipulatorsDisplay::ManipulatorsDisplay(std::shared_ptr<ManipulatorsInterface> interf)
        : _manipulatorsInterface(std::move(interf))
    {}

    ManipulatorsDisplay::~ManipulatorsDisplay()
    {}

}


