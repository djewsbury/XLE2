// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "../../ShaderService.h"
#include "../../Types.h"
#include "../../../Assets/Assets.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/StringFormat.h"

#pragma warning(disable:4702)

namespace RenderCore { namespace Metal_Vulkan
{
    using ::Assets::ResChar;


        ////////////////////////////////////////////////////////////////////////////////////////////////

    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& ps)
    {
		_validationCallback = std::make_shared<Assets::DependencyValidation>();

		if (vs.GetStage() != ShaderStage::Null) {
			assert(vs.GetStage() == ShaderStage::Vertex);
            _modules[(unsigned)ShaderStage::Vertex] = factory.CreateShaderModule(vs.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Vertex] = vs;
			assert(_modules[(unsigned)ShaderStage::Vertex]);
			Assets::RegisterAssetDependency(_validationCallback, vs.GetDependencyValidation());
		}

		if (ps.GetStage() != ShaderStage::Null) {
			assert(ps.GetStage() == ShaderStage::Pixel);
            _modules[(unsigned)ShaderStage::Pixel] = factory.CreateShaderModule(ps.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Pixel] = ps;
			assert(_modules[(unsigned)ShaderStage::Pixel]);
			Assets::RegisterAssetDependency(_validationCallback, ps.GetDependencyValidation());
		}
    }
    
    ShaderProgram::ShaderProgram(   ObjectFactory& factory, 
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& gs,
									const CompiledShaderByteCode& ps,
									StreamOutputInitializers so)
    :   ShaderProgram(factory, vs, ps)
    {
		if (gs.GetStage() != ShaderStage::Null) {
			assert(gs.GetStage() == ShaderStage::Geometry);
            _modules[(unsigned)ShaderStage::Geometry] = factory.CreateShaderModule(gs.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Geometry] = gs;
			assert(_modules[(unsigned)ShaderStage::Geometry]);
			Assets::RegisterAssetDependency(_validationCallback, gs.GetDependencyValidation());
		}
    }

    ShaderProgram::ShaderProgram(   ObjectFactory& factory, 
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& gs,
									const CompiledShaderByteCode& ps,
									const CompiledShaderByteCode& hs,
									const CompiledShaderByteCode& ds,
									StreamOutputInitializers so)
    :   ShaderProgram(factory, vs, gs, ps, so)
    {
		if (hs.GetStage() != ShaderStage::Null) {
			assert(hs.GetStage() == ShaderStage::Hull);
            _modules[(unsigned)ShaderStage::Hull] = factory.CreateShaderModule(hs.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Hull] = hs;
			assert(_modules[(unsigned)ShaderStage::Hull]);
			Assets::RegisterAssetDependency(_validationCallback, hs.GetDependencyValidation());
		}

		if (ds.GetStage() != ShaderStage::Null) {
			assert(ds.GetStage() == ShaderStage::Domain);
            _modules[(unsigned)ShaderStage::Domain] = factory.CreateShaderModule(ds.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Domain] = ds;
			assert(_modules[(unsigned)ShaderStage::Domain]);
			Assets::RegisterAssetDependency(_validationCallback, ds.GetDependencyValidation());
		}
    }

	ShaderProgram::ShaderProgram() {}
    ShaderProgram::~ShaderProgram() {}

    bool ShaderProgram::DynamicLinkingEnabled() const { return false; }

	void        ShaderProgram::Apply(GraphicsPipelineBuilder& pipeline) const
    {
        if (pipeline._shaderProgram != this) {
            pipeline._shaderProgram = this;
            pipeline._pipelineStale = true;
        }
    }

	void        ShaderProgram::Apply(GraphicsPipelineBuilder& pipeline, const BoundClassInterfaces&) const
	{
		if (pipeline._shaderProgram != this) {
            pipeline._shaderProgram = this;
            pipeline._pipelineStale = true;
        }
	}

        ////////////////////////////////////////////////////////////////////////////////////////////////

