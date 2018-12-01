// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ConfigFileContainer.h"
#include "AssetServices.h"
#include "Assets.h"
#include "IFileSystem.h"
#include "../Utility/StringUtils.h"
#include <regex>

namespace Assets
{

	namespace Internal
	{
		void MarkInvalid(StringSection<ResChar> initializer, const char reason[]);
		void MarkValid(StringSection<ResChar> initializer);
	}

	template<typename Formatter>
		Formatter ConfigFileContainer<Formatter>::GetRootFormatter() const
	{
		if (!_fileData) return Formatter {};
		return Formatter(MemoryMappedInputStream(MakeIteratorRange(*_fileData)));
	}

	template<typename Formatter>
		Formatter ConfigFileContainer<Formatter>::GetFormatter(StringSection<typename Formatter::value_type> configName) const
	{
		if (!_fileData) return Formatter {};

		// search through for the specific element we need (ignoring other elements)
		Formatter formatter(MemoryMappedInputStream(MakeIteratorRange(*_fileData)));

		auto immediateConfigName = configName;
		immediateConfigName._end = std::find(immediateConfigName.begin(), configName.end(), ':');

		for (;;) {
			switch (formatter.PeekNext()) {
			case Formatter::Blob::BeginElement:
				{
					typename Formatter::InteriorSection eleName;
					if (!formatter.TryBeginElement(eleName))
						Throw(Utility::FormatException("Poorly formed begin element in config file", formatter.GetLocation()));

					if (XlEqStringI(eleName, immediateConfigName)) {
						// We can access a nested item using ':' as a separator
						// For example, "first:second:third" will look for "first" at
						// the top level with "second" nested within and then "third"
						// nested within that.
						immediateConfigName._start = immediateConfigName.end()+1;
						if (immediateConfigName.begin() < configName.end())
							immediateConfigName._end = std::find(immediateConfigName.begin(), configName.end(), ':');
						if (immediateConfigName.begin() >= immediateConfigName.end())
							return formatter;
						// else continue searching for the next config name
					} else {
						formatter.SkipElement();    // skip the whole element; it's not required
						if (!formatter.TryEndElement())
							Throw(Utility::FormatException("Expecting end element in config file", formatter.GetLocation()));
					}

					continue;
				}

			case Formatter::Blob::AttributeName:
				{
					typename Formatter::InteriorSection name, value;
					formatter.TryAttribute(name, value);
					continue;
				}

			default:
				break;
			}
			break;
		}

		return Formatter();
	}

	template<typename Formatter>
		ConfigFileContainer<Formatter>::ConfigFileContainer(StringSection<ResChar> initializer)
	{
		_validationCallback = std::make_shared<DependencyValidation>();
		RegisterFileDependency(_validationCallback, initializer);

		_fileData = ::Assets::TryLoadFileAsBlob(initializer);
		if (!_fileData)
			Throw(Exceptions::ConstructionError(Exceptions::ConstructionError::Reason::MissingFile, _validationCallback, "Error loading config file container for %s", initializer.AsString().c_str()));
	}

	template<typename Formatter>
		ConfigFileContainer<Formatter>::ConfigFileContainer(const Blob& blob, const DepValPtr& depVal, StringSection<ResChar>)
	: _fileData(blob), _validationCallback(depVal)
	{
	}

	template<typename Formatter> 
		ConfigFileContainer<Formatter>::ConfigFileContainer<Formatter>::~ConfigFileContainer() {}

	template<typename Formatter>
		auto ConfigFileContainer<Formatter>::CreateNew(StringSection<ResChar> initialiser)
			-> std::unique_ptr<ConfigFileContainer>
	{
		return std::make_unique<ConfigFileContainer>(initialiser);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename DestType = unsigned, typename CharType = char>
        DestType StringToUnsigned(const StringSection<CharType> source)
    {
        auto* start = source.begin();
        auto* end = source.end();
        if (start >= end) return 0;

        auto result = DestType(0);
        for (;;) {
            if (start >= end) break;
            if (*start < '0' || *start > '9') break;
            result = (result * 10) + DestType((*start) - '0');
            ++start;
        }
        return result;
    }

    static bool IsWhitespace(char chr) { return chr == ' ' || chr == '\t'; }

    static std::regex s_chunkHeader(R"--(<<Chunk:(\w+):(\w+)>>(\S+)\()--");

    template<typename CharType>
        std::vector<TextChunk<CharType>> ReadCompoundTextDocument(StringSection<CharType> doc)
    {
            // compound text documents should begin with:
            //      "BOM?//\s*CompoundDocument:(\d)+"
        std::vector<TextChunk<CharType>> result;

        auto* i = doc.begin();

            // Check if we start with the UTF8 byte order mark... Skip over it
            // if so.
        if ((size_t(doc.end()) - size_t(i)) >= 3) {
            auto t = (unsigned char*)i;
            if (t[0] == 0xefu && t[1] == 0xbbu && t[2] == 0xbfu)
                i = (decltype(i))&t[3];
        }

        if (i == doc.end() || *i != '/') return std::move(result);
        ++i;
        if (i == doc.end() || *i != '/') return std::move(result);
        ++i;
        while (i != doc.end() && IsWhitespace(*i)) ++i;     // (whitespace but not newline)

        char markerAscii[] = "CompoundDocument:";
        auto* m = markerAscii;
        while (*m) {
            if (i == doc.end() || *i != *m) return std::move(result);
            ++i; ++m;
        }

        auto versionStart = i;
        while (i != doc.end() && XlIsDigit(*i)) ++i;
        auto version = StringToUnsigned(MakeStringSection(versionStart, i));
        if (version != 1)
            Throw(::Exceptions::BasicLabel("Compound text document version (%i) not understood", version));

        // If we get here, then the result is a compound document that we can read
        // scan through to find the chunks.
        std::regex_iterator<const CharType*> ri(i, doc.end(), s_chunkHeader);
        while (ri != std::regex_iterator<const CharType*>()) {
            const auto& match = *ri;
            if (!match.empty() && match.size() >= 4) {
                const auto& t = match[1];
                const auto& n = match[2];
                const auto& d = match[3];
                auto* ci = match[0].second;

                // We must scan forward to find a ')' followed by a duplicate of the 
                // deliminator pattern
                auto* contentStart = ci;
                while (ci != doc.end()) {
                    while (ci != doc.end() && *ci != ')') ++ci;
                    if (ci == doc.end()) break;

                    const auto* di = d.first;
                    auto ci2 = ci+1;
                    while (ci2 != doc.end() && di != d.second && *ci2 == *di) { ++ci2; ++di; }
                    if (di == d.second) break; // matching deliminator means we terminate here
                    ++ci;
                }

                if (ci == doc.end())
                    Throw(::Exceptions::BasicLabel("Hit end of file while reading chunk in compound text document"));

                result.push_back(
                    TextChunk<CharType>(
                        MakeStringSection(t.first, t.second),
                        MakeStringSection(n.first, n.second),
                        MakeStringSection(contentStart, ci)));
            }
            ++ri;
        }

        return std::move(result);
    }

    // Even though this is templated, it uses std::regex
    //  std::regex has problems with our unicode character types, so it's not
    //  working currently
    template std::vector<TextChunk<char>> ReadCompoundTextDocument(StringSection<char>);

	template class ConfigFileContainer<InputStreamFormatter<utf8>>;
	template class ConfigFileContainer<InputStreamFormatter<utf16>>;
}

