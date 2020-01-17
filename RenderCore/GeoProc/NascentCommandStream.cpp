// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NascentCommandStream.h"
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

        _constantDrivers.push_back({dataOffset, (unsigned)parameterIndex, format, samplerType, samplerOffset});
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

        _animationDrivers.push_back({curveId, (unsigned)parameterIndex, samplerType, samplerOffset});
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
        const NascentAnimationSet& animation, const std::string& name)
    {
            //
            //      Merge the animation drivers in the given input animation, and give 
            //      them the supplied name
            //
        float minTime = FLT_MAX, maxTime = -FLT_MAX;
        size_t startIndex = _animationDrivers.size();
        size_t constantStartIndex = _constantDrivers.size();
        for (auto i=animation._animationDrivers.cbegin(); i!=animation._animationDrivers.end(); ++i) {
            if (i->_curveIndex >= animation._curves.size()) continue;
            const auto* animCurve = &animation._curves[i->_curveIndex];
            if (animCurve) {
                float curveStart = animCurve->StartTime();
                float curveEnd = animCurve->EndTime();
                minTime = std::min(minTime, curveStart);
                maxTime = std::max(maxTime, curveEnd);

                const std::string& pname = animation._parameterInterfaceDefinition[i->_parameterIndex];
                _curves.emplace_back(Assets::RawAnimationCurve(*animCurve));
                AddAnimationDriver(
                    pname, unsigned(_curves.size()-1), 
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

        _animations.push_back(
			std::make_pair(
				name,
				Animation{
					(unsigned)startIndex, (unsigned)_animationDrivers.size(), 
					(unsigned)constantStartIndex, (unsigned)_constantDrivers.size(),
					minTime, maxTime}));
    }

	void	NascentAnimationSet::MakeIndividualAnimation(const std::string& name)
	{
		// Make an Animation record that covers all of the curves registered.
		// This is intended for cases where there's only a single animation within the NascentAnimationSet
		float minTime = FLT_MAX, maxTime = -FLT_MAX;
		for (auto i=_animationDrivers.cbegin(); i!=_animationDrivers.end(); ++i) {
            if (i->_curveIndex >= _curves.size()) continue;
            const auto* animCurve = &_curves[i->_curveIndex];
            if (animCurve) {
                float curveStart = animCurve->StartTime();
                float curveEnd = animCurve->EndTime();
                minTime = std::min(minTime, curveStart);
                maxTime = std::max(maxTime, curveEnd);
			}
		}

		_animations.push_back(
			std::make_pair(
				name,
				Animation{
					(unsigned)0, (unsigned)_animationDrivers.size(), 
					(unsigned)0, (unsigned)_constantDrivers.size(),
					minTime, maxTime}));
	}

	void	NascentAnimationSet::AddAnimation(
			const std::string& name, 
			unsigned driverBegin, unsigned driverEnd,
			unsigned constantBegin, unsigned constantEnd,
			float minTime, float maxTime)
	{
		_animations.push_back(
			std::make_pair(
				name,
				Animation{
					driverBegin, driverEnd, 
					constantBegin, constantEnd,
					minTime, maxTime}));
	}

	unsigned NascentAnimationSet::AddCurve(RenderCore::Assets::RawAnimationCurve&& curve)
	{
		auto result = (unsigned)_curves.size();
		_curves.emplace_back(std::move(curve));
		return result;
	}

    void NascentAnimationSet::SerializeMethod(Serialization::NascentBlockSerializer& serializer) const
    {
		AnimationSet finalAnimationSet;
		finalAnimationSet._animationDrivers.insert(finalAnimationSet._animationDrivers.begin(), _animationDrivers.begin(), _animationDrivers.end());
		finalAnimationSet._constantDrivers.insert(finalAnimationSet._constantDrivers.begin(), _constantDrivers.begin(), _constantDrivers.end());

		finalAnimationSet._animations.reserve(_animations.size());
		for (const auto&a:_animations)
			finalAnimationSet._animations.push_back(std::make_pair(Hash64(a.first), a.second));
		std::sort(finalAnimationSet._animations.begin(), finalAnimationSet._animations.end(), CompareFirst<uint64_t, Animation>());

		finalAnimationSet._outputInterface.reserve(_parameterInterfaceDefinition.size());
		for (const auto&p:_parameterInterfaceDefinition)
			finalAnimationSet._outputInterface.push_back(Hash64(p));

		finalAnimationSet._constantData.insert(finalAnimationSet._constantData.begin(), _constantData.begin(), _constantData.end());

		// Construct the string name block (note that we have write the names in their final sorted order)
		for (const auto&a:finalAnimationSet._animations) {
			std::string srcName;
			for (const auto&src:_animations)
				if (a.first == Hash64(src.first)) {
					srcName = src.first;
					break;
				}
			finalAnimationSet._stringNameBlockOffsets.push_back((unsigned)finalAnimationSet._stringNameBlock.size());
			finalAnimationSet._stringNameBlock.insert(finalAnimationSet._stringNameBlock.end(), srcName.begin(), srcName.end());
		}
		finalAnimationSet._stringNameBlockOffsets.push_back((unsigned)finalAnimationSet._stringNameBlock.size());
		
		Serialize(serializer, finalAnimationSet);

		Serialize(serializer, _curves);
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
			stream << "[" << c << "] " << anim.first << " " << anim.second._beginTime << " to " << anim.second._endTime << std::endl;
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





    void NascentSkeleton::SerializeMethod(Serialization::NascentBlockSerializer& serializer) const
    {
        Serialize(serializer, _skeletonMachine);
		Serialize(serializer, _defaultParameters);
    }





    unsigned NascentModelCommandStream::RegisterInputInterfaceMarker(const std::string& skeleton, const std::string& name)
    {
		auto j = std::make_pair(skeleton, name);
		auto existing = std::find(_inputInterfaceNames.begin(), _inputInterfaceNames.end(), j);
		if (existing != _inputInterfaceNames.end()) {
			return (unsigned)std::distance(_inputInterfaceNames.begin(), existing);
		}

        auto result = (unsigned)_inputInterfaceNames.size();
		_inputInterfaceNames.push_back({skeleton, name});
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

    void NascentModelCommandStream::GeometryInstance::SerializeMethod(Serialization::NascentBlockSerializer& serializer) const
    {
        Serialize(serializer, _id);
        Serialize(serializer, _localToWorldId);
        serializer.SerializeSubBlock(MakeIteratorRange(_materials));
        Serialize(serializer, _materials.size());
        Serialize(serializer, _levelOfDetail);
    }

    void NascentModelCommandStream::SkinControllerInstance::SerializeMethod(Serialization::NascentBlockSerializer& serializer) const
    {
        Serialize(serializer, _id);
        Serialize(serializer, _localToWorldId);
        serializer.SerializeSubBlock(MakeIteratorRange(_materials));
        Serialize(serializer, _materials.size());
        Serialize(serializer, _levelOfDetail);
    }

    void NascentModelCommandStream::SerializeMethod(Serialization::NascentBlockSerializer& serializer) const
    {
        serializer.SerializeSubBlock(MakeIteratorRange(_geometryInstances));
        serializer.SerializeValue(_geometryInstances.size());
        serializer.SerializeSubBlock(MakeIteratorRange(_skinControllerInstances));
        serializer.SerializeValue(_skinControllerInstances.size());
            
            //
            //      Turn our list of input matrices into hash values, and write out the
            //      run-time input interface definition...
            //
		auto hashedInterface = BuildHashedInputInterface();
        serializer.SerializeSubBlock(MakeIteratorRange(hashedInterface));
        serializer.SerializeValue(hashedInterface.size());
    }

	std::vector<uint64_t> NascentModelCommandStream::BuildHashedInputInterface() const
	{
		std::vector<uint64_t> hashedInterface;
		hashedInterface.reserve(_inputInterfaceNames.size());
		for (const auto&j:_inputInterfaceNames) hashedInterface.push_back(HashCombine(Hash64(j.first), Hash64(j.second)));
		return hashedInterface;
	}

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
            stream << "  [" << c++ << "] " << i.first << " : " << i.second  << std::endl;
        return stream;
    }

}}}
