// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IFileSystem.h"
#include <unordered_map>

namespace Assets
{
	using Blob = std::shared_ptr<std::vector<uint8_t>>;
	std::unique_ptr<IFileInterface> CreateMemoryFile(const Blob&);

	std::unique_ptr<IFileInterface> CreateSubFile(
		const std::shared_ptr<MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange);

	/// Pass 0 to fixedWindowSize for a non-fixed-window-size compressed block
	/// (in other words, there's expected to be a header that contains the window size)
	/// "15" seems to be a common default for the fixedWindowSize
	std::unique_ptr<IFileInterface> CreateDecompressOnReadFile(
		const std::shared_ptr<MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange,
		size_t decompressedSize,
		unsigned fixedWindowSize = 0);

	// Creates a static case sensitive filesystem containing the given of files (in a single directory)
	// with the given contents
	std::shared_ptr<IFileSystem>	CreateFileSystem_Memory(const std::unordered_map<std::string, Blob>& filesAndContents);
}

