// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelCache.h"
#include "ModelRunTime.h"
#include "MaterialScaffold.h"
#include "SharedStateSet.h"
#include "Services.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/AssetTraits.h"
#include "../../Utility/HeapUtils.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/StringFormat.h"
#include <map>

namespace RenderCore { namespace Assets
{
    typedef std::pair<Float3, Float3> BoundingBox;
    class ModelCache::Pimpl
    {
    public:
        std::map<uint64, BoundingBox> _boundingBoxes;

        LRUCache<::Assets::AssetFuture<ModelScaffold>>     _modelScaffolds;
        LRUCache<::Assets::AssetFuture<MaterialScaffold>>  _materialScaffolds;
        LRUCache<ModelRenderer>     _modelRenderers;

        std::unique_ptr<SharedStateSet> _sharedStateSet;

        uint32 _reloadId;

        Pimpl(const ModelCache::Config& cfg);
        ~Pimpl();

        LRUCache<::Assets::AssetFuture<ModelSupplementScaffold>>   _supplements;

        std::vector<const ModelSupplementScaffold*> 
            LoadSupplementScaffolds(
                StringSection<ResChar> modelFilename, 
                StringSection<ResChar> materialFilename,
                IteratorRange<const SupplementGUID*> supplements);
    };
        
    ModelCache::Pimpl::Pimpl(const ModelCache::Config& cfg)
    : _modelScaffolds(cfg._modelScaffoldCount)
    , _materialScaffolds(cfg._materialScaffoldCount)
    , _modelRenderers(cfg._rendererCount)
    , _supplements(100), _reloadId(0)
    {}

    ModelCache::Pimpl::~Pimpl() {}

    namespace Internal
    {
        static ::Assets::FuturePtr<ModelScaffold> CreateModelScaffold(StringSection<::Assets::ResChar> filename)
        {
            return ::Assets::MakeAsset<ModelScaffold>(filename);
        }

        static ::Assets::FuturePtr<MaterialScaffold> CreateMaterialScaffold(
            StringSection<::Assets::ResChar> model, 
            StringSection<::Assets::ResChar> material)
        {
                // note --  we need to remove any parameters after ':' in the model name
                //          these are references to sub-nodes within the model hierarchy
                //          (which are irrelevant when dealing with materials, since the
                //          materials are shared for the entire model file)
            ::Assets::ResChar temp[MaxPath];
            auto splitter = MakeFileNameSplitter(model);
            if (!splitter.ParametersWithDivider().IsEmpty()) {
                XlCopyString(temp, splitter.AllExceptParameters());
                model = temp;
            }

            return ::Assets::MakeAsset<MaterialScaffold>(material, model);
        }
    }

    uint32 ModelCache::GetReloadId() { return _pimpl->_reloadId; }

    auto ModelCache::GetScaffolds(
        StringSection<ResChar> modelFilename, 
        StringSection<ResChar> materialFilename) -> ModelCacheScaffolds
    {
		ModelCacheScaffolds result;
        result._hashedModelName = Hash64(modelFilename.begin(), modelFilename.end());
        auto modelFuture = _pimpl->_modelScaffolds.Get(result._hashedModelName);
        if (!modelFuture || modelFuture->IsOutOfDate()) {
			modelFuture = Internal::CreateModelScaffold(modelFilename);
            auto insertType = _pimpl->_modelScaffolds.Insert(result._hashedModelName, modelFuture);
                // we upload the "_reloadId" value when change one of our pointers from a previous
                // valid value to something new
            if (modelFuture || insertType == LRUCacheInsertType::EvictAndReplace) ++_pimpl->_reloadId;
        }

        result._hashedMaterialName = HashCombine(Hash64(materialFilename.begin(), materialFilename.end()), result._hashedModelName);
        auto matNamePtr = materialFilename;

        auto materialFuture = _pimpl->_materialScaffolds.Get(result._hashedMaterialName);
        if (!materialFuture || materialFuture->IsOutOfDate()) {
			materialFuture = Internal::CreateMaterialScaffold(modelFilename, matNamePtr);
            auto insertType = _pimpl->_materialScaffolds.Insert(result._hashedMaterialName, materialFuture);
            if (materialFuture || insertType == LRUCacheInsertType::EvictAndReplace) ++_pimpl->_reloadId;
        }

		if (modelFuture)
			result._model = modelFuture->TryActualize().get();

		if (materialFuture)
			result._material = materialFuture->TryActualize().get();

        return result;
    }

    namespace Internal
    {
        static ::Assets::FuturePtr<ModelSupplementScaffold> CreateSupplement(
            uint64 compilerHash,
            StringSection<::Assets::ResChar> modelFilename,
            StringSection<::Assets::ResChar> materialFilename)
        {
            return ::Assets::MakeAsset<ModelSupplementScaffold>(
				MakeStringSection((const ::Assets::ResChar*)&compilerHash, (const ::Assets::ResChar*)PtrAdd(&compilerHash, sizeof(compilerHash))),
				modelFilename, materialFilename);
        }
    }

