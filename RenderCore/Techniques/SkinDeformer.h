// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleModelDeform.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/SkeletonScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../Format.h"
#include "../Techniques/SimpleModelDeform.h"
#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { class VertexElementIterator; }

namespace RenderCore { namespace Techniques
{
	class SkinDeformer : public IDeformOperation
	{
	public:
		virtual void Execute(
			IteratorRange<const VertexElementRange*> sourceElements,
			IteratorRange<const VertexElementRange*> destinationElements) const;

		void FeedInSkeletonMachineResults(
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface);
		
		SkinDeformer(
			const RenderCore::Assets::ModelScaffold& modelScaffold,
			unsigned geoId);
		~SkinDeformer();

		static std::vector<RenderCore::Techniques::DeformOperationInstantiation> InstantiationFunction(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold);
	private:
		std::vector<float>		_jointWeights;
		std::vector<unsigned>	_jointIndices;
		size_t					_influencesPerVertex;

		RenderCore::Assets::ModelCommandStream::InputInterface _jointInputInterface;

		struct Section
		{
			IteratorRange<const RenderCore::Assets::DrawCallDesc*> _preskinningDrawCalls;
			IteratorRange<const Float4x4*> _bindShapeByInverseBindMatrices;
			IteratorRange<const uint16_t*> _jointMatrices;
		};
		std::vector<Section> _sections;

		Float4x4 _bindShapeMatrix;
		std::vector<Float4x4> _skeletonMachineOutput;
		RenderCore::Assets::SkeletonBinding _skeletonBinding;

		void WriteJointTransforms(
			const Section& section,
			IteratorRange<Float3x4*>		destination,
			IteratorRange<const Float4x4*>	skeletonMachineResult) const;
	};
}}

