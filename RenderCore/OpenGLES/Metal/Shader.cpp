// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "../../ShaderService.h"
#include "../../RenderUtils.h"
#include "../../Types.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/DepVal.h"
#include "../../../ConsoleRig/Log.h"
#include "../../../Utility/Streams/FileUtils.h"
#include "../../../Utility/Streams/PathUtils.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/Conversion.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Core/SelectConfiguration.h"
#include "IncludeGLES.h"

#include <iostream>
#include <regex>

namespace RenderCore { namespace Metal_OpenGLES
{
    using ::Assets::ResChar;

    class OGLESShaderCompiler : public ShaderService::ILowLevelCompiler
    {
    public:
        virtual void AdaptShaderModel(
            ResChar destination[],
            const size_t destinationCount,
            StringSection<ResChar> inputShaderModel) const;

        virtual bool DoLowLevelCompile(
            /*out*/ Payload& payload,
            /*out*/ Payload& errors,
            /*out*/ std::vector<::Assets::DependentFileState>& dependencies,
            const void* sourceCode, size_t sourceCodeLength,
            const ShaderService::ResId& shaderPath,
            StringSection<::Assets::ResChar> definesTable,
            IteratorRange<const ShaderService::SourceLineMarker*> sourceLineMarkers) const;

        virtual std::string MakeShaderMetricsString(
            const void* byteCode, size_t byteCodeSize) const;

        OGLESShaderCompiler();
        ~OGLESShaderCompiler();

        static Threading::Mutex s_compiledShadersLock;
        static std::unordered_map<uint64_t, intrusive_ptr<OpenGL::Shader>> s_compiledShaders;
    };

    Threading::Mutex OGLESShaderCompiler::s_compiledShadersLock;
    std::unordered_map<uint64_t, intrusive_ptr<OpenGL::Shader>> OGLESShaderCompiler::s_compiledShaders;

    void OGLESShaderCompiler::AdaptShaderModel(
        ResChar destination[],
        const size_t destinationCount,
        StringSection<ResChar> inputShaderModel) const
    {
        if (destination != inputShaderModel.begin())
            XlCopyString(destination, destinationCount, inputShaderModel);
    }

    static unsigned NewLineCount(const char* str)
    {
        unsigned result = 0;
        for (auto i=str; *i != '\0'; ++i)
            if (*i == '\n') ++result;
        return result;
    }
    
    static std::regex s_appleStyle(R"--((\w*)\s*:\s*(\d*)\s*:\s*(\d*)\s*:\s*(.*))--");

    struct ErrorMessage
    {
    public:
        std::string     _sourceFile;
        unsigned        _line;      // (this is a zero-base line number)
        std::string     _msg;
    };
    static std::vector<ErrorMessage> DecodeCompilerErrorLog(
        StringSection<> compilerLog,
        unsigned preambleLineCount,
        IteratorRange<const ShaderService::SourceLineMarker*> sourceLineMarkers)
    {
        std::vector<ErrorMessage> errorMessages;
        const char* i = compilerLog.begin();
        const char* endi = compilerLog.end();
        if (((endi-i) >= 1) && *(endi-1) == '\0') --endi;   // if there's a null terminator, drop back once
        for (; i!=endi;) {
            while (i != endi && (*i == '\n' || *i == '\r')) ++i;

            auto nexti = i;
            while (nexti != endi && *nexti != '\n' && *nexti != '\r') ++nexti;

            auto line = MakeStringSection(i, nexti);
            if (line.IsEmpty()) break;

            std::cmatch match;
            bool a = std::regex_match(line.begin(), line.end(), match, s_appleStyle);
            if (a && match.size() >= 5) {
                std::stringstream str;
                str << match[1].str() << ": " << match[4].str();
                auto line = Conversion::Convert<unsigned>(MakeStringSection(match[3].first, match[3].second));
                line --; // (to zero base number)
                errorMessages.push_back({std::string{}, line, str.str()});
            } else {
                errorMessages.push_back({std::string{}, ~0u, line.AsString()});
            }

            i = nexti;
        }

        // Remap line numbers into the source file name/lines from before preprocessing
        if (!sourceLineMarkers.empty()) {
            for (auto&e:errorMessages) {
                if (e._line == ~0u) continue;

                if (e._line < preambleLineCount) {
                    e._sourceFile = "<<in preamble>>";
                    continue;
                }

                e._line -= preambleLineCount;
                auto m = sourceLineMarkers.end()-1;
                while (m >= sourceLineMarkers.begin()) {
                    if (m->_processedSourceLine <= e._line) {
                        e._line = e._line - m->_processedSourceLine + m->_sourceLine;
                        e._sourceFile = m->_sourceName;
                        break;
                    }
                    --m;
                }
            }
        }

        return errorMessages;
    }