    std::vector<const ModelSupplementScaffold*> ModelCache::Pimpl::LoadSupplementScaffolds(
        StringSection<ResChar> modelFilename, StringSection<ResChar> materialFilename,
        IteratorRange<const SupplementGUID*> supplements)
    {
        std::vector<const ModelSupplementScaffold*> result;
        for (auto s=supplements.cbegin(); s!=supplements.cend(); ++s) {
            auto hashName = HashCombine(HashCombine(Hash64(modelFilename.begin(), modelFilename.end()), Hash64(materialFilename.begin(), materialFilename.end())), *s);
            auto supp = _supplements.Get(hashName);
            if (!supp || supp->IsOutOfDate()) {
                if (supp) { ++_reloadId; }
                supp = Internal::CreateSupplement(*s, modelFilename, materialFilename);
                if (supp) {
                    auto insertType = _supplements.Insert(hashName, supp);
                    if (insertType == LRUCacheInsertType::EvictAndReplace) { ++_reloadId; }
                }
            }
			const auto* actual = supp->TryActualize().get();
            if (actual) result.push_back(actual);
        }
        return std::move(result);
    }

    auto ModelCache::GetModel(
        StringSection<ResChar> modelFilename, StringSection<ResChar> materialFilename,
        IteratorRange<const SupplementGUID*> supplements,
        unsigned LOD) -> ModelCacheModel
    {
        auto scaffold = GetScaffolds(modelFilename, materialFilename);
		if (!scaffold._model || !scaffold._material)
			// Throw(::Assets::Exceptions::PendingAsset(modelFilename));
			return ModelCacheModel{};

        auto maxLOD = scaffold._model->GetMaxLOD();
        LOD = std::min(LOD, maxLOD);

        uint64 hashedModel = HashCombine(HashCombine(Hash64(modelFilename.begin(), modelFilename.end()), Hash64(materialFilename.begin(), materialFilename.end())), LOD);
        for (auto s=supplements.begin(); s!=supplements.end(); ++s)
            hashedModel = HashCombine(hashedModel, *s);

		BoundingBox boundingBox = std::make_pair(Zero<Float3>(), Zero<Float3>());
		auto renderer = _pimpl->_modelRenderers.Get(hashedModel);
		if (!renderer || renderer->GetDependencyValidation()->GetValidationIndex() > 0) {
			auto searchRules = ::Assets::DefaultDirectorySearchRules(modelFilename);
			searchRules.AddSearchDirectoryFromFilename(materialFilename);
			auto suppScaff = _pimpl->LoadSupplementScaffolds(modelFilename, materialFilename, supplements);
			if (renderer) { ++_pimpl->_reloadId; }
			renderer = std::make_shared<ModelRenderer>(
				std::ref(*scaffold._model), std::ref(*scaffold._material), 
				MakeIteratorRange(suppScaff),
				std::ref(*_pimpl->_sharedStateSet), &searchRules, LOD);

			auto insertType = _pimpl->_modelRenderers.Insert(hashedModel, renderer);
			if (insertType == LRUCacheInsertType::EvictAndReplace) { ++_pimpl->_reloadId; }
		}

			// cache the bounding box, because it's an expensive operation to recalculate
		auto boundingBoxI = _pimpl->_boundingBoxes.find(scaffold._hashedModelName);
		if (boundingBoxI== _pimpl->_boundingBoxes.end()) {
			boundingBox = scaffold._model->GetStaticBoundingBox(0);
			_pimpl->_boundingBoxes.insert(std::make_pair(scaffold._hashedModelName, boundingBox));
		} else {
			boundingBox = boundingBoxI->second;
		}

        ModelCacheModel result;
        result._renderer = renderer.get();
        result._sharedStateSet = _pimpl->_sharedStateSet.get();
        result._model = scaffold._model;
        result._boundingBox = boundingBox;
        result._hashedModelName = scaffold._hashedModelName;
        result._hashedMaterialName = scaffold._hashedMaterialName;
        result._selectedLOD = LOD;
        result._maxLOD = maxLOD;
        return result;
    }

    ::Assets::AssetState ModelCache::PrepareModel(
        StringSection<ResChar> modelFilename, StringSection<ResChar> materialFilename,
        SupplementRange supplements,
        unsigned LOD)
    {
        auto scaffold = GetScaffolds(modelFilename, materialFilename);
        if (!scaffold._model || !scaffold._material)
            return ::Assets::AssetState::Pending;
        return ::Assets::AssetState::Ready;
    }

    ::Assets::FuturePtr<ModelScaffold> ModelCache::GetModelScaffold(StringSection<ResChar> modelFilename)
    {
        auto hashedModelName = Hash64(modelFilename.begin(), modelFilename.end());
        auto modelFuture = _pimpl->_modelScaffolds.Get(hashedModelName);
        if (!modelFuture) {
			modelFuture = Internal::CreateModelScaffold(modelFilename);
            if (modelFuture) { ++_pimpl->_reloadId; }
            auto insertType = _pimpl->_modelScaffolds.Insert(hashedModelName, modelFuture);
            if (insertType == LRUCacheInsertType::EvictAndReplace) { ++_pimpl->_reloadId; }
        }
        return modelFuture;
    }

    SharedStateSet& ModelCache::GetSharedStateSet() { return *_pimpl->_sharedStateSet; }

    ModelCache::ModelCache(const Config& cfg)
    {
        _pimpl = std::make_unique<Pimpl>(cfg);
        _pimpl->_sharedStateSet = std::make_unique<SharedStateSet>(RenderCore::Assets::Services::GetTechniqueConfigDirs());
    }

    ModelCache::~ModelCache()
    {}


}}
