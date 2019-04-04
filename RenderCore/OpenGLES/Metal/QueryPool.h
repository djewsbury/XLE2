// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../IAnnotator.h"
#include "../../../Utility/IntrusivePtr.h"
#include "../../../Core/Types.h"
#include <algorithm>
#include <memory>

#if defined(_DEBUG) && !defined(GPUANNOTATIONS_ENABLE)
    #define GPUANNOTATIONS_ENABLE
#endif

namespace RenderCore { namespace Metal_OpenGLES
{
	class DeviceContext;
	class ObjectFactory;

	class TimeStampQueryPool
	{
	public:
		using QueryId = unsigned;
		using FrameId = unsigned;

		static const QueryId QueryId_Invalid = ~0u;
		static const FrameId FrameId_Invalid = ~0u;

		QueryId		SetTimeStampQuery(DeviceContext& context);

		FrameId		BeginFrame(DeviceContext& context);
		void		EndFrame(DeviceContext& context, FrameId frame);

		struct FrameResults
		{
			bool		_resultsReady;
			bool		_isDisjoint;
			uint64*		_resultsStart;
			uint64*		_resultsEnd;
			uint64		_frequency;
		};
		FrameResults GetFrameResults(DeviceContext& context, FrameId id);

		TimeStampQueryPool(ObjectFactory& factory);
		~TimeStampQueryPool();
	private:
	};

    #if defined(GPUANNOTATIONS_ENABLE)

        /// <summary>Add a debugging annotation into the GPU command buffer</summary>
        /// These annotations are used for debugging. They will create a marker in debugging
        /// tools (like nsight / RenderDoc). The "annotationName" can be any arbitrary name.
        /// Note that we're taking a wchar_t for the name. This is for DirectX, which needs
        /// a wide character string. But other tools (eg, OpenGL/Android) aren't guaranteed
        /// to work this way. We could do compile time conversion with a system of macros...
        /// Otherwise, we might need fall back to run time conversion.
        class GPUAnnotation
        {
        public:
			static void Begin(DeviceContext& context, const char annotationName[]);
			static void End(DeviceContext& context);

			GPUAnnotation(DeviceContext& context, const char annotationName[]);
            ~GPUAnnotation();
        protected:
            DeviceContext* _context;
        };

    #else

        class GPUAnnotation
        {
        public:
			static void Begin(DeviceContext& context, const char annotationName[]) {}
			static void End(DeviceContext& context) {}
			GPUAnnotation(DeviceContext&, const char[]) {}
        };

    #endif

	class Annotator : public IAnnotator
	{
	public:
		virtual void	Frame_Begin(IThreadContext& primaryContext, unsigned frameID);
		virtual void	Frame_End(IThreadContext& primaryContext);

		virtual void	Event(IThreadContext& context, const char name[], EventTypes::BitField types);

		virtual unsigned	AddEventListener(const EventListener& callback);
		virtual void		RemoveEventListener(unsigned listenerId);

		Annotator();
		virtual ~Annotator();
	};

}}

