#pragma once

#include "DepVal.h"
#if !defined(__CLR_VER)
#include "AssetFuture.h"
#include "DeferredConstruction.h"
#include "../Utility/Threading/Mutex.h"
#endif
#include <memory>

namespace Assets
{
	class DivergentAssetBase;

	class AssetHeapRecord
	{
	public:
		rstring		_initializer;
		AssetState	_state;
		DepValPtr	_depVal;
		Blob		_actualizationLog; 
		uint64_t	_typeCode;
		uint64_t	_idInAssetHeap;
        unsigned    _initializationCount = 0;
	};

	class IDefaultAssetHeap
	{
	public:
		virtual uint64_t		GetTypeCode() const = 0;
		virtual std::string		GetTypeName() const = 0;
		virtual void            Clear() = 0;

		virtual std::vector<AssetHeapRecord>	LogRecords() const = 0;

		virtual ~IDefaultAssetHeap();
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(__CLR_VER)
	template<typename AssetType>
		class DefaultAssetHeap : public IDefaultAssetHeap
	{
	public:
		template<typename... Params>
			FuturePtr<AssetType> Get(Params...);

		template<typename... Params>
			uint64_t SetShadowingAsset(AssetPtr<AssetType>&& newShadowingAsset, Params...);

		void            Clear();
		uint64_t		GetTypeCode() const;
		std::string		GetTypeName() const;

		std::vector<AssetHeapRecord>		LogRecords() const;

		DefaultAssetHeap();
		~DefaultAssetHeap();
		DefaultAssetHeap(const DefaultAssetHeap&) = delete;
		DefaultAssetHeap& operator=(const DefaultAssetHeap&) = delete;
	private:
		mutable Threading::Mutex _lock;		
		std::vector<std::pair<uint64_t, FuturePtr<AssetType>>> _assets;
		std::vector<std::pair<uint64_t, FuturePtr<AssetType>>> _shadowingAssets;
	};

	template<typename AssetType>
		static bool IsInvalidated(AssetFuture<AssetType>& future)
	{
		// We must check the "background state" here. If it's invalidated in the
		// background, we can restart the compile; even if that invalidated state hasn't
		// reached the "foreground" yet.
		AssetPtr<AssetType> actualized;
		DepValPtr depVal;
		Blob actualizationLog;
		auto state = future.CheckStatusBkgrnd(actualized, depVal, actualizationLog);
		if (state == AssetState::Pending)
			return false;
		if (!depVal)
			return false;
		return depVal->GetValidationIndex() > 0;
	}

	template<typename AssetType>
		template<typename... Params>
			auto DefaultAssetHeap<AssetType>::Get(Params... initialisers) -> FuturePtr<AssetType>
	{
		auto hash = Internal::BuildHash(initialisers...);

		FuturePtr<AssetType> newFuture;
		{
			ScopedLock(_lock);
			auto shadowing = LowerBound(_shadowingAssets, hash);
			if (shadowing != _shadowingAssets.end() && shadowing->first == hash)
				return shadowing->second;

			auto i = LowerBound(_assets, hash);
			if (i != _assets.end() && i->first == hash)
				if (!IsInvalidated(*i->second))
					return i->second;

			auto stringInitializer = Internal::AsString(initialisers...);	// (used for tracking/debugging purposes)
			newFuture = std::make_shared<AssetFuture<AssetType>>(stringInitializer);
			if (i != _assets.end() && i->first == hash) {
				i->second = newFuture;
			} else 
				_assets.insert(i, std::make_pair(hash, newFuture));
		}

		// note -- call AutoConstructToFuture outside of the mutex lock, because this operation can be expensive
		// after the future has been constructed but before we complete AutoConstructToFuture, the asset is considered to be
		// in "pending" state, and Actualize() will through a PendingAsset exception, so this should be thread-safe, even if
		// another thread grabs the future before AutoConstructToFuture is done
		AutoConstructToFuture<AssetType>(*newFuture, std::forward<Params>(initialisers)...);
		return newFuture;
	}

	template<typename AssetType>
		template<typename... Params>
			uint64_t DefaultAssetHeap<AssetType>::SetShadowingAsset(AssetPtr<AssetType>&& newShadowingAsset, Params... initialisers)
	{
		auto hash = Internal::BuildHash(initialisers...);

		ScopedLock(_lock);
		auto shadowing = LowerBound(_shadowingAssets, hash);
		if (shadowing != _shadowingAssets.end() && shadowing->first == hash) {
			shadowing->second->SimulateChange(); 
			if (newShadowingAsset) {
				shadowing->second->SetAssetForeground(std::move(newShadowingAsset), nullptr);
			} else {
				_shadowingAssets.erase(shadowing);
			}
			return hash;
		}

		if (newShadowingAsset) {
			auto stringInitializer = Internal::AsString(initialisers...);	// (used for tracking/debugging purposes)
			auto newShadowingFuture = std::make_shared<AssetFuture<AssetType>>(stringInitializer);
			newShadowingFuture->SetAssetForeground(std::move(newShadowingAsset), nullptr);
			_shadowingAssets.emplace(shadowing, std::make_pair(hash, std::move(newShadowingFuture)));
		}

		auto i = LowerBound(_assets, hash);
		if (i != _assets.end() && i->first == hash)
			i->second->SimulateChange();

		return hash;
	}

	template<typename AssetType>
		DefaultAssetHeap<AssetType>::DefaultAssetHeap() {}

	template<typename AssetType>
		DefaultAssetHeap<AssetType>::~DefaultAssetHeap() {}
	
	template<typename AssetType>
		void DefaultAssetHeap<AssetType>::Clear()
    {
        ScopedLock(_lock);
        _assets.clear();
        _shadowingAssets.clear();
    }

	template<typename AssetType>
		auto DefaultAssetHeap<AssetType>::LogRecords() const -> std::vector<AssetHeapRecord>
	{
		ScopedLock(_lock);
		std::vector<AssetHeapRecord> result;
		result.reserve(_assets.size() + _shadowingAssets.size());
		auto typeCode = GetTypeCode();
		for (const auto&a : _assets)
			result.push_back({ a.second->Initializer(), a.second->GetAssetState(), a.second->GetDependencyValidation(), a.second->GetActualizationLog(), typeCode, a.first });
		for (const auto&a : _shadowingAssets)
			result.push_back({ a.second->Initializer(), a.second->GetAssetState(), a.second->GetDependencyValidation(), a.second->GetActualizationLog(), typeCode, a.first });
		return result;
	}

	template<typename AssetType>
		uint64_t		DefaultAssetHeap<AssetType>::GetTypeCode() const
		{
			return typeid(AssetType).hash_code();
		}

	template<typename AssetType>
		std::string		DefaultAssetHeap<AssetType>::GetTypeName() const
		{
			return typeid(AssetType).name();
		}

#endif

}
