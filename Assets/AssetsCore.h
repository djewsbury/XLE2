// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Exceptions.h"
#include <string>
#include <memory>
#include <vector>

namespace Utility { template<typename> class StringSection; template<typename> class IteratorRange; }
using namespace Utility;

namespace Assets
{
    using ResChar = char;
	using rstring = std::basic_string<ResChar>;
	using Blob = std::shared_ptr<std::vector<uint8_t>>;

    enum class AssetState { Pending, Ready, Invalid };

    class DependencyValidation;
    using DepValPtr = std::shared_ptr<DependencyValidation>;

	template<typename AssetType> class AssetFuture;
	template<typename AssetType>
		using FuturePtr = std::shared_ptr<AssetFuture<AssetType>>;

	Blob AsBlob(const std::exception& e);
	Blob AsBlob(IteratorRange<const void*> copyFrom);

    /// <summary>Exceptions related to rendering</summary>
    namespace Exceptions
    {
		/// <summary>An error occurred while attempting to retreive and assert from an asset heap</summary>
		/// This is usually caused by either an invalid asset, or an asset that is still pending.
		///
		/// This type of exception (including InvalidAsset and PendingAsset) should only be thrown
		/// from an asset heap implementation. Asset types can use standard exceptions, or ConstructionError
		/// to signal errors during asset construction.
        class RetrievalError : public ::Exceptions::BasicLabel
        {
        public:
            const ResChar* Initializer() const { return _initializer; }
            virtual AssetState State() const = 0;

			RetrievalError(StringSection<ResChar> initializer) never_throws;
        private:
            ResChar _initializer[512];
        };

        /// <summary>An asset can't be loaded</summary>
        /// This exception means a asset failed during loading, and can
        /// never be loaded. It might mean that the resource is corrupted on
        /// disk, or maybe using an unsupported file format (or bad version).
        /// The most common cause is due to a compile error in a shader. 
        /// If we attempt to use a shader with a compile error, it will throw
        /// a InvalidAsset exception.
        class InvalidAsset : public RetrievalError
        {
        public: 
            virtual bool CustomReport() const;
            virtual AssetState State() const;
			const DepValPtr& GetDependencyValidation() const { return _depVal; }
			const Blob& GetActualizationLog() const { return _actualizationLog; }

            InvalidAsset(StringSection<ResChar> initializer, const DepValPtr&, const Blob& actualizationLog) never_throws;
		private:
			DepValPtr _depVal;
			Blob _actualizationLog;
        };

        /// <summary>An asset is still being loaded</summary>
        /// This is common exception. It occurs if we attempt to use an asset that
        /// is still being prepared. Usually this means that the resource is being
        /// loaded from disk, or compiled in a background thread.
        /// For example, shader resources can take some time to compile. If we attempt
        /// to use the shader while it's still compiling, we'll get a PendingAsset
        /// exception.
        class PendingAsset : public RetrievalError
        {
        public: 
            virtual bool CustomReport() const;
            virtual AssetState State() const;

            PendingAsset(StringSection<ResChar> initializer) never_throws;
        };

		/// <summary>An error occurred during the construction of an asset<summary>
		/// This exception type includes some extra properties that are used by the asset system to
		/// figure out how to handle the given error.
		///
		/// For example, the attached dependency validation can be used to monitor for file system 
		/// changes, and reattempt if those files change.
		///
		/// Furthermore, on UnsupportedVersion errors, the system can attempt a recompile of the asset.
        class ConstructionError : public ::Exceptions::BasicLabel
        {
        public:
            enum class Reason
            {
				Unknown,
                UnsupportedVersion,
                FormatNotUnderstood,
				MissingFile
            };

            Reason				GetReason() const { return _reason; }
			const DepValPtr&	GetDependencyValidation() const { return _depVal; }
			const Blob&			GetActualizationLog() const { return _actualizationLog; }

			virtual bool CustomReport() const;
            virtual const char* what() const noexcept;

			ConstructionError(Reason reason, const DepValPtr&, const Blob& actualizationLog) never_throws;
			ConstructionError(Reason reason, const DepValPtr&, const char format[], ...) never_throws;
			ConstructionError(const std::exception&, const DepValPtr&) never_throws;
			ConstructionError(const ConstructionError&, const DepValPtr&) never_throws;

        private:
			Reason _reason;
			DepValPtr _depVal;
			Blob _actualizationLog;
        };
    }
}