    ComputeShader::ComputeShader(ObjectFactory& factory, const CompiledShaderByteCode& compiledShader)
    : _compiledCode(compiledShader)
    {
		if (compiledShader.GetStage() != ShaderStage::Null) {
			assert(compiledShader.GetStage() == ShaderStage::Compute);
            _module = factory.CreateShaderModule(compiledShader.GetByteCode());
		}

        _validationCallback = std::make_shared<Assets::DependencyValidation>();
        Assets::RegisterAssetDependency(_validationCallback, compiledShader.GetDependencyValidation());
    }

    ComputeShader::ComputeShader() {}
    ComputeShader::~ComputeShader() {}

        ////////////////////////////////////////////////////////////////////////////////////////////////

	static std::shared_ptr<::Assets::AssetFuture<CompiledShaderByteCode>> MakeByteCodeFuture(ShaderStage stage, StringSection<> initializer, StringSection<> definesTable)
	{
		char profileStr[] = "?s_";
		switch (stage) {
		case ShaderStage::Vertex: profileStr[0] = 'v'; break;
		case ShaderStage::Geometry: profileStr[0] = 'g'; break;
		case ShaderStage::Pixel: profileStr[0] = 'p'; break;
		case ShaderStage::Domain: profileStr[0] = 'd'; break;
		case ShaderStage::Hull: profileStr[0] = 'h'; break;
		case ShaderStage::Compute: profileStr[0] = 'c'; break;
		}
		if (!XlFindStringI(initializer, profileStr)) {
			ResChar temp[MaxPath];
			StringMeldInPlace(temp) << initializer << ":" << profileStr << "*";
			return ::Assets::MakeAsset<CompiledShaderByteCode>(temp, definesTable);
		}
		else {
			return ::Assets::MakeAsset<CompiledShaderByteCode>(initializer, definesTable);
		}
	}

	static void TryRegisterDependency(
		::Assets::DepValPtr& dst,
		const std::shared_ptr<::Assets::AssetFuture<CompiledShaderByteCode>>& future)
	{
		auto futureDepVal = future->GetDependencyValidation();
		if (futureDepVal)
			::Assets::RegisterAssetDependency(dst, futureDepVal);
	}

	void ShaderProgram::ConstructToFuture(
		::Assets::AssetFuture<ShaderProgram>& future,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsCode = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto psCode = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);