    bool OGLESShaderCompiler::DoLowLevelCompile(
        /*out*/ Payload& payload,
        /*out*/ Payload& errors,
        /*out*/ std::vector<::Assets::DependentFileState>& dependencies,
        const void* sourceCode, size_t sourceCodeLength,
        const ShaderService::ResId& shaderPath,
        StringSection<::Assets::ResChar> definesTable,
        IteratorRange<const ShaderService::SourceLineMarker*> sourceLineMarkers) const
    {
        ShaderType::Enum shaderType = ShaderType::FragmentShader;
        if (shaderPath._shaderModel[0] == 'v') {
            shaderType = ShaderType::VertexShader;
        }

        auto& objectFactory = GetObjectFactory();
        auto newShader = objectFactory.CreateShader(shaderType);
        if (newShader->AsRawGLHandle() == RawGLHandle_Invalid) {
            return false;
        }

        std::stringstream definesPreamble;
        {
            auto p = definesTable.begin();
            while (p != definesTable.end()) {
                while (p != definesTable.end() && std::isspace(*p)) ++p;

                auto definition = std::find(p, definesTable.end(), '=');
                auto defineEnd = std::find(p, definesTable.end(), ';');

                auto endOfName = std::min(defineEnd, definition);
                while ((endOfName-1) > p && std::isspace(*(endOfName-1))) ++endOfName;

                if (definition < defineEnd) {
                    auto e = definition+1;
                    while (e < defineEnd && std::isspace(*e)) ++e;
                    definesPreamble << "#define " << MakeStringSection(p, endOfName).AsString() << " " << MakeStringSection(e, defineEnd).AsString() << "\n";
                } else {
                    definesPreamble << "#define " << MakeStringSection(p, endOfName).AsString() << "\n";
                }

                p = (defineEnd == definesTable.end()) ? defineEnd : (defineEnd+1);
            }
        }
        auto definesPreambleStr = definesPreamble.str();

        bool isFragmentShader = shaderPath._shaderModel[0] == 'p';

        bool supportsGLES300 = strstr((const char*)sourceCode, "SUPPORT_GLSL300");

        #if PLATFORMOS_TARGET == PLATFORMOS_OSX
            // hack for version string for OSX
            const GLchar* versionDecl = isFragmentShader
                ? "#version 120\n#define FRAGMENT_SHADER 1\n"
                : "#version 120\n";
            (void)supportsGLES300;
        #else
            const GLchar* versionDecl;
            if (supportsGLES300) {
                versionDecl = isFragmentShader
                    ? "#version 300 es\n#define FRAGMENT_SHADER 1\n"
                    : "#version 300 es\n";
            } else {
                versionDecl = isFragmentShader
                    ? "#define FRAGMENT_SHADER 1\n"
                    : "";
            }
        #endif

        // DavidJ -- hack -- make this platform preamble configurable, and set when constructing
        //      the ShaderCompiler
        const char* platformPreamble =
            #if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
                "#define CC3_PLATFORM_IOS 0\n"
                "#define CC3_PLATFORM_OSX 0\n"
                "#define CC3_PLATFORM_ANDROID 0\n"
                "#define CC3_PLATFORM_WINDOWS 1\n";
            #elif PLATFORMOS_TARGET == PLATFORMOS_ANDROID
                "#define CC3_PLATFORM_IOS 0\n"
                "#define CC3_PLATFORM_OSX 0\n"
                "#define CC3_PLATFORM_ANDROID 1\n"
                "#define CC3_PLATFORM_WINDOWS 0\n";
            #elif PLATFORMOS_TARGET == PLATFORMOS_IOS
                "#define CC3_PLATFORM_IOS 1\n"
                "#define CC3_PLATFORM_OSX 0\n"
                "#define CC3_PLATFORM_ANDROID 0\n"
                "#define CC3_PLATFORM_WINDOWS 0\n";
            #elif PLATFORMOS_TARGET == PLATFORMOS_OSX
                "#define CC3_PLATFORM_IOS 0\n"
                "#define CC3_PLATFORM_OSX 1\n"
                "#define CC3_PLATFORM_ANDROID 0\n"
                "#define CC3_PLATFORM_WINDOWS 0\n";
            #endif

        const GLchar* shaderSourcePointers[4] { versionDecl, platformPreamble, definesPreambleStr.c_str(), (const GLchar*)sourceCode };
        GLint shaderSourceLengths[4] = { (GLint)std::strlen(versionDecl), (GLint)std::strlen(platformPreamble), (GLint)definesPreambleStr.size(), (GLint)sourceCodeLength };

        Log(Verbose) << "Compiling: " << shaderPath._filename << std::endl;

        glShaderSource  (newShader->AsRawGLHandle(), dimof(shaderSourcePointers), shaderSourcePointers, shaderSourceLengths);
        glCompileShader (newShader->AsRawGLHandle());

        if (ObjectFactory::WriteObjectLabels() && (objectFactory.GetFeatureSet() & FeatureSet::Flags::LabelObject) && shaderPath._filename[0])
            glLabelObjectEXT(GL_SHADER_OBJECT_EXT, newShader->AsRawGLHandle(), 0, shaderPath._filename);

        GLint compileStatus = 0;
        glGetShaderiv   (newShader->AsRawGLHandle(), GL_COMPILE_STATUS, &compileStatus);
        if (!compileStatus) {
            GLint infoLen = 0;
            glGetShaderiv(newShader->AsRawGLHandle(), GL_INFO_LOG_LENGTH, &infoLen);
            if ( infoLen > 1 ) {
                auto infoLog = std::make_unique<char[]>(infoLen);
                glGetShaderInfoLog(newShader->AsRawGLHandle(), infoLen, nullptr, (GLchar*)infoLog.get());

                auto preambleLineCount = NewLineCount(shaderSourcePointers[0]) + NewLineCount(shaderSourcePointers[1]) + NewLineCount(shaderSourcePointers[2]);
                auto errorMessages = DecodeCompilerErrorLog(
                    MakeStringSection(infoLog.get(), PtrAdd(infoLog.get(), infoLen)),
                    preambleLineCount, sourceLineMarkers);

                std::stringstream translatedErrors;
                for (const auto&e:errorMessages) {
                        /* note -- add one to go from zero-based line number to one-based for output */
                    translatedErrors << e._sourceFile << ": " << (e._line+1) << ": " << e._msg << std::endl;
                }

                Log(Error) << "Failure during shader compile. Errors follow:" << std::endl;
                Log(Error) << translatedErrors.str() << std::endl;

                {
                    std::stringstream str;
                    str << "Failure during shader compile. Errors follow:" << std::endl;
                    str << translatedErrors.str() << std::endl;
                    str << "---------------------------------------------" << std::endl;
                    str << "Full source:" << std::endl;
                    for (unsigned c=0; c<dimof(shaderSourcePointers); ++c)
                        str << shaderSourcePointers[c] << std::endl;
                    auto logString = str.str();

                    errors = std::make_shared<std::vector<uint8_t>>((const uint8_t*)AsPointer(logString.begin()), (const uint8_t*)AsPointer(logString.end()));
                }
            }

            return false;
        }

        // Log the shader info log, even if compile was successful
        #if defined(_DEBUG)
            {
                GLint infoLen = 0;
                glGetShaderiv(newShader->AsRawGLHandle(), GL_INFO_LOG_LENGTH, &infoLen);
                if ( infoLen > 1 ) {
                    auto infoLog = std::make_unique<char[]>(infoLen);
                    glGetShaderInfoLog(newShader->AsRawGLHandle(), infoLen, nullptr, (GLchar*)infoLog.get());
                    Log(Verbose) << "Shader log: " << (GLchar*)infoLog.get() << std::endl;
                }
            }
        #endif

        uint64_t hashCode = DefaultSeed64;
        for (unsigned c=0; c<dimof(shaderSourcePointers); ++c)
            hashCode = Hash64(shaderSourcePointers[c], PtrAdd(shaderSourcePointers[c], shaderSourceLengths[c]), hashCode);
        {
            ScopedLock(s_compiledShadersLock);
            while (s_compiledShaders.find(hashCode) != s_compiledShaders.end())
                ++hashCode;     // uniquify this hash. There are some edge cases where we can end up compiling the same shader twice; it's better to tread safely here
            s_compiledShaders.emplace(std::make_pair(hashCode, std::move(newShader)));
        }

        struct OutputBlob
        {
            ShaderService::ShaderHeader _hdr;
            uint64_t _hashCode;
        };
        payload = std::shared_ptr<std::vector<uint8>>(
            new std::vector<uint8>(sizeof(OutputBlob)),
            [](std::vector<uint8>* obj) {
                if (!obj) return;
                OutputBlob& blob = *(OutputBlob*)obj->data();
                {
                    ScopedLock(s_compiledShadersLock);
                    s_compiledShaders.erase(blob._hashCode);
                }
                delete obj;
            });
        OutputBlob& output = *(OutputBlob*)payload->data();
        StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
        identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "-" << std::hex << hashCode;
        output._hdr = ShaderService::ShaderHeader { identifier.AsStringSection(), shaderPath._shaderModel, false };
        output._hashCode = hashCode;
        return true;
    }

