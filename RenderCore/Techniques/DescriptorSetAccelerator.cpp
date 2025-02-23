// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DescriptorSetAccelerator.h"
#include "DeferredShaderResource.h"
#include "TechniqueUtils.h"
#include "Services.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Metal/State.h"
#include "../Metal/InputLayout.h"
#include "../IDevice.h"
#include "../BufferView.h"
#include "../UniformsStream.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/Assets.h"
#include "../../Utility/ParameterBox.h"

namespace RenderCore { namespace Techniques 
{

	void DescriptorSetAccelerator::Apply(
		Metal::DeviceContext& devContext,
		Metal::BoundUniforms& boundUniforms,
		unsigned streamIdx) const
	{
		ConstantBufferView cbvs[32];
		const Metal::ShaderResourceView* srvs[32];

		for (auto i=_constantBuffers.begin(); i!=_constantBuffers.end(); ++i)
			cbvs[i-_constantBuffers.begin()] = *i;

		for (auto i=_shaderResources.begin(); i!=_shaderResources.end(); ++i)
			srvs[i-_shaderResources.begin()] = &(*i)->GetShaderResource();

		UniformsStream result;
		result._constantBuffers = MakeIteratorRange(cbvs, cbvs+_constantBuffers.size());
		result._resources = UniformsStream::MakeResources(MakeIteratorRange(srvs, srvs+_shaderResources.size()));
		
		boundUniforms.Apply(devContext, streamIdx, result);
	}

	::Assets::FuturePtr<DescriptorSetAccelerator> MakeDescriptorSetAccelerator(
		const Utility::ParameterBox& constantBindings,
		const Utility::ParameterBox& resourceBindings,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		const std::string& descriptorSetName)
	{
		auto result = std::make_shared<::Assets::AssetFuture<DescriptorSetAccelerator>>(descriptorSetName);

		UniformsStreamInterface usi;

		// Trigger loading of shader resources
		std::vector<::Assets::FuturePtr<DeferredShaderResource>> pendingSRVs;
		pendingSRVs.reserve(layout._resources.size());

		for (unsigned c=0; c<layout._resources.size(); ++c) {
			assert(layout._resources[c]._conditions.empty());		// note -- ignoring conditions on this object currently (this was added for when the descriptor set is parsed from a file, which isn't the usual path to get to here)
			auto hashName = Hash64(layout._resources[c]._name);
			if (layout._resources[c]._arrayElementCount) {
				for (unsigned arrayIndex=0; arrayIndex<layout._resources[c]._arrayElementCount; ++arrayIndex) {
					auto boundResourceName = resourceBindings.GetString<char>(hashName+arrayIndex);
					if (!boundResourceName.empty()) {
						pendingSRVs.push_back(::Assets::MakeAsset<DeferredShaderResource>(MakeStringSection(boundResourceName)));
					} else {
						pendingSRVs.push_back({});
					}
					usi.BindShaderResource(unsigned(pendingSRVs.size()-1), hashName+arrayIndex);
				}
			} else {
				auto boundResourceName = resourceBindings.GetString<char>(hashName);
				if (!boundResourceName.empty()) {
					pendingSRVs.push_back(::Assets::MakeAsset<DeferredShaderResource>(MakeStringSection(boundResourceName)));
				} else {
					pendingSRVs.push_back({});
				}
				usi.BindShaderResource(unsigned(pendingSRVs.size()-1), hashName);
			}
		}

		auto& device = Services::GetDevice();
		auto shrLanguage = GetDefaultShaderLanguage();

		// Build constant buffers
		std::vector<std::shared_ptr<IResource>> constantBuffers;
		constantBuffers.reserve(layout._constantBuffers.size());
		for (unsigned c=0; c<layout._constantBuffers.size(); ++c) {
			const auto& cb = layout._constantBuffers[c];
			auto buffer = cb._layout->BuildCBDataAsVector(constantBindings, shrLanguage);
			constantBuffers.push_back(
				device.CreateResource(
					CreateDesc(BindFlag::ConstantBuffer, 0, GPUAccess::Read, LinearBufferDesc::Create((unsigned)buffer.size()), cb._name),
					[&buffer](SubResourceId) {
						return SubResourceInitData{MakeIteratorRange(buffer)};
					}));

			auto eles = cb._layout->MakeConstantBufferElements(shrLanguage);
			usi.BindConstantBuffer(c, {Hash64(cb._name), MakeIteratorRange(eles)});
		}

		// Not really doing anything with samplers

		result->SetPollingFunction(
			[pendingSRVs, constantBuffers, usi](::Assets::AssetFuture<DescriptorSetAccelerator>& thatFuture) -> bool {
				// Keep waiting as long as all SRVs are in pending state
				for (const auto&d:pendingSRVs)
					if (d && d->GetAssetState() == ::Assets::AssetState::Pending)
						return true;

				auto finalDescriptorSet = std::make_shared<DescriptorSetAccelerator>();
				finalDescriptorSet->_depVal = std::make_shared<::Assets::DependencyValidation>();
				finalDescriptorSet->_usi = std::move(usi);

				// Construct the final descriptor set; even if we got some (or all) invalid assets
				finalDescriptorSet->_shaderResources.reserve(pendingSRVs.size());
				for (const auto&d:pendingSRVs) {
					if (!d) {
						finalDescriptorSet->_shaderResources.push_back(nullptr);
						continue;
					}

					::Assets::AssetPtr<DeferredShaderResource> actualized;
					::Assets::DepValPtr depVal;
					::Assets::Blob actualizationLog;
					if (d->CheckStatusBkgrnd(actualized, depVal, actualizationLog) == ::Assets::AssetState::Ready) {
						finalDescriptorSet->_shaderResources.push_back(actualized);
					} else {
						// todo -- use some kind of "invalid marker" for this resource
						finalDescriptorSet->_shaderResources.push_back(nullptr);
					}

					if (depVal)
						::Assets::RegisterAssetDependency(finalDescriptorSet->_depVal, depVal);
				}

				finalDescriptorSet->_constantBuffers = std::move(constantBuffers);

				thatFuture.SetAsset(std::move(finalDescriptorSet), {});
				return false;
			});

		return result;
	}


}}
