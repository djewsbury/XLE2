// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NascentTransformationMachine.h"
#include "../RenderCore/Assets/TransformationCommands.h"		// (for TransformationParameterSet)
#include "../RenderCore/Assets/AnimationScaffoldInternal.h"
#include <vector>
#include <string>

namespace Serialization { class NascentBlockSerializer; }
namespace RenderCore { namespace Assets { class RawAnimationCurve; }}
namespace RenderCore { enum class Format : int; }
namespace Utility { class OutputStream; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
    class NascentSkeleton;
	class NascentSkeletonMachine;

        //
        //      "NascentAnimationSet" is a set of animations
        //      and some information to bind these animations to
        //      a skeleton
        //
	
    class NascentAnimationSet
    {
    public:
		using AnimationDriver = RenderCore::Assets::AnimationSet::AnimationDriver;
		using ConstantDriver = RenderCore::Assets::AnimationSet::ConstantDriver;
		using Animation = RenderCore::Assets::AnimationSet::Animation;

        void    AddAnimationDriver(
            const std::string&  parameterName, 
            unsigned            curveId, 
            AnimSamplerType     samplerType, 
            unsigned            samplerOffset);

        void    AddConstantDriver(  
            const std::string&  parameterName, 
            const void*         constantValue,
			size_t				constantValueSize,
			Format				format,
            AnimSamplerType     samplerType, 
            unsigned            samplerOffset);

        bool    HasAnimationDriver(const std::string&  parameterName) const;

        void    MergeAnimation(
            const NascentAnimationSet& animation, const std::string& name,
            IteratorRange<const Assets::RawAnimationCurve*> sourceCurves, 
			SerializableVector<Assets::RawAnimationCurve>& destinationCurves);

		void	MakeIndividualAnimation(const std::string& name, IteratorRange<const RawAnimationCurve*> curves);

		void	AddAnimation(
			const std::string& name, 
			unsigned driverBegin, unsigned driverEnd,
			unsigned constantBegin, unsigned constantEnd,
			float minTime, float maxTime);

		IteratorRange<const AnimationDriver*> GetAnimationDrivers() const { return MakeIteratorRange(_animationDrivers); }
		IteratorRange<const ConstantDriver*> GetConstantDrivers() const { return MakeIteratorRange(_constantDrivers); }

		friend std::ostream& StreamOperator(
			std::ostream& stream, 
			const NascentAnimationSet& transMachine);

        void            Serialize(Serialization::NascentBlockSerializer& serializer) const;
    private:
        std::vector<AnimationDriver>    _animationDrivers;
        std::vector<ConstantDriver>     _constantDrivers;
        std::vector<std::pair<std::string, Animation>>          _animations;
        std::vector<std::string>        _parameterInterfaceDefinition;
        std::vector<uint8>              _constantData;
    };

        //
        //      "NascentSkeleton" represents the skeleton information for an 
        //      object. Usually this is mostly just the transformation machine.
        //      But we also need some binding information for binding the output
        //      matrices of the transformation machine to joints.
        //

    class NascentSkeleton
    {
    public:
        NascentSkeletonMachine&				GetSkeletonMachine()			{ return _skeletonMachine; }
        const NascentSkeletonMachine&		GetSkeletonMachine() const		{ return _skeletonMachine; }
		NascentSkeletonInterface&			GetInterface()					{ return _interface; }
		const NascentSkeletonInterface&		GetInterface() const			{ return _interface; }
		TransformationParameterSet&         GetDefaultParameters()			{ return _defaultParameters; }
		const TransformationParameterSet&   GetDefaultParameters() const	{ return _defaultParameters; }

		void	FilterOutputInterface(IteratorRange<const std::pair<std::string, std::string>*> filterIn);
		void	Serialize(Serialization::NascentBlockSerializer& serializer) const;

    private:
        NascentSkeletonMachine		_skeletonMachine;
		NascentSkeletonInterface	_interface;
		TransformationParameterSet	_defaultParameters;
    };

        //
        //      "Model Command Stream" is the list of model elements
        //      from a single export. Usually it will be mostly geometry
        //      and skin controller elements.
        //

    class NascentModelCommandStream
    {
    public:
        typedef uint64 MaterialGuid;
        static const MaterialGuid s_materialGuid_Invalid = ~0x0ull;

            /////   G E O M E T R Y   I N S T A N C E   /////
        class GeometryInstance
        {
        public:
            unsigned                    _id = ~0u;
            unsigned                    _localToWorldId = ~0u;
            std::vector<MaterialGuid>   _materials;
            unsigned                    _levelOfDetail = ~0u;

            void Serialize(Serialization::NascentBlockSerializer& serializer) const;
        };

            /////   S K I N   C O N T R O L L E R   I N S T A N C E   /////
        class SkinControllerInstance
        {
        public:
            unsigned                    _id = ~0u;
            unsigned                    _localToWorldId = ~0u;
            std::vector<MaterialGuid>   _materials;
            unsigned                    _levelOfDetail = ~0u;

            void Serialize(Serialization::NascentBlockSerializer& serializer) const;
        };

            /////   C A M E R A   I N S T A N C E   /////
        class CameraInstance
        {
        public:
            unsigned    _localToWorldId = ~0u;
        };

        void Add(GeometryInstance&& geoInstance);
        void Add(CameraInstance&& geoInstance);
        void Add(SkinControllerInstance&& geoInstance);

        bool IsEmpty() const { return _geometryInstances.empty() && _cameraInstances.empty() && _skinControllerInstances.empty(); }
        void Serialize(Serialization::NascentBlockSerializer& serializer) const;

        unsigned RegisterInputInterfaceMarker(const std::string& skeletonName, const std::string& name);

        std::vector<uint64_t> BuildHashedInputInterface() const;
        unsigned GetMaxLOD() const;

        std::vector<GeometryInstance>               _geometryInstances;
        std::vector<CameraInstance>                 _cameraInstances;
        std::vector<SkinControllerInstance>         _skinControllerInstances;

        friend std::ostream& operator<<(std::ostream&, const NascentModelCommandStream&);

    private:
        std::vector<std::pair<std::string, std::string>>	_inputInterfaceNames;
    };
}}}