    std::string OGLESShaderCompiler::MakeShaderMetricsString(const void* byteCode, size_t byteCodeSize) const { return std::string(); }

    OGLESShaderCompiler::OGLESShaderCompiler()
    {}
    OGLESShaderCompiler::~OGLESShaderCompiler()
    {}

    std::shared_ptr<ShaderService::ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice& device)
    {
        return std::make_shared<OGLESShaderCompiler>();
    }

    static uint32_t g_nextShaderProgramGUID = 0;

    static std::string GetProgramInfoLog(RawGLHandle program)
    {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            std::string buffer;
            buffer.resize(infoLen);
            glGetProgramInfoLog(program, infoLen, nullptr, (GLchar*)buffer.data());
            return buffer;
        }
        return {};
    }

    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
                                    const CompiledShaderByteCode& vertexShader,
                                    const CompiledShaderByteCode& fragmentShader)
    {
        auto vsByteCode = vertexShader.GetByteCode();
        auto fsByteCode = fragmentShader.GetByteCode();

        assert(vsByteCode.size() == sizeof(uint64_t) && fsByteCode.size() == sizeof(uint64_t));
        intrusive_ptr<OpenGL::Shader> vs, fs;
        {
            ScopedLock(OGLESShaderCompiler::s_compiledShadersLock);
            vs = OGLESShaderCompiler::s_compiledShaders[*(uint64_t*)vsByteCode.first];
            fs = OGLESShaderCompiler::s_compiledShaders[*(uint64_t*)fsByteCode.first];
            assert(vs && fs);
        }

        _depVal = std::make_shared<Assets::DependencyValidation>();
        Assets::RegisterAssetDependency(_depVal, vertexShader.GetDependencyValidation());
        Assets::RegisterAssetDependency(_depVal, fragmentShader.GetDependencyValidation());

        GLint vsType = 0, vsCompileStatus = 0;
        GLint fsType = 0, fsCompileStatus = 0;
        glGetShaderiv(vs->AsRawGLHandle(), GL_SHADER_TYPE, &vsType);
        glGetShaderiv(vs->AsRawGLHandle(), GL_COMPILE_STATUS, &vsCompileStatus);
        glGetShaderiv(fs->AsRawGLHandle(), GL_SHADER_TYPE, &fsType);
        glGetShaderiv(fs->AsRawGLHandle(), GL_COMPILE_STATUS, &fsCompileStatus);
        if (!vsCompileStatus || !fsCompileStatus)
            Throw(::Assets::Exceptions::ConstructionError(
                ::Assets::Exceptions::ConstructionError::Reason::Unknown,
                _depVal,
                "Cannot build shader program because either vs or fs has bad compile status"));
        if (vsType != GL_VERTEX_SHADER || fsType != GL_FRAGMENT_SHADER)
            Throw(::Assets::Exceptions::ConstructionError(
                ::Assets::Exceptions::ConstructionError::Reason::Unknown,
                _depVal,
                "Cannot build shader program because shader types don't match expected"));

        auto newProgramIndex = factory.CreateShaderProgram();
        glAttachShader  (newProgramIndex->AsRawGLHandle(), vs->AsRawGLHandle());
        glAttachShader  (newProgramIndex->AsRawGLHandle(), fs->AsRawGLHandle());
        glLinkProgram   (newProgramIndex->AsRawGLHandle());
        glDetachShader  (newProgramIndex->AsRawGLHandle(), fs->AsRawGLHandle());
        glDetachShader  (newProgramIndex->AsRawGLHandle(), vs->AsRawGLHandle());

        GLint linkStatus = 0;
        glGetProgramiv   (newProgramIndex->AsRawGLHandle(), GL_LINK_STATUS, &linkStatus);

        std::stringstream str;
        str << "[VS:" << vertexShader.GetIdentifier() << "][FS:" << fragmentShader.GetIdentifier() << "]";
        auto sourceIdentifiers = str.str();

        if (ObjectFactory::WriteObjectLabels() && (factory.GetFeatureSet() & FeatureSet::Flags::LabelObject))
            glLabelObjectEXT(GL_PROGRAM_OBJECT_EXT, newProgramIndex->AsRawGLHandle(), (GLsizei)sourceIdentifiers.length(), (const GLchar*)sourceIdentifiers.data());

        if (!linkStatus) {
            ::Assets::Blob errorsLog;

            {
                auto buffer = GetProgramInfoLog(newProgramIndex->AsRawGLHandle());

                std::stringstream str;
                str << "Failure while linking of shaders (" << vertexShader.GetIdentifier() << ") & (" << fragmentShader.GetIdentifier() << "): ";
                str << buffer;
                buffer = str.str();

                Log(Warning) << buffer << std::endl;
                errorsLog = std::make_shared<std::vector<uint8_t>>(buffer.size()+1);
                auto d = errorsLog->begin();
                for (auto s:buffer) *d++ = s;
                *d = '\0';
            }

            Throw(::Assets::Exceptions::ConstructionError(
                ::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
                _depVal,
                errorsLog));
        }

        #if defined(_DEBUG)
            _sourceIdentifiers = sourceIdentifiers;
        #endif

        _underlying = std::move(newProgramIndex);

        _guid = g_nextShaderProgramGUID++;
    }

    ShaderProgram::~ShaderProgram()
    {
    }

    std::ostream& operator<<(std::ostream& stream, const ShaderProgram& shaderProgram)
    {
        #if defined(_DEBUG)
            stream << "Program: " << shaderProgram._sourceIdentifiers << std::endl;
        #endif

        std::pair<const char*, GLenum> attributes[] = {
            {"GL_ACTIVE_ATTRIBUTES",                     GL_ACTIVE_ATTRIBUTES},
            {"GL_ACTIVE_ATTRIBUTE_MAX_LENGTH",           GL_ACTIVE_ATTRIBUTE_MAX_LENGTH},
            {"GL_ACTIVE_UNIFORM_BLOCKS",                 GL_ACTIVE_UNIFORM_BLOCKS},
            {"GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH",  GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH},
            {"GL_ACTIVE_UNIFORMS",                       GL_ACTIVE_UNIFORMS},
            {"GL_ACTIVE_UNIFORM_MAX_LENGTH",             GL_ACTIVE_UNIFORM_MAX_LENGTH},
            {"GL_ATTACHED_SHADERS",                      GL_ATTACHED_SHADERS},
            {"GL_DELETE_STATUS",                         GL_DELETE_STATUS},
            {"GL_INFO_LOG_LENGTH",                       GL_INFO_LOG_LENGTH},
            {"GL_LINK_STATUS",                           GL_LINK_STATUS},
            {"GL_PROGRAM_BINARY_RETRIEVABLE_HINT",       GL_PROGRAM_BINARY_RETRIEVABLE_HINT},
            {"GL_TRANSFORM_FEEDBACK_BUFFER_MODE",        GL_TRANSFORM_FEEDBACK_BUFFER_MODE},
            {"GL_TRANSFORM_FEEDBACK_VARYINGS",           GL_TRANSFORM_FEEDBACK_VARYINGS},
            {"GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH", GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH},
            {"GL_VALIDATE_STATUS",                       GL_VALIDATE_STATUS}
        };

        auto starterError = glGetError();
        if (starterError) {
            stream << "<<pending error before starting: " << GLenumAsString(starterError) << ">>" << std::endl;
        }

        for (auto a:attributes) {
            GLint value = 0;
            glGetProgramiv(shaderProgram.GetUnderlying()->AsRawGLHandle(), a.second, &value);
            auto err = glGetError();
            if (err) {
                stream << "Got GL error (" << err << ") while querying program attribute (" << a.first << ")" << std::endl;
            } else {
                stream << "[" << a.first << "] " << value << std::endl;
            }
        }

        stream << "Program info log: " << GetProgramInfoLog(shaderProgram.GetUnderlying()->AsRawGLHandle()) << std::endl;

        GLint attachedShaderCount = 0;
        glGetProgramiv(shaderProgram.GetUnderlying()->AsRawGLHandle(), GL_ATTACHED_SHADERS, &attachedShaderCount);
        if (attachedShaderCount) {
            GLuint attachedShaders[attachedShaderCount];
            glGetAttachedShaders(shaderProgram.GetUnderlying()->AsRawGLHandle(), attachedShaderCount, &attachedShaderCount, attachedShaders);
            stream << "Attached shaders: ";
            for (unsigned c=0; c<attachedShaderCount; ++c) {
                if (c != 0) stream << ", ";
                stream << attachedShaders[c];
            }
            stream << std::endl;
        }

        {
            GLint activeAttributeCount = 0, activeAttributeMaxLength = 0;
            glGetProgramiv(shaderProgram.GetUnderlying()->AsRawGLHandle(), GL_ACTIVE_ATTRIBUTES, &activeAttributeCount);
            glGetProgramiv(shaderProgram.GetUnderlying()->AsRawGLHandle(), GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &activeAttributeMaxLength);

            char buffer[activeAttributeMaxLength+1];
            stream << "Active attributes: " << std::endl;
            for (unsigned c=0; c<activeAttributeCount; ++c) {
                memset(buffer, 0, activeAttributeMaxLength+1);
                GLsizei length = 0;
                GLint size; GLenum type;
                glGetActiveAttrib(
                    shaderProgram.GetUnderlying()->AsRawGLHandle(),
                    c, activeAttributeMaxLength,
                    &length, &size, &type,
                    buffer);
                stream << "[" << c << "] " << GLenumAsString(type) << " " << buffer;
                if (size > 1)
                    stream << "(size: " << size << ")";
                stream << std::endl;
            }
        }

        {
            GLint activeUniformCount = 0, activeUniformMaxCount = 0;
            glGetProgramiv(shaderProgram.GetUnderlying()->AsRawGLHandle(), GL_ACTIVE_UNIFORMS, &activeUniformCount);
            glGetProgramiv(shaderProgram.GetUnderlying()->AsRawGLHandle(), GL_ACTIVE_UNIFORM_MAX_LENGTH, &activeUniformMaxCount);

            char buffer[activeUniformMaxCount+1];
            stream << "Active uniforms: " << std::endl;
            for (unsigned c=0; c<activeUniformCount; ++c) {
                memset(buffer, 0, activeUniformMaxCount + 1);
                GLsizei length = 0;
                GLint size; GLenum type;
                glGetActiveUniform(
                    shaderProgram.GetUnderlying()->AsRawGLHandle(),
                    c, activeUniformMaxCount,
                    &length, &size, &type,
                    buffer);
                stream << "[" << c << "] " << GLenumAsString(type) << " " << buffer;
                if (size > 1)
                    stream << "(size: " << size << ")";

                stream << " Locations: ";
                if (size > 1) {
                    char* bracket = strchr(buffer, '[');
                    if (bracket) *bracket = '\0';
                    for (unsigned e=0; e<size; ++e) {
                        std::stringstream str;
                        str << buffer << "[" << e << "]";
                        auto location = glGetUniformLocation(shaderProgram.GetUnderlying()->AsRawGLHandle(), str.str().c_str());
                        if (e!=0) stream << ", ";
                        stream << location;
                    }
                } else {
                    auto location = glGetUniformLocation(shaderProgram.GetUnderlying()->AsRawGLHandle(), buffer);
                    stream << location;
                }
                stream << std::endl;
            }
        }

        return stream;
    }

    void DestroyGLESCachedShaders()
    {
        try {
            ScopedLock(OGLESShaderCompiler::s_compiledShadersLock);
            decltype(OGLESShaderCompiler::s_compiledShaders)().swap(OGLESShaderCompiler::s_compiledShaders);
        } catch (const std::system_error&) {
            // suppress a system error here, which can sometimes happen due to shutdown order issues
            // (if the mutex has been destroyed before this is called)
        }
    }

}}

