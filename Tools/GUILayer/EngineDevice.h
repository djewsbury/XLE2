// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/IThreadContext_Forward.h"
#include <memory>

using namespace System::ComponentModel::Composition;

namespace GUILayer
{
    class NativeEngineDevice;

    public interface class IOnEngineShutdown 
    {
        virtual void OnEngineShutdown();
    };

    /// <summary>CLI layer to represent a rendering device</summary>
    ///
    /// This class manages the construction/destruction and access of some global 
    /// engine resources.
    ///
    /// It must be a managed classed, so that it can be accessed from a C# layer.
    /// We generally want to avoid putting a lot of functionality in "ref class"
    /// CLI objects -- but we do need them to provide interfaces that can be used
    /// from GUI elements. This creates a kind of balancing act between what should
    /// go in "ref class" objects and plain native objects.
    public ref class EngineDevice
    {
    public:
        static EngineDevice^ GetInstance() { return s_instance; }

        static void SetDefaultWorkingDirectory();
        NativeEngineDevice& GetNative() { return *_pimpl; }

        RenderCore::IThreadContext* GetNativeImmediateContext();
        void ForegroundUpdate();

        void AttachDefaultCompilers();
        void PrepareForShutdown();

        void AddOnShutdown(IOnEngineShutdown^ callback);

        EngineDevice();
        ~EngineDevice();
        !EngineDevice();
    protected:
        NativeEngineDevice* _pimpl;			// (note that we can't use a clix::auto_ptr, because that requires an extra include here)
        System::Collections::Generic::IList<System::WeakReference^>^ _shutdownCallbacks;

        static EngineDevice^ s_instance;
    };
}

