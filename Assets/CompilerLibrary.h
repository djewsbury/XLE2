// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "../Core/Types.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <memory>

namespace Assets
{
	class NascentChunk;
	using NascentChunkArray = std::shared_ptr<std::vector<NascentChunk>>;
	class DependentFileState;

	class ICompilerDesc
	{
	public:
		virtual const char*			Description() const = 0;

		class FileKind
		{
		public:
			IteratorRange<const uint64_t*>	_assetTypes;
			const ::Assets::ResChar*		_extension;
			const char*						_name;
		};
		virtual unsigned			FileKindCount() const = 0;
		virtual FileKind			GetFileKind(unsigned index) const = 0;

		virtual ~ICompilerDesc();
	};

	class ICompileOperation
	{
	public:
		class TargetDesc
		{
		public:
			uint64			_type;
			const char*		_name;
		};
		virtual unsigned			TargetCount() const = 0;
		virtual TargetDesc			GetTarget(unsigned idx) const = 0;
		virtual NascentChunkArray	SerializeTarget(unsigned idx) = 0;
		virtual std::shared_ptr<std::vector<DependentFileState>> GetDependencies() const = 0;

		virtual ~ICompileOperation();
	};

	typedef std::shared_ptr<ICompilerDesc> GetCompilerDescFn();
	typedef std::shared_ptr<ICompileOperation> CreateCompileOperationFn(StringSection<::Assets::ResChar> identifier);
}

