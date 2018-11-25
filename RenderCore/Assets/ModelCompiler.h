// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Core/Types.h"
#include <memory>

namespace Assets { class DirectorySearchRules; }

namespace RenderCore { namespace Assets 
{

    class ModelCompiler : public ::Assets::IAssetCompiler, public std::enable_shared_from_this<ModelCompiler>
    {
    public:
        std::shared_ptr<::Assets::IArtifactCompileMarker> Prepare(
            uint64 typeCode, 
            const StringSection<::Assets::ResChar> initializers[], unsigned initializerCount);

        void StallOnPendingOperations(bool cancelAll);

        static const uint64 Type_Model = ConstHash64<'Mode', 'l'>::Value;
        static const uint64 Type_AnimationSet = ConstHash64<'Anim', 'Set'>::Value;
        static const uint64 Type_Skeleton = ConstHash64<'Skel', 'eton'>::Value;
        static const uint64 Type_RawMat = ConstHash64<'RawM', 'at'>::Value;

		void AddLibrarySearchDirectories(const ::Assets::DirectorySearchRules&);

        ModelCompiler();
        ~ModelCompiler();
    protected:
        class Pimpl;
        std::shared_ptr<Pimpl> _pimpl;

        class Marker;
    };

}}

