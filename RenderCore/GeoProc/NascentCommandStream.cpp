// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentCommandStream.h"
#include "SkeletonRegistry.h"
#include "../Format.h"
#include "../Assets/RawAnimationCurve.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/AssetsCore.h"
#include "../../ConsoleRig/OutputStream.h"
#include "../../ConsoleRig/Log.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{ 
    class NascentAnimationSet::Animation
    {
    public:
        std::string     _name;
        unsigned        _begin, _end;
        unsigned        _constantBegin, _constantEnd;
        float           _startTime, _endTime;
        Animation() : _begin(0), _end(0), _constantBegin(0), _constantEnd(0), _startTime(0.f), _endTime(0.f) {}
        Animation(const std::string& name, unsigned begin, unsigned end, unsigned constantBegin, unsigned constantEnd, float startTime, float endTime) 
            : _name(name), _begin(begin), _end(end), _constantBegin(constantBegin), _constantEnd(constantEnd), _startTime(startTime), _endTime(endTime) {}
    };

    void    NascentAnimationSet::AddConstantDriver( 
                                    const std::string&  parameterName, 
                                    const void*         constantValue, 
									size_t				valueSize,
									Format				format,
                                    AnimSamplerType     samplerType, 
                                    unsigned            samplerOffset)
    {
        size_t parameterIndex = _parameterInterfaceDefinition.size();
        auto i = std::find( _parameterInterfaceDefinition.cbegin(), 
                            _parameterInterfaceDefinition.cend(), parameterName);
        if (i!=_parameterInterfaceDefinition.end()) {
            parameterIndex = (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i);
        } else {
            _parameterInterfaceDefinition.push_back(parameterName);
        }

		// Expecting a single value -- it should match the bits per pixel value
		// associated with the given format
		assert(unsigned(valueSize) == RenderCore::BitsPerPixel(format)/8);

        unsigned dataOffset = unsigned(_constantData.size());
        std::copy(
            (uint8*)constantValue, PtrAdd((uint8*)constantValue, valueSize),
            std::back_inserter(_constantData));

        _constantDrivers.push_back(
            ConstantDriver(dataOffset, (unsigned)parameterIndex, format, samplerType, samplerOffset));
    }

    void    NascentAnimationSet::AddAnimationDriver( 
        const std::string& parameterName, 
        unsigned curveId, 
        AnimSamplerType samplerType, unsigned samplerOffset)
    {
        size_t parameterIndex = _parameterInterfaceDefinition.size();
        auto i = std::find( _parameterInterfaceDefinition.cbegin(), 
                            _parameterInterfaceDefinition.cend(), parameterName);
        if (i!=_parameterInterfaceDefinition.end()) {
            parameterIndex = (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i);
        } else {
            _parameterInterfaceDefinition.push_back(parameterName);
        }

        _animationDrivers.push_back(
            AnimationDriver(curveId, (unsigned)parameterIndex, samplerType, samplerOffset));
    }

    bool    NascentAnimationSet::HasAnimationDriver(const std::string&  parameterName) const
    {
        auto i2 = std::find(_parameterInterfaceDefinition.cbegin(), 
                            _parameterInterfaceDefinition.cend(), parameterName);
        if (i2==_parameterInterfaceDefinition.end()) 
            return false;

        auto parameterIndex = (unsigned)std::distance(_parameterInterfaceDefinition.cbegin(), i2);

        for (auto i=_animationDrivers.begin(); i!=_animationDrivers.end(); ++i) {
            if (i->_parameterIndex == parameterIndex)
                return true;
        }

        for (auto i=_constantDrivers.begin(); i!=_constantDrivers.end(); ++i) {
            if (i->_parameterIndex == parameterIndex)
                return true;
        }
        return false;
    }

    void    NascentAnimationSet::MergeAnimation(
        const NascentAnimationSet& animation, const std::string& name,
        const std::vector<Assets::RawAnimationCurve>& sourceCurves, 
        std::vector<Assets::RawAnimationCurve>& destinationCurves)
    {
            //
            //      Merge the animation drivers in the given input animation, and give 
            //      them the supplied name
            //
        float minTime = FLT_MAX, maxTime = -FLT_MAX;
        size_t startIndex = _animationDrivers.size();
        size_t constantStartIndex = _constantDrivers.size();
        for (auto i=animation._animationDrivers.cbegin(); i!=animation._animationDrivers.end(); ++i) {
            if (i->_curveIndex >= sourceCurves.size()) continue;
            const auto* animCurve = &sourceCurves[i->_curveIndex];
            if (animCurve) {
                float curveStart = animCurve->StartTime();
                float curveEnd = animCurve->EndTime();
                minTime = std::min(minTime, curveStart);
                maxTime = std::max(maxTime, curveEnd);

                const std::string& pname = animation._parameterInterfaceDefinition[i->_parameterIndex];
                destinationCurves.push_back(Assets::RawAnimationCurve(*animCurve));
                AddAnimationDriver(
                    pname, unsigned(destinationCurves.size()-1), 
                    i->_samplerType, i->_samplerOffset);
            }
        }

        for (auto i=animation._constantDrivers.cbegin(); i!=animation._constantDrivers.end(); ++i) {
            const std::string& pname = animation._parameterInterfaceDefinition[i->_parameterIndex];
            AddConstantDriver(
				pname, PtrAdd(AsPointer(animation._constantData.begin()), i->_dataOffset), 
				BitsPerPixel(i->_format), i->_format,
				i->_samplerType, i->_samplerOffset);
        }

        _animations.push_back(Animation(name, 
            (unsigned)startIndex, (unsigned)_animationDrivers.size(), 
            (unsigned)constantStartIndex, (unsigned)_constantDrivers.size(),
            minTime, maxTime));
    }

	void	NascentAnimationSet::MakeIndividualAnimation(const std::string& name, IteratorRange<const RawAnimationCurve*> curves)
	{
		// Make an Animation record that covers all of the curves registered.
		// This is intended for cases where there's only a single animation within the NascentAnimationSet
		float minTime = FLT_MAX, maxTime = -FLT_MAX;
		for (auto i=_animationDrivers.cbegin(); i!=_animationDrivers.end(); ++i) {
            if (i->_curveIndex >= curves.size()) continue;
            const auto* animCurve = &curves[i->_curveIndex];
            if (animCurve) {
                float curveStart = animCurve->StartTime();
                float curveEnd = animCurve->EndTime();
                minTime = std::min(minTime, curveStart);
                maxTime = std::max(maxTime, curveEnd);
			}
		}

		_animations.push_back(Animation(name, 
            (unsigned)0, (unsigned)_animationDrivers.size(), 
            (unsigned)0, (unsigned)_constantDrivers.size(),
            minTime, maxTime));
	}

    void NascentAnimationSet::AnimationDriver::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        ::Serialize(serializer, unsigned(_curveIndex));
        ::Serialize(serializer, _parameterIndex);
        ::Serialize(serializer, _samplerOffset);
        ::Serialize(serializer, unsigned(_samplerType));
    }

    void NascentAnimationSet::ConstantDriver::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        ::Serialize(serializer, _dataOffset);
        ::Serialize(serializer, _parameterIndex);
        ::Serialize(serializer, _samplerOffset);
        ::Serialize(serializer, unsigned(_samplerType));
    }

    struct AnimationDesc        // matches AnimationSet::Animation
    {
        uint64      _name;
        unsigned    _beginDriver, _endDriver;
        unsigned    _beginConstantDriver, _endConstantDriver;
        float       _beginTime, _endTime; 

        AnimationDesc() {}
        void Serialize(Serialization::NascentBlockSerializer& serializer) const
        {
            ::Serialize(serializer, _name);
            ::Serialize(serializer, _beginDriver);
            ::Serialize(serializer, _endDriver);
            ::Serialize(serializer, _beginConstantDriver);
            ::Serialize(serializer, _endConstantDriver);
            ::Serialize(serializer, _beginTime);
            ::Serialize(serializer, _endTime);
        }
    };

    struct CompareAnimationName
    {
        bool operator()(const AnimationDesc& lhs, const AnimationDesc& rhs) const   { return lhs._name < rhs._name; }
        bool operator()(const AnimationDesc& lhs, uint64 rhs) const                 { return lhs._name < rhs; }
        bool operator()(uint64 lhs, const AnimationDesc& rhs) const                 { return lhs < rhs._name; }
    };

    void NascentAnimationSet::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        serializer.SerializeSubBlock(MakeIteratorRange(_animationDrivers));
        serializer.SerializeValue(_animationDrivers.size());
        serializer.SerializeSubBlock(MakeIteratorRange(_constantDrivers));
        serializer.SerializeValue(_constantDrivers.size());
        serializer.SerializeSubBlock(MakeIteratorRange(_constantData));

            //      List of animations...

        auto outputAnimations = std::make_unique<AnimationDesc[]>(_animations.size());
        for (size_t c=0; c<_animations.size(); ++c) {
            AnimationDesc&o = outputAnimations[c];
            const Animation&i = _animations[c];
            o._name = Hash64(AsPointer(i._name.begin()), AsPointer(i._name.end()));
            o._beginDriver = i._begin; o._endDriver = i._end;
            o._beginConstantDriver = i._constantBegin; o._endConstantDriver = i._constantEnd;
            o._beginTime = i._startTime; o._endTime = i._endTime;
        }
        std::sort(outputAnimations.get(), &outputAnimations[_animations.size()], CompareAnimationName());
        serializer.SerializeSubBlock(MakeIteratorRange((const AnimationDesc*)outputAnimations.get(), (const AnimationDesc*)&outputAnimations[_animations.size()]));
        serializer.SerializeValue(_animations.size());

            //      Output interface...

        ConsoleRig::DebuggerOnlyWarning("Animation set output interface:\n");
        auto parameterNameHashes = std::make_unique<uint64[]>(_parameterInterfaceDefinition.size());
        for (size_t c=0; c<_parameterInterfaceDefinition.size(); ++c) {
            ConsoleRig::DebuggerOnlyWarning("  [%i] %s\n", c, _parameterInterfaceDefinition[c].c_str());
            parameterNameHashes[c] = Hash64(AsPointer(_parameterInterfaceDefinition[c].begin()), AsPointer(_parameterInterfaceDefinition[c].end()));
        }
        serializer.SerializeSubBlock(MakeIteratorRange(parameterNameHashes.get(), &parameterNameHashes[_parameterInterfaceDefinition.size()]));
        serializer.SerializeValue(_parameterInterfaceDefinition.size());
    }

	std::ostream& StreamOperator(
		std::ostream& stream, 
		const NascentAnimationSet& animSet)
	{
		// write out some metrics / debugging information
		stream << "--- Output animation parameters (" << animSet._parameterInterfaceDefinition.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._parameterInterfaceDefinition.size(); ++c)
			stream << "[" << c << "] " << animSet._parameterInterfaceDefinition[c] << std::endl;

		stream << "--- Animations (" << animSet._animations.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._animations.size(); ++c) {
			auto& anim = animSet._animations[c];
			stream << "[" << c << "] " << anim._name << " " << anim._startTime << " to " << anim._endTime << std::endl;
		}

		stream << "--- Animations drivers (" << animSet._animationDrivers.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._animationDrivers.size(); ++c) {
			auto& driver = animSet._animationDrivers[c];
			stream << "[" << c << "] Curve index: " << driver._curveIndex << " Parameter index: " << driver._parameterIndex << " (" << animSet._parameterInterfaceDefinition[driver._parameterIndex] << ") with sampler: " << AsString(driver._samplerType) << " and sampler offset " << driver._samplerOffset << std::endl;
		}

		stream << "--- Constant drivers (" << animSet._constantDrivers.size() << ")" << std::endl;
		for (unsigned c=0; c<animSet._constantDrivers.size(); ++c) {
			auto& driver = animSet._constantDrivers[c];
			stream << "[" << c << "] Parameter index: " << driver._parameterIndex << " (" << animSet._parameterInterfaceDefinition[driver._parameterIndex] << ") with sampler: " << AsString(driver._samplerType) << " and sampler offset " << driver._samplerOffset << std::endl;
		}

		return stream;
	}

    NascentAnimationSet::NascentAnimationSet() {}
    NascentAnimationSet::~NascentAnimationSet() {}
    NascentAnimationSet::NascentAnimationSet(NascentAnimationSet&& moveFrom)
    :   _animationDrivers(std::move(moveFrom._animationDrivers))
    ,   _constantDrivers(std::move(moveFrom._constantDrivers))
    ,   _animations(std::move(moveFrom._animations))
    ,   _parameterInterfaceDefinition(std::move(moveFrom._parameterInterfaceDefinition))
    ,   _constantData(std::move(moveFrom._constantData))
    {}
    NascentAnimationSet& NascentAnimationSet::operator=(NascentAnimationSet&& moveFrom)
    {
        _animationDrivers = std::move(moveFrom._animationDrivers);
        _constantDrivers = std::move(moveFrom._constantDrivers);
        _animations = std::move(moveFrom._animations);
        _parameterInterfaceDefinition = std::move(moveFrom._parameterInterfaceDefinition);
        _constantData = std::move(moveFrom._constantData);
        return *this;
    }





    void NascentSkeleton::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        ::Serialize(serializer, _skeletonMachine);
		::Serialize(serializer, _interface);
		::Serialize(serializer, _defaultParameters);
    }

    NascentSkeleton::NascentSkeleton() {}
    NascentSkeleton::~NascentSkeleton() {}




    unsigned NascentModelCommandStream::RegisterInputInterfaceMarker(const std::string& name)
    {
		auto hash = Hash64(name);
		auto existing = std::find(_inputInterface.begin(), _inputInterface.end(), hash);
		if (existing != _inputInterface.end()) {
			assert(_inputInterfaceNames[std::distance(_inputInterface.begin(), existing)] == name);
			return (unsigned)std::distance(_inputInterface.begin(), existing);
		}

        auto result = (unsigned)_inputInterfaceNames.size();
		_inputInterface.push_back(hash);
		_inputInterfaceNames.push_back(name);
		return result;
    }

    void NascentModelCommandStream::Add(GeometryInstance&& geoInstance)
    {
        _geometryInstances.emplace_back(std::move(geoInstance));
    }

    void NascentModelCommandStream::Add(CameraInstance&& camInstance)
    {
        _cameraInstances.emplace_back(std::move(camInstance));
    }

    void NascentModelCommandStream::Add(SkinControllerInstance&& skinControllerInstance)
    {
        _skinControllerInstances.emplace_back(std::move(skinControllerInstance));
    }

    NascentModelCommandStream::NascentModelCommandStream() {}
    NascentModelCommandStream::~NascentModelCommandStream() {}


    void NascentModelCommandStream::GeometryInstance::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        ::Serialize(serializer, _id);
        ::Serialize(serializer, _localToWorldId);
        serializer.SerializeSubBlock(MakeIteratorRange(_materials));
        ::Serialize(serializer, _materials.size());
        ::Serialize(serializer, _levelOfDetail);
    }

    void NascentModelCommandStream::SkinControllerInstance::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        ::Serialize(serializer, _id);
        ::Serialize(serializer, _localToWorldId);
        serializer.SerializeSubBlock(MakeIteratorRange(_materials));
        ::Serialize(serializer, _materials.size());
        ::Serialize(serializer, _levelOfDetail);
    }

    void NascentModelCommandStream::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        serializer.SerializeSubBlock(MakeIteratorRange(_geometryInstances));
        serializer.SerializeValue(_geometryInstances.size());
        serializer.SerializeSubBlock(MakeIteratorRange(_skinControllerInstances));
        serializer.SerializeValue(_skinControllerInstances.size());
            
            //
            //      Turn our list of input matrices into hash values, and write out the
            //      run-time input interface definition...
            //
        ConsoleRig::DebuggerOnlyWarning("Command stream input interface:\n");
        serializer.SerializeSubBlock(MakeIteratorRange(_inputInterface));
        serializer.SerializeValue(_inputInterface.size());
    }

    std::vector<uint64> NascentModelCommandStream::GetInputInterface() const { return _inputInterface; }

    unsigned NascentModelCommandStream::GetMaxLOD() const
    {
        unsigned maxLOD = 0u;
        for (const auto&i:_geometryInstances) maxLOD = std::max(i._levelOfDetail, maxLOD);
        for (const auto&i:_skinControllerInstances) maxLOD = std::max(i._levelOfDetail, maxLOD);
        return maxLOD;
    }

    std::ostream& operator<<(std::ostream& stream, const NascentModelCommandStream& cmdStream)
    {
        stream << " --- Geometry instances:" << std::endl;
        unsigned c=0;
        for (const auto& i:cmdStream._geometryInstances) {
            stream << "  [" << c++ << "] GeoId: " << i._id << " Transform: " << i._localToWorldId << " LOD: " << i._levelOfDetail << std::endl;
            stream << "     Materials: " << std::hex;
            for (size_t q=0; q<i._materials.size(); ++q) {
                if (q != 0) stream << ", ";
                stream << i._materials[q];
            }
            stream << std::dec << std::endl;
        }

        stream << " --- Skin controller instances:" << std::endl;
        c=0;
        for (const auto& i:cmdStream._skinControllerInstances) {
            stream << "  [" << c++ << "] GeoId: " << i._id << " Transform: " << i._localToWorldId << std::endl;
            stream << "     Materials: " << std::hex;
            for (size_t q=0; q<i._materials.size(); ++q) {
                if (q != 0) stream << ", ";
                stream << i._materials[q];
            }
            stream << std::dec << std::endl;
        }

        stream << " --- Camera instances:" << std::endl;
        c=0;
        for (const auto& i:cmdStream._cameraInstances)
            stream << "  [" << c++ << "] Transform: " << i._localToWorldId << std::endl;

        stream << " --- Input interface:" << std::endl;
        c=0;
        for (const auto& i:cmdStream._inputInterfaceNames)
            stream << "  [" << c++ << "] " << i << std::endl;
        return stream;
    }

	void RegisterNodeBindingNames(
		NascentSkeleton& skeleton,
		const SkeletonRegistry& registry)
	{
		for (const auto& nodeDesc:registry.GetImportantNodes()) {
			uint32 outputMarker = 0u;
			auto success = skeleton.GetInterface().TryRegisterJointName(
				outputMarker, MakeStringSection(nodeDesc._bindingName));
			if (!success)
				Log(Warning) << "Found possible duplicate joint name in transformation machine: " << nodeDesc._bindingName << std::endl;
		}
	}

	void RegisterNodeBindingNames(
		NascentModelCommandStream& stream,
		const SkeletonRegistry& registry)
	{
		for (const auto& nodeDesc:registry.GetImportantNodes())
			stream.RegisterInputInterfaceMarker(nodeDesc._bindingName);
	}
}}}
