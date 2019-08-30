// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceContext.h"
#include "State.h"
#include "Shader.h"
#include "InputLayout.h"
#include "Buffer.h"
#include "Format.h"
#include "../../IThreadContext.h"
#include "../../../ConsoleRig/Log.h"

#include "IncludeGLES.h"
#include <assert.h>

namespace RenderCore { namespace Metal_OpenGLES
{
    static GLenum AsGLIndexBufferType(Format idxFormat)
    {
        GLenum glFormat = GL_UNSIGNED_SHORT;
        switch (idxFormat) {
        case Format::R8_UINT: glFormat = GL_UNSIGNED_BYTE; break;
        case Format::R16_UINT: glFormat = GL_UNSIGNED_SHORT; break;
        case Format::R32_UINT: glFormat = GL_UNSIGNED_INT; break;
        default: assert(0);
        }
        return glFormat;
    }

    void DeviceContext::Bind(const IndexBufferView& IB)
    {
		auto ibBuffer = GetBufferRawGLHandle(*IB._resource);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibBuffer);

        // Note that Format::R32_UINT is only supported on OGLES3.0+
        assert(IB._indexFormat == Format::R32_UINT || IB._indexFormat == Format::R16_UINT || IB._indexFormat == Format::R8_UINT);
        _indicesFormat = AsGLIndexBufferType(IB._indexFormat);
        _indexFormatBytes = BitsPerPixel(IB._indexFormat) / 8;
        _indexBufferOffsetBytes = IB._offset;
        CheckGLError("Bind IndexBufferView");
    }

    void DeviceContext::UnbindInputLayout()
    {
        if (_featureSet & FeatureSet::GLES300) {
            glBindVertexArray(0);
        } else {
            #if GL_APPLE_vertex_array_object
                glBindVertexArrayAPPLE(0);
            #else
                glBindVertexArrayOES(0);
            #endif
        }
        if (_capturedStates)
            _capturedStates->_boundVAO = 0;

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void GraphicsPipelineBuilder::Bind(const ShaderProgram& shaderProgram)
    {
        glUseProgram(shaderProgram.GetUnderlying()->AsRawGLHandle());
        CheckGLError("Bind ShaderProgram");
    }

    static std::shared_ptr<GraphicsPipeline> s_dummyPipeline;
    
    const std::shared_ptr<GraphicsPipeline>& GraphicsPipelineBuilder::CreatePipeline(ObjectFactory&)
    {
        return s_dummyPipeline;
    }

#pragma clang diagnostic ignored "-Wunused-function"        // SetUnmanagedStates() not used in this file

    static void SetUnmanagedStates()
    {
        // The following render states are not managed by RenderCore, but we can use this function
        // to set them all to rational defaults. In many cases, this may be just the same as the
        // default; but it's still useful incase some other system is setting them, and we want to
        // reset to something that won't interfere with our rendering.

        // RasterizationDesc unmanaged states
        glLineWidth(1.f);
        glPolygonOffset(0.f, 0.f);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DITHER);       // (not supported in D3D11)
    }

    void DeviceContext::Bind(const RasterizationDesc& desc)
    {
        if (desc._cullMode != CullMode::None) {
            glEnable(GL_CULL_FACE);
            glCullFace(AsGLenum(desc._cullMode));
        } else {
            glDisable(GL_CULL_FACE);
        }
        glFrontFace(AsGLenum(desc._frontFaceWinding));
        CheckGLError("Bind RasterizationState");
    }

    void GraphicsPipelineBuilder::Bind(const DepthStencilDesc& desc)
    {
        CheckGLError("Bind DepthStencilState (start)");

        glDepthFunc(AsGLenum(desc._depthTest));
        glDepthMask(desc._depthWrite ? GL_TRUE : GL_FALSE);

        if (desc._depthTest != CompareOp::Always) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }

        if (desc._stencilEnable) {
            glStencilFuncSeparate(
                GL_FRONT,
                AsGLenum(desc._frontFaceStencil._comparisonOp),
                desc._stencilReference,
                desc._stencilReadMask);
            glStencilOpSeparate(
                GL_FRONT,
                AsGLenum(desc._frontFaceStencil._failOp),
                AsGLenum(desc._frontFaceStencil._depthFailOp),
                AsGLenum(desc._frontFaceStencil._passOp));
            glStencilFuncSeparate(
                GL_BACK,
                AsGLenum(desc._backFaceStencil._comparisonOp),
                desc._stencilReference,
                desc._stencilReadMask);
            glStencilOpSeparate(
                GL_BACK,
                AsGLenum(desc._backFaceStencil._failOp),
                AsGLenum(desc._backFaceStencil._depthFailOp),
                AsGLenum(desc._backFaceStencil._passOp));
            glStencilMaskSeparate(GL_FRONT_AND_BACK, desc._stencilWriteMask);
            glEnable(GL_STENCIL_TEST);
        } else {
            glDisable(GL_STENCIL_TEST);
        }
        CheckGLError("Bind DepthStencilState");
    }
    
    DepthStencilDesc GraphicsPipelineBuilder::ActiveDepthStencilDesc()
    {
        DepthStencilDesc depthStencil = {};
        GLint depthFunc = 0;
        GLboolean depthTest = 0, depthMask = 0;
        // glGetBooleanv(GL_DEPTH_TEST, &depthTest);      (this is supposed to work, according to the documentation, but IOS drivers don't seem to like it)
        depthTest = glIsEnabled(GL_DEPTH_TEST);
        if (depthTest) {
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
            depthStencil._depthTest = AsCompareOp(depthFunc);
        } else
            depthStencil._depthTest = CompareOp::Always;
        
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        depthStencil._depthWrite = depthMask != 0;
        
        GLboolean stencilEnable = 0;
        // glGetBooleanv(GL_STENCIL_TEST, &stencilEnable);      (this is supposed to work, according to the documentation, but IOS drivers don't seem to like it)
        stencilEnable = glIsEnabled(GL_STENCIL_TEST);
        if (stencilEnable) {
            depthStencil._stencilEnable = true;
            
            GLint stencilRef = 0, stencilValueMask = 0, stencilWriteMask = 0;
            glGetIntegerv(GL_STENCIL_REF, &stencilRef);
            glGetIntegerv(GL_STENCIL_VALUE_MASK, &stencilValueMask);
            glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilWriteMask);
            
            depthStencil._stencilReference = stencilRef;
            depthStencil._stencilReadMask = stencilValueMask;
            depthStencil._stencilWriteMask = stencilWriteMask;
            
            GLint passDepthFail = 0, passDepthPass = 0, fail = 0, stencilFunc = 0;
            glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &passDepthFail);
            glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &passDepthPass);
            glGetIntegerv(GL_STENCIL_FAIL, &fail);
            glGetIntegerv(GL_STENCIL_FUNC, &stencilFunc);
            
            depthStencil._frontFaceStencil._comparisonOp = AsCompareOp(stencilFunc);
            depthStencil._frontFaceStencil._failOp = AsStencilOp(fail);
            depthStencil._frontFaceStencil._passOp = AsStencilOp(passDepthPass);
            depthStencil._frontFaceStencil._depthFailOp = AsStencilOp(passDepthFail);
            
            passDepthFail = passDepthPass = fail = stencilFunc = 0;
            glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, &passDepthFail);
            glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, &passDepthPass);
            glGetIntegerv(GL_STENCIL_BACK_FAIL, &fail);
            glGetIntegerv(GL_STENCIL_BACK_FUNC, &stencilFunc);
            
            depthStencil._backFaceStencil._comparisonOp = AsCompareOp(stencilFunc);
            depthStencil._backFaceStencil._failOp = AsStencilOp(fail);
            depthStencil._backFaceStencil._passOp = AsStencilOp(passDepthPass);
            depthStencil._backFaceStencil._depthFailOp = AsStencilOp(passDepthFail);
            
        } else {
            depthStencil._stencilEnable = false;
        }
        
        return depthStencil;
    }

    void GraphicsPipelineBuilder::Bind(const AttachmentBlendDesc& desc)
    {
        if (desc._blendEnable) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);

        glBlendFuncSeparate(AsGLenum(desc._srcColorBlendFactor), AsGLenum(desc._dstColorBlendFactor), AsGLenum(desc._srcAlphaBlendFactor), AsGLenum(desc._dstAlphaBlendFactor));
        glBlendEquationSeparate(AsGLenum(desc._colorBlendOp), AsGLenum(desc._alphaBlendOp));
        glColorMask(((desc._writeMask & ColorWriteMask::Red)   ? GL_TRUE : GL_FALSE),
                    ((desc._writeMask & ColorWriteMask::Green) ? GL_TRUE : GL_FALSE),
                    ((desc._writeMask & ColorWriteMask::Blue)  ? GL_TRUE : GL_FALSE),
                    ((desc._writeMask & ColorWriteMask::Alpha) ? GL_TRUE : GL_FALSE));
    }

    void DeviceContext::Bind(const ViewportDesc& viewport)
    {
        glViewport((GLint)viewport.TopLeftX, (GLint)viewport.TopLeftY, (GLsizei)viewport.Width, (GLsizei)viewport.Height);

        // hack -- desktop gl has a slight naming change
        #if defined(GL_ES_VERSION_3_0) || defined(GL_ES_VERSION_2_0)
            glDepthRangef(viewport.MinDepth, viewport.MaxDepth);
        #else
            glDepthRange(viewport.MinDepth, viewport.MaxDepth);
        #endif

        CheckGLError("Bind Viewport");
    }

    void DeviceContext::Draw(unsigned vertexCount, unsigned startVertexLocation)
    {
        glDrawArrays(GLenum(_nativeTopology), startVertexLocation, vertexCount);
        CheckGLError("Draw()");
    }

    void DeviceContext::DrawIndexed(unsigned indexCount, unsigned startIndexLocation, unsigned baseVertexLocation)
    {
        CheckGLError("before DrawIndexed()");

        assert(baseVertexLocation==0);  // (doesn't seem to be supported. Maybe best to remove it from the interface)
        glDrawElements(
            GLenum(_nativeTopology), GLsizei(indexCount), // std::min(6, GLsizei(indexCount)),
            GLenum(_indicesFormat),
            (const void*)(size_t)(_indexFormatBytes * startIndexLocation + _indexBufferOffsetBytes));
        CheckGLError("DrawIndexed()");
    }

    void DeviceContext::DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
    {
        #if defined(GL_ES_VERSION_2_0) || defined(GL_ES_VERSION_3_0)
            if (_featureSet & FeatureSet::GLES300) {
                glDrawArraysInstanced(
                    GLenum(_nativeTopology),
                    startVertexLocation, vertexCount,
                    instanceCount);
            } else {
                #if GL_EXT_draw_instanced && (PLATFORMOS_TARGET != PLATFORMOS_WINDOWS) // Some distributions of Angle for Win32s declare but don't implement this function
                    glDrawArraysInstancedEXT(
                        GLenum(_nativeTopology),
                        startVertexLocation, vertexCount,
                        instanceCount);
                #else
                    assert(0);      // no support for instanced draw calls on this feature set
                #endif
            }
        #else
            glDrawArraysInstancedARB(
                GLenum(_nativeTopology),
                startVertexLocation, vertexCount,
                instanceCount);
        #endif
        CheckGLError("DrawInstances()");
    }

    void DeviceContext::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation, unsigned baseVertexLocation)
    {
        assert(baseVertexLocation==0);  // (doesn't seem to be supported. Maybe best to remove it from the interface)
        #if defined(GL_ES_VERSION_2_0) || defined(GL_ES_VERSION_3_0)
            if (_featureSet & FeatureSet::GLES300) {
                glDrawElementsInstanced(
                    GLenum(_nativeTopology), GLsizei(indexCount),
                    GLenum(_indicesFormat),
                    (const void*)(size_t)(_indexFormatBytes * startIndexLocation + _indexBufferOffsetBytes),
                    instanceCount);
            } else {
                #if GL_EXT_draw_instanced && (PLATFORMOS_TARGET != PLATFORMOS_WINDOWS) // Some distributions of Angle for Win32s declare but don't implement this function
                    glDrawElementsInstancedEXT(
                        GLenum(_nativeTopology), GLsizei(indexCount),
                        GLenum(_indicesFormat),
                        (const void*)(size_t)(_indexFormatBytes * startIndexLocation + _indexBufferOffsetBytes),
                        instanceCount);
                #else
                    assert(0);      // no support for instanced draw calls on this feature set
                #endif
            }
        #else
            glDrawElementsInstancedARB(
                GLenum(_nativeTopology), GLsizei(indexCount),
                GLenum(_indicesFormat),
                (const void*)(size_t)(_indexFormatBytes * startIndexLocation + _indexBufferOffsetBytes),
                instanceCount);
        #endif
        CheckGLError("DrawIndexedInstances()");
    }

    static unsigned s_nextCapturedStatesGUID = 1;

    void DeviceContext::BeginStateCapture(CapturedStates& capturedStates)
    {
        assert(!_capturedStates);
        _capturedStates = &capturedStates;

        ///////////////////////////////////////////////////////
            //      Vertex attrib
        GLint maxVertexAttributes = 0;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, (GLint*)&maxVertexAttributes);
        for (unsigned v=0; v<maxVertexAttributes; ++v) {
            glDisableVertexAttribArray(v);
            if (_featureSet & FeatureSet::GLES300)
                glVertexAttribDivisor(v, 0);
        }

        capturedStates._activeVertexAttrib = 0;
        capturedStates._instancedVertexAttrib = 0;
        capturedStates._boundVAO = ~0u;      // (unknown state)

        GLint maxTextureUnits = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, (GLint*)&maxTextureUnits);
        capturedStates._samplerStateBindings.resize(maxTextureUnits);
        for (unsigned c=0; c<maxTextureUnits; ++c)
            capturedStates._samplerStateBindings[c] = ~0u;

        capturedStates._customBindings.clear();
        capturedStates._captureGUID = s_nextCapturedStatesGUID++;

        ///////////////////////////////////////////////////////
            //      Misc
        // capturedStates._texUnitsSetToCube = 0;
    }

    void DeviceContext::EndStateCapture()
    {
        assert(_capturedStates != nullptr);
        _capturedStates = nullptr;
    }

    #if defined(_DEBUG)
        void CapturedStates::VerifyIntegrity()
        {
            if (_boundVAO != ~0u) {
                GLint activeVAO = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &activeVAO);
                assert(_boundVAO == activeVAO);
            }
            
            if (_activeTextureIndex != ~0u) {
                GLint activeTexture = ~0u;
                glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
                assert(activeTexture == (GL_TEXTURE0 + _activeTextureIndex));
            }
        }
    #endif

    GraphicsPipelineBuilder::GraphicsPipelineBuilder(FeatureSet::BitField featureSet)
    {
        _featureSet = featureSet;
        _nativeTopology = GL_TRIANGLES;
    }

    DeviceContext::DeviceContext(std::shared_ptr<IDevice> device, FeatureSet::BitField featureSet)
    : GraphicsPipelineBuilder(featureSet)
    {
        _indicesFormat = AsGLIndexBufferType(Format::R16_UINT);
        _indexFormatBytes = 2;

        _featureSet = featureSet;
        _capturedStates = nullptr;

        _device = device;
    }

    DeviceContext::~DeviceContext()
    {
    }

    void                            DeviceContext::BeginCommandList()
    {   
    }

    intrusive_ptr<CommandList>         DeviceContext::ResolveCommandList()
    {
        return intrusive_ptr<CommandList>();
    }

    void                            DeviceContext::ExecuteCommandList(CommandList& commandList)
    {

    }

    std::shared_ptr<IDevice> DeviceContext::GetDevice()
    {
        return _device.lock();
    }

    const std::shared_ptr<DeviceContext>& DeviceContext::Get(IThreadContext& threadContext)
    {
        static std::shared_ptr<DeviceContext> dummy;
        auto* tc = (IThreadContextOpenGLES*)threadContext.QueryInterface(typeid(IThreadContextOpenGLES).hash_code());
        if (tc) return tc->GetDeviceContext();
        return dummy;
    }

}}

