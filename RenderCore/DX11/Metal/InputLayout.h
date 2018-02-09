// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DX11.h"
#include "Format.h"
#include "Buffer.h"
#include "../../Types.h"
#include "../../RenderUtils.h"
#include "../../ShaderService.h"     // (just for ShaderStage enum)
#include "../../../Utility/IntrusivePtr.h"
#include "../../../Utility/MiniHeap.h"
#include <memory>
#include <vector>

namespace RenderCore { class VertexBufferView; }

namespace RenderCore { namespace Metal_DX11
{
        ////////////////////////////////////////////////////////////////////////////////////////////////

    class ShaderProgram;

    class BoundInputLayout
    {
    public:
		void Apply(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws;

        BoundInputLayout(const InputLayout& layout, const CompiledShaderByteCode& shader);
        BoundInputLayout(const InputLayout& layout, const ShaderProgram& shader);
        explicit BoundInputLayout(DeviceContext& context);
        BoundInputLayout();
        ~BoundInputLayout();

		BoundInputLayout(BoundInputLayout&& moveFrom) never_throws;
		BoundInputLayout& operator=(BoundInputLayout&& moveFrom) never_throws;

        typedef ID3D::InputLayout*  UnderlyingType;
        UnderlyingType              GetUnderlying() const { return _underlying.get(); }

    private:
        intrusive_ptr<ID3D::InputLayout>	_underlying;
		std::vector<unsigned>				_vertexStrides;
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class ConstantBufferLayoutElement
    {
    public:
        const char*     _name;
		Format			_format;
        unsigned        _offset;
        unsigned        _arrayCount;
    };

    class ConstantBufferLayoutElementHash
    {
    public:
        uint64      _name;
		Format		_format;
        unsigned    _offset;
        unsigned    _arrayCount;
    };

    class ConstantBufferLayout
    {
    public:
        size_t _size;
        std::unique_ptr<ConstantBufferLayoutElementHash[]> _elements;
        unsigned _elementCount;

        ConstantBufferLayout();
        ConstantBufferLayout(ConstantBufferLayout&& moveFrom);
        ConstantBufferLayout& operator=(ConstantBufferLayout&& moveFrom);

    protected:
        ConstantBufferLayout(const ConstantBufferLayout&);
        ConstantBufferLayout& operator=(const ConstantBufferLayout&);
    };

    class DeviceContext;
    class ShaderResourceView;
    class ShaderProgram;
    class DeepShaderProgram;

    using ConstantBufferPacket = SharedPkt;

    class UniformsStream
    {
    public:
        UniformsStream();
        UniformsStream( const ConstantBufferPacket packets[], const Buffer* prebuiltBuffers[], size_t packetCount,
                        const ShaderResourceView* resources[] = nullptr, size_t resourceCount = 0);

        template <int Count0>
            UniformsStream( ConstantBufferPacket (&packets)[Count0]);
        template <int Count0, int Count1>
            UniformsStream( ConstantBufferPacket (&packets)[Count0],
                            const Buffer* (&prebuiltBuffers)[Count1]);
        template <int Count0, int Count1>
            UniformsStream( ConstantBufferPacket (&packets)[Count0],
                            const ShaderResourceView* (&resources)[Count1]);
        template <int Count0, int Count1, int Count2>
            UniformsStream( ConstantBufferPacket (&packets)[Count0],
                            const Buffer* (&prebuiltBuffers)[Count1],
                            const ShaderResourceView* (&resources)[Count2]);

        UniformsStream(
            std::initializer_list<const ConstantBufferPacket> cbs,
            std::initializer_list<const ShaderResourceView*> srvs);
    protected:
        const ConstantBufferPacket*     _packets;
        const Buffer*const*     _prebuiltBuffers;
        size_t                          _packetCount;
        const ShaderResourceView*const* _resources;
        size_t                          _resourceCount;

        friend class BoundUniforms;
    };

    class BoundUniforms
    {
    public:
        BoundUniforms(const ShaderProgram& shader);
        BoundUniforms(const DeepShaderProgram& shader);
        BoundUniforms(const CompiledShaderByteCode& shader);
        BoundUniforms(const BoundUniforms& copyFrom);
        BoundUniforms();
        ~BoundUniforms();
        BoundUniforms& operator=(const BoundUniforms& copyFrom);
        BoundUniforms(BoundUniforms&& moveFrom) never_throws;
        BoundUniforms& operator=(BoundUniforms&& moveFrom) never_throws;

        bool BindConstantBuffer(    uint64 hashName, unsigned slot, unsigned uniformsStream,
                                    const ConstantBufferLayoutElement elements[] = nullptr, 
                                    size_t elementCount = 0);
        bool BindShaderResource(    uint64 hashName, unsigned slot, unsigned uniformsStream);

        bool BindConstantBuffers(unsigned uniformsStream, std::initializer_list<const char*> cbs);
        bool BindConstantBuffers(unsigned uniformsStream, std::initializer_list<uint64> cbs);

        bool BindShaderResources(unsigned uniformsStream, std::initializer_list<const char*> res);
        bool BindShaderResources(unsigned uniformsStream, std::initializer_list<uint64> res);

		void CopyReflection(const BoundUniforms& copyFrom);
		intrusive_ptr<ID3D::ShaderReflection> GetReflection(ShaderStage stage);

        ConstantBufferLayout                            GetConstantBufferLayout(const char name[]);
        std::vector<std::pair<ShaderStage,unsigned>>	GetConstantBufferBinding(const char name[]);

        void Apply( DeviceContext& context, 
                    const UniformsStream& stream0, const UniformsStream& stream1) const;
        void UnbindShaderResources(DeviceContext& context, unsigned streamIndex) const;
    private:

        class StageBinding
        {
        public:
            intrusive_ptr<ID3D::ShaderReflection>  _reflection;
            class Binding
            {
            public:
                unsigned _shaderSlot;
                unsigned _inputInterfaceSlot;
                mutable Buffer _savedCB;
            };
            std::vector<Binding>    _shaderConstantBindings;
            std::vector<Binding>    _shaderResourceBindings;

            StageBinding();
            ~StageBinding();
            StageBinding(StageBinding&& moveFrom);
            StageBinding& operator=(StageBinding&& moveFrom);
			StageBinding(const StageBinding& copyFrom);
			StageBinding& operator=(const StageBinding& copyFrom);
        };

        StageBinding    _stageBindings[ShaderStage::Max];
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class BoundClassInterfaces
    {
    public:
        void Bind(uint64 hashName, unsigned bindingArrayIndex, const char instance[]);

        const std::vector<intrusive_ptr<ID3D::ClassInstance>>& GetClassInstances(ShaderStage stage) const;

        BoundClassInterfaces(const ShaderProgram& shader);
        BoundClassInterfaces(const DeepShaderProgram& shader);
        BoundClassInterfaces();
        ~BoundClassInterfaces();

        BoundClassInterfaces(BoundClassInterfaces&& moveFrom);
        BoundClassInterfaces& operator=(BoundClassInterfaces&& moveFrom);
    private:
        class StageBinding
        {
        public:
            intrusive_ptr<ID3D::ShaderReflection>   _reflection;
            intrusive_ptr<ID3D::ClassLinkage>       _linkage;
            std::vector<intrusive_ptr<ID3D::ClassInstance>> _classInstanceArray;

            StageBinding();
            ~StageBinding();
            StageBinding(StageBinding&& moveFrom);
            StageBinding& operator=(StageBinding&& moveFrom);
			StageBinding(const StageBinding& copyFrom);
			StageBinding& operator=(const StageBinding& copyFrom);
        };

        StageBinding    _stageBindings[ShaderStage::Max];
    };

        ////////////////////////////////////////////////////////////////////////////////////////////////

    inline UniformsStream::UniformsStream()
    {
        _packets = nullptr;
        _prebuiltBuffers = nullptr;
        _packetCount = 0;
        _resources = nullptr;
        _resourceCount = 0;
    }

    inline UniformsStream::UniformsStream(  const ConstantBufferPacket packets[], const Buffer* prebuiltBuffers[], size_t packetCount,
                                            const ShaderResourceView* resources[], size_t resourceCount)
    {
        _packets = packets;
        _prebuiltBuffers = prebuiltBuffers;
        _packetCount = packetCount;
        _resources = resources;
        _resourceCount = resourceCount;
    }

    template <int Count0>
        UniformsStream::UniformsStream(ConstantBufferPacket (&packets)[Count0])
        {
            _packets = packets;
            _prebuiltBuffers = nullptr;
            _packetCount = Count0;
            _resources = nullptr;
            _resourceCount = 0;
        }
        
    template <int Count0, int Count1>
        UniformsStream::UniformsStream( ConstantBufferPacket (&packets)[Count0],
                                        const Buffer* (&prebuildBuffers)[Count1])
        {
            static_assert(Count0 == Count1, "Expecting equal length arrays in UniformsStream constructor");
            _packets = packets;
            _prebuiltBuffers = prebuildBuffers;
            _packetCount = Count0;
            _resources = nullptr;
            _resourceCount = 0;
        }

    template <int Count0, int Count1>
        UniformsStream::UniformsStream( ConstantBufferPacket (&packets)[Count0],
                                        const ShaderResourceView* (&resources)[Count1])
        {
            _packets = packets;
            _prebuiltBuffers = nullptr;
            _packetCount = Count0;
            _resources = resources;
            _resourceCount = Count1;
        }

    template <int Count0, int Count1, int Count2>
        UniformsStream::UniformsStream( ConstantBufferPacket (&packets)[Count0],
                                        const Buffer* (&prebuiltBuffers)[Count1],
                                        const ShaderResourceView* (&resources)[Count2])
    {
            static_assert(Count0 == Count1, "Expecting equal length arrays in UniformsStream constructor");
            _packets = packets;
            _prebuiltBuffers = prebuiltBuffers;
            _packetCount = Count0;
            _resources = resources;
            _resourceCount = Count2;
    }

    inline UniformsStream::UniformsStream(
        std::initializer_list<const ConstantBufferPacket> cbs,
        std::initializer_list<const ShaderResourceView*> srvs)
    {
            // note -- this is really dangerous!
            //      we're taking pointers into the initializer_lists. This is fine
            //      if the lifetime of UniformsStream is longer than the initializer_list
            //      (which is common in many use cases of this class).
            //      But there is no protection to make sure that the memory here is valid
            //      when it is used!
            // Use at own risk!
        _packets = cbs.begin();
        _prebuiltBuffers = nullptr;
        _packetCount = cbs.size();
        _resources = srvs.begin();
        _resourceCount = srvs.size();
    }

}}