		future.SetPollingFunction(
			[vsCode, psCode](::Assets::AssetFuture<ShaderProgram>& thatFuture) -> bool {

			auto vsActual = vsCode->TryActualize();
			auto psActual = psCode->TryActualize();

			if (!vsActual || !psActual) {
				auto vsState = vsCode->GetAssetState();
				auto psState = psCode->GetAssetState();
				if (vsState == ::Assets::AssetState::Invalid || psState == ::Assets::AssetState::Invalid) {
					auto depVal = std::make_shared<::Assets::DependencyValidation>();
					TryRegisterDependency(depVal, vsCode);
					TryRegisterDependency(depVal, psCode);
					thatFuture.SetInvalidAsset(depVal, nullptr);
					return false;
				}
				return true;
			}

			auto newShaderProgram = std::make_shared<ShaderProgram>(GetObjectFactory(), *vsActual, *psActual);
			thatFuture.SetAsset(std::move(newShaderProgram), {});
			return false;
		});
	}

	void ShaderProgram::ConstructToFuture(
		::Assets::AssetFuture<ShaderProgram>& future,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> gsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsCode = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto gsCode = MakeByteCodeFuture(ShaderStage::Geometry, gsName, definesTable);
		auto psCode = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);

		future.SetPollingFunction(
			[vsCode, gsCode, psCode](::Assets::AssetFuture<ShaderProgram>& thatFuture) -> bool {

			auto vsActual = vsCode->TryActualize();
			auto gsActual = gsCode->TryActualize();
			auto psActual = psCode->TryActualize();

			if (!vsActual || !gsActual || !psActual) {
				auto vsState = vsCode->GetAssetState();
				auto gsState = gsCode->GetAssetState();
				auto psState = psCode->GetAssetState();
				if (vsState == ::Assets::AssetState::Invalid || gsState == ::Assets::AssetState::Invalid || psState == ::Assets::AssetState::Invalid) {
					auto depVal = std::make_shared<::Assets::DependencyValidation>();
					TryRegisterDependency(depVal, vsCode);
					TryRegisterDependency(depVal, gsCode);
					TryRegisterDependency(depVal, psCode);
					thatFuture.SetInvalidAsset(depVal, nullptr);
					return false;
				}
				return true;
			}

			auto newShaderProgram = std::make_shared<ShaderProgram>(GetObjectFactory(), *vsActual, *gsActual, *psActual);
			thatFuture.SetAsset(std::move(newShaderProgram), {});
			return false;
		});
	}

	void ShaderProgram::ConstructToFuture(
		::Assets::AssetFuture<ShaderProgram>& future,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> gsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> hsName,
		StringSection<::Assets::ResChar> dsName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsCode = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto gsCode = MakeByteCodeFuture(ShaderStage::Geometry, gsName, definesTable);
		auto psCode = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);
		auto hsCode = MakeByteCodeFuture(ShaderStage::Hull, hsName, definesTable);
		auto dsCode = MakeByteCodeFuture(ShaderStage::Domain, dsName, definesTable);

		future.SetPollingFunction(
			[vsCode, gsCode, psCode, hsCode, dsCode](::Assets::AssetFuture<ShaderProgram>& thatFuture) -> bool {

			auto vsActual = vsCode->TryActualize();
			auto gsActual = gsCode->TryActualize();
			auto psActual = psCode->TryActualize();
			auto hsActual = hsCode->TryActualize();
			auto dsActual = dsCode->TryActualize();

			if (!vsActual || !gsActual || !psActual || !hsActual || !dsActual) {
				auto vsState = vsCode->GetAssetState();
				auto gsState = gsCode->GetAssetState();
				auto psState = psCode->GetAssetState();
				auto hsState = hsCode->GetAssetState();
				auto dsState = dsCode->GetAssetState();
				if (vsState == ::Assets::AssetState::Invalid || gsState == ::Assets::AssetState::Invalid || psState == ::Assets::AssetState::Invalid || hsState == ::Assets::AssetState::Invalid || dsState == ::Assets::AssetState::Invalid) {
					auto depVal = std::make_shared<::Assets::DependencyValidation>();
					TryRegisterDependency(depVal, vsCode);
					TryRegisterDependency(depVal, gsCode);
					TryRegisterDependency(depVal, psCode);
					TryRegisterDependency(depVal, hsCode);
					TryRegisterDependency(depVal, dsCode);
					thatFuture.SetInvalidAsset(depVal, nullptr);
					return false;
				}
				return true;
			}

			auto newShaderProgram = std::make_shared<ShaderProgram>(GetObjectFactory(), *vsActual, *gsActual, *psActual, *hsActual, *dsActual);
			thatFuture.SetAsset(std::move(newShaderProgram), {});
			return false;
		});
	}

	void ComputeShader::ConstructToFuture(
		::Assets::AssetFuture<ComputeShader>& future,
		StringSection<::Assets::ResChar> codeName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto code = MakeByteCodeFuture(ShaderStage::Compute, codeName, definesTable);

		future.SetPollingFunction(
			[code](::Assets::AssetFuture<ComputeShader>& thatFuture) -> bool {

			auto codeActual = code->TryActualize();

			if (!codeActual) {
				auto codeState = code->GetAssetState();
				if (codeState == ::Assets::AssetState::Invalid) {
					auto depVal = std::make_shared<::Assets::DependencyValidation>();
					TryRegisterDependency(depVal, code);
					thatFuture.SetInvalidAsset(depVal, nullptr);
					return false;
				}
				return true;
			}

			auto newShader = std::make_shared<ComputeShader>(GetObjectFactory(), *codeActual);
			thatFuture.SetAsset(std::move(newShader), {});
			return false;
		});
	}

}}

