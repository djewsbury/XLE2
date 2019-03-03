// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlacementsQuadTree.h"
#include "../Math/ProjectionMath.h"
#include "../Utility/PtrUtils.h"
#include "../Core/Prefix.h"
#include <stack>

#include "PlacementsQuadTreeDebugger.h"
#include "PlacementsManager.h"
#include "../RenderOverlays/IOverlayContext.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../ConsoleRig/Console.h"

namespace SceneEngine
{
    using namespace RenderCore;

    class PlacementsQuadTree::Pimpl
    {
    public:
        class Node
        {
        public:
            BoundingBox     _boundary;
            unsigned        _payloadID;
            unsigned        _treeDepth;
            unsigned        _children[4];
        };

        class Payload
        {
        public:
            std::vector<unsigned> _objects;
        };

        std::vector<Node>       _nodes;
        std::vector<Payload>    _payloads;
        unsigned                _maxCullResults;

        class WorkingObject
        {
        public:
            BoundingBox     _boundary;
            int             _id;
        };

        void PushNode(  unsigned parentNode, unsigned childIndex,
                        const std::vector<WorkingObject>& workingObjects);

        unsigned CalculateMaxResults()
        {
            unsigned result = 0;
            for (auto&i:_payloads)
                result += (unsigned)i._objects.size();
            return result;
        }

        static void InitPayload(Payload& p, const std::vector<WorkingObject>& workingObjects)
        {
            for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {
                p._objects.push_back(i->_id);
            }
        }

        static BoundingBox CalculateBoundary(const std::vector<WorkingObject>& workingObjects)
        {
            BoundingBox result;
            result.first  = Float3( FLT_MAX,  FLT_MAX,  FLT_MAX);
            result.second = Float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {
                assert(i->_boundary.first[0] <= i->_boundary.second[0]);
                assert(i->_boundary.first[1] <= i->_boundary.second[1]);
                assert(i->_boundary.first[2] <= i->_boundary.second[2]);
                result.first[0] = std::min(result.first[0], i->_boundary.first[0]);
                result.first[1] = std::min(result.first[1], i->_boundary.first[1]);
                result.first[2] = std::min(result.first[2], i->_boundary.first[2]);
                result.second[0] = std::max(result.second[0], i->_boundary.second[0]);
                result.second[1] = std::max(result.second[1], i->_boundary.second[1]);
                result.second[2] = std::max(result.second[2], i->_boundary.second[2]);
            }
            return result;
        }

        static std::tuple<unsigned, unsigned, unsigned> DividingLineMetricsX(float dividingLineX, const std::vector<WorkingObject>& workingObjects)
        {
            auto result = std::make_tuple(0,0,0);
            for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {
                std::get<0>(result) += (i->_boundary.second[0] <= dividingLineX);
                std::get<1>(result) += (i->_boundary.first[0] < dividingLineX) && (i->_boundary.second[0] > dividingLineX);
                std::get<2>(result) += (i->_boundary.first[0] >= dividingLineX);
            }
            return result;
        }

        static std::tuple<unsigned, unsigned, unsigned> DividingLineMetricsY(float dividingLineY, const std::vector<WorkingObject>& workingObjects)
        {
            auto result = std::make_tuple(0,0,0);
            for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {
                std::get<0>(result) += (i->_boundary.second[1] <= dividingLineY);
                std::get<1>(result) += (i->_boundary.first[1] < dividingLineY) && (i->_boundary.second[1] > dividingLineY);
                std::get<2>(result) += (i->_boundary.first[1] >= dividingLineY);
            }
            return result;
        }

        static float Volume(const BoundingBox& box)
        {
            return (box.second[2] - box.first[2]) * (box.second[1] - box.first[1]) * (box.second[0] - box.first[0]);
        }
    };

    void PlacementsQuadTree::Pimpl::PushNode(   
        unsigned parentNodeIndex, unsigned childIndex,
        const std::vector<WorkingObject>& workingObjects)
    {
        Node newNode;
        newNode._boundary = CalculateBoundary(workingObjects);
        newNode._children[0] = newNode._children[1] = newNode._children[2] = newNode._children[3] = ~unsigned(0x0);

        Node* parent = nullptr;
        if (parentNodeIndex < _nodes.size())
            parent = &_nodes[parentNodeIndex];

        newNode._treeDepth = parent ? (parent->_treeDepth+1) : 0;
        for (unsigned c=0; c<4; ++c) newNode._children[c] = ~unsigned(0x0);
        newNode._payloadID = ~unsigned(0x0);

            //  if the quantity of objects in this node is less than a threshold 
            //  amount, then we can consider it a leaf node
        const unsigned leafThreshold = 12;
        if (workingObjects.size() <= leafThreshold) {
            Payload payload;
            InitPayload(payload, workingObjects);
            _payloads.push_back(std::move(payload));
            newNode._payloadID = unsigned(_payloads.size()-1);

            if (parent) {
                parent->_children[childIndex] = unsigned(_nodes.size());
            }
            _nodes.push_back(newNode);
            return;
        }

            //  if it's not a leaf, then we must divide the boundary into sub nodes
            //  Let's try to do this in a way that will adapt to the placements of
            //  objects, and create a balanced tree. However, there is always a
            //  chance that objects will not be able to fit into the division 
            //  perfectly... These "straddling" objects need to be placed into the 
            //  smallest node that contains them completely. Ideally we want to find
            //  dividing lines that separate the objects into 2 roughly even groups, 
            //  but minimize the number of straddling objects. We can just do a brute
            //  force test of various potential dividing lines near the median points

        float bestDividingLineX = LinearInterpolate(newNode._boundary.first[0], newNode._boundary.second[0], 0.5f);
        float bestDividingLineY = LinearInterpolate(newNode._boundary.first[1], newNode._boundary.second[1], 0.5f);

        {
            std::vector<WorkingObject> sortedObjects = workingObjects;
            std::sort(
                sortedObjects.begin(), sortedObjects.end(),
                [](const WorkingObject& lhs, const WorkingObject&rhs)
                { return (lhs._boundary.first[0] + lhs._boundary.second[0]) < (rhs._boundary.first[0] + rhs._boundary.second[0]); });

            bestDividingLineX = .5f * (sortedObjects[sortedObjects.size()/2]._boundary.first[0] + sortedObjects[sortedObjects.size()/2]._boundary.second[0]);

            std::sort(
                sortedObjects.begin(), sortedObjects.end(),
                [](const WorkingObject& lhs, const WorkingObject&rhs)
                { return (lhs._boundary.first[1] + lhs._boundary.second[1]) < (rhs._boundary.first[1] + rhs._boundary.second[1]); });

            bestDividingLineY = .5f * (sortedObjects[sortedObjects.size()/2]._boundary.first[1] + sortedObjects[sortedObjects.size()/2]._boundary.second[1]);

                //  Attempt to improve the subdivision by looking for dividing lines
                //  that minimize the number of objects straddling this line. This will
                //  help try to avoid overlap between child nodes.
            const bool useAdaptiveDivision = true;
            if (constant_expression<useAdaptiveDivision>::result()) {

                auto objCount =  unsigned(sortedObjects.size());
                auto testCount = std::max(size_t(1), objCount/size_t(4));

                    //  Attempt to optimise X dividing line
                    //  Our optimised dividing line should always be on one
                    //  of the edges of the bounding box of one of the objects.
                std::sort(
                    sortedObjects.begin(), sortedObjects.end(),
                    [](const WorkingObject& lhs, const WorkingObject&rhs)
                    { return (lhs._boundary.first[0] + lhs._boundary.second[0]) < (rhs._boundary.first[0] + rhs._boundary.second[0]); });

                unsigned minStradingCount = std::get<1>(DividingLineMetricsX(bestDividingLineX, sortedObjects));
                float minDivLineX = LinearInterpolate(newNode._boundary.first[0], newNode._boundary.second[0], 0.25f);
                float maxDivLineX = LinearInterpolate(newNode._boundary.first[0], newNode._boundary.second[0], 0.75f);

                    // todo -- rather than starting at the object that has an equal number
                    //  of objects on each side, we could consider starting on the object that
                    //  is closest to the center of the bounding box
                for (unsigned c=0; c<testCount && minStradingCount; ++c) {
                    unsigned o;
                    if (c & 1)  { o = objCount/2 - ((c+1)>>1); }
                    else        { o = objCount/2 + ((c+1)>>1); }

                        //  We need to test both the left and right edges of the
                        //  this object's bounding box.
                    float testLine = sortedObjects[o]._boundary.first[0];
                    if (testLine >= minDivLineX && testLine <= maxDivLineX) {
                        unsigned leftCount, straddleCount, rightCount;
                        std::tie(leftCount, straddleCount, rightCount) = DividingLineMetricsX(testLine, sortedObjects);
                        if (straddleCount < minStradingCount && leftCount && rightCount) {
                            bestDividingLineX = testLine;
                            minStradingCount = straddleCount;
                        }
                    }

                    testLine = sortedObjects[o]._boundary.second[0];
                    if (testLine >= minDivLineX && testLine <= maxDivLineX) {
                        unsigned leftCount, straddleCount, rightCount;
                        std::tie(leftCount, straddleCount, rightCount) = DividingLineMetricsX(testLine, sortedObjects);
                        if (straddleCount < minStradingCount && leftCount && rightCount) {
                            bestDividingLineX = testLine;
                            minStradingCount = straddleCount;
                        }
                    }
                }

                    //  Attempt to optimise Y dividing line
                std::sort(
                    sortedObjects.begin(), sortedObjects.end(),
                    [](const WorkingObject& lhs, const WorkingObject&rhs)
                    { return (lhs._boundary.first[1] + lhs._boundary.second[1]) < (rhs._boundary.first[1] + rhs._boundary.second[1]); });

                minStradingCount = std::get<1>(DividingLineMetricsY(bestDividingLineY, sortedObjects));
                float minDivLineY = LinearInterpolate(newNode._boundary.first[1], newNode._boundary.second[1], 0.25f);
                float maxDivLineY = LinearInterpolate(newNode._boundary.first[1], newNode._boundary.second[1], 0.75f);

                for (unsigned c=0; c<testCount && minStradingCount; ++c) {
                    unsigned o;
                    if (c & 1)  { o = objCount/2 - ((c+1)>>1); }
                    else        { o = objCount/2 + ((c+1)>>1); }

                    float testLine = sortedObjects[o]._boundary.first[1];
                    if (testLine >= minDivLineY && testLine <= maxDivLineY) {
                        unsigned leftCount, straddleCount, rightCount;
                        std::tie(leftCount, straddleCount, rightCount) = DividingLineMetricsY(testLine, sortedObjects);
                        if (straddleCount < minStradingCount && leftCount && rightCount) {
                            bestDividingLineY = testLine;
                            minStradingCount = straddleCount;
                        }
                    }

                    testLine = sortedObjects[o]._boundary.second[1];
                    if (testLine >= minDivLineY && testLine <= maxDivLineY) {
                        unsigned leftCount, straddleCount, rightCount;
                        std::tie(leftCount, straddleCount, rightCount) = DividingLineMetricsY(testLine, sortedObjects);
                        if (straddleCount < minStradingCount && leftCount && rightCount) {
                            bestDividingLineY = testLine;
                            minStradingCount = straddleCount;
                        }
                    }
                }

            }
        }

            //  ok, now we have our dividing line. We an divide our objects up into 5 parts:
            //  4 children nodes, and the straddling nodes

        std::vector<WorkingObject> dividedObjects[5];
        for (auto i=workingObjects.cbegin(); i!=workingObjects.cend(); ++i) {

                /// \todo - if an object is large compared to the size of this
                ///         node's bounding box, then we should put it into
                ///         a forth "straddling" payload (rather than expanding one
                ///         of our children to almost the size of the parent node)

            unsigned index = 0;
            if (i->_boundary.first[0] > bestDividingLineX)          { index |= 0x1; } 
            else if (i->_boundary.second[0] < bestDividingLineX)    { index |= 0x0; } 
            else { 
                    //  try to put this object on the left or the right (based on where
                    //  the larger volume of this object is)
                if ((bestDividingLineX - i->_boundary.first[0]) > (i->_boundary.second[0] - bestDividingLineX)) {
                    index |= 0x0;
                } else {
                    index |= 0x1;
                }
            }

            if (i->_boundary.first[1] > bestDividingLineY)          { index |= 0x2; } 
            else if (i->_boundary.second[1] < bestDividingLineY)    { index |= 0x0; } 
            else { 
                if ((bestDividingLineY - i->_boundary.first[1]) > (i->_boundary.second[1] - bestDividingLineY)) {
                    index |= 0x0;
                } else {
                    index |= 0x2;
                }
            }

            dividedObjects[std::min(index, 4u)].push_back(*i);
        }

        // When there is a lot of overlap (or too few objects), we can choose to
        // merge children together.
        //   2+0 -> merged into 0
        //   3+1 -> merged into 1
        //   1+0 -> merged into 0
        //   3+2 -> merged into 2
        if ((dividedObjects[2].size() + dividedObjects[0].size()) <= leafThreshold) {
            if ((dividedObjects[1].size() + dividedObjects[0].size()) <= leafThreshold) {
                //  we can merge 2+0 or 1+0. Let's just do whichever ends up with a bounding
                //  box that has a smaller volume.
                auto merge20 = dividedObjects[2]; merge20.insert(merge20.begin(), dividedObjects[0].begin(), dividedObjects[0].end());
                auto merge10 = dividedObjects[1]; merge10.insert(merge10.begin(), dividedObjects[0].begin(), dividedObjects[0].end());
                if (Volume(CalculateBoundary(merge20)) < Volume(CalculateBoundary(merge10))) {
                    dividedObjects[0] = merge20;
                    dividedObjects[2].clear();
                } else {
                    dividedObjects[0] = merge10;
                    dividedObjects[1].clear();
                }
            } else {
                dividedObjects[0].insert(dividedObjects[0].begin(), dividedObjects[2].begin(), dividedObjects[2].end());
                dividedObjects[2].clear();
            }
        }

        if ((dividedObjects[3].size() + dividedObjects[1].size()) <= leafThreshold) {
            dividedObjects[1].insert(dividedObjects[1].begin(), dividedObjects[3].begin(), dividedObjects[3].end());
            dividedObjects[3].clear();
        }

        if ((dividedObjects[1].size() + dividedObjects[0].size()) <= leafThreshold) {
            dividedObjects[0].insert(dividedObjects[0].begin(), dividedObjects[1].begin(), dividedObjects[1].end());
            dividedObjects[1].clear();
        }

        if ((dividedObjects[3].size() + dividedObjects[2].size()) <= leafThreshold) {
            dividedObjects[2].insert(dividedObjects[2].begin(), dividedObjects[3].begin(), dividedObjects[3].end());
            dividedObjects[3].clear();
        }

        if (!dividedObjects[4].empty()) {
            Payload payload;
            InitPayload(payload, dividedObjects[4]);
            _payloads.push_back(std::move(payload));
            newNode._payloadID = unsigned(_payloads.size()-1);
        }

        assert(dividedObjects[0].size() + dividedObjects[1].size() + dividedObjects[2].size() + dividedObjects[3].size() + dividedObjects[4].size() == workingObjects.size());

        auto newNodeId = unsigned(_nodes.size());
        if (parent) {
            parent->_children[childIndex] = newNodeId;
        }
        _nodes.push_back(newNode);

            // now just push in the children
        for (unsigned c=0; c<4; ++c) {
            if (!dividedObjects[c].empty()) {
                PushNode(newNodeId, c, dividedObjects[c]);
            }
        }
    }

    bool PlacementsQuadTree::CalculateVisibleObjects(
        const Float4x4& cellToClipAligned, 
        const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
        unsigned visObjs[], unsigned& visObjsCount, unsigned visObjMaxCount,
        Metrics* metrics) const
    {
        visObjsCount = 0;
        assert((size_t(AsFloatArray(cellToClipAligned)) & 0xf) == 0);

        unsigned nodeAabbTestCount = 0, payloadAabbTestCount = 0;

            //  Traverse through the quad tree, and find do bounding box level 
            //  culling on each object
        static std::stack<unsigned> workingStack;
        static std::stack<unsigned> entirelyVisibleStack;
        assert(workingStack.empty() && entirelyVisibleStack.empty());
        workingStack.push(0);
        while (!workingStack.empty()) {
            auto nodeIndex = workingStack.top();
            workingStack.pop();
            
            auto& node = _pimpl->_nodes[nodeIndex];
            auto test = TestAABB_Aligned(cellToClipAligned, node._boundary.first, node._boundary.second, RenderCore::Techniques::GetDefaultClipSpaceType());
            ++nodeAabbTestCount;
            if (test == AABBIntersection::Culled) {
                continue;
            }

            if (test == AABBIntersection::Within) {

                    //  this node and all children are "visible" without
                    //  any further culling tests
                entirelyVisibleStack.push(nodeIndex);

            } else {

                for (unsigned c=0; c<4; ++c) {
                    if (node._children[c] < _pimpl->_nodes.size()) {
                        workingStack.push(node._children[c]);
                    }
                }

                if (node._payloadID < _pimpl->_payloads.size()) {
                    auto& payload = _pimpl->_payloads[node._payloadID];
                    for (auto i=payload._objects.cbegin(); i!=payload._objects.cend(); ++i) {

                            //  Test the "cell" space bounding box of the object itself
                            //  This must be done inside of this function, we can't
                            //  drop the responsibility to the caller. Because:
                            //      * sometimes we can skip it entirely, when quad tree
                            //          node bounding boxes are considered entirely within the frustum
                            //      * it's best to reduce the result arrays to as small as
                            //          possible (because the caller may need to sort them)

                        const auto& boundary = *PtrAdd(objCellSpaceBoundingBoxes, (*i) * objStride);
                        ++payloadAabbTestCount;
                        if (!CullAABB_Aligned(cellToClipAligned, boundary.first, boundary.second, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                            if ((visObjsCount+1) > visObjMaxCount) {
                                return false;
                            }
                            visObjs[visObjsCount++] = *i; 
                        }
                    }
                }

            }
        }

            //  some nodes might be "entirely visible" -- ie, the bounding box is completely
            //  within the culling frustum. In these cases, we can skip the rest of the culling
            //  checks and just add these objects as visible
        while (!entirelyVisibleStack.empty()) {
            auto nodeIndex = entirelyVisibleStack.top();
            entirelyVisibleStack.pop();

            auto& node = _pimpl->_nodes[nodeIndex];
            for (unsigned c=0; c<4; ++c) {
                if (node._children[c] < _pimpl->_nodes.size()) {
                    entirelyVisibleStack.push(node._children[c]);
                }
            }

            if (node._payloadID < _pimpl->_payloads.size()) {
                auto& payload = _pimpl->_payloads[node._payloadID];

                if ((visObjsCount + payload._objects.size()) > visObjMaxCount) {
                    return false;
                }

                for (auto i=payload._objects.cbegin(); i!=payload._objects.cend(); ++i) {
                    visObjs[visObjsCount++] = *i; 
                }
            }
        }

        assert(visObjsCount <= visObjMaxCount);
        if (metrics) {
            metrics->_nodeAabbTestCount = nodeAabbTestCount; 
            metrics->_payloadAabbTestCount = payloadAabbTestCount;
        }

        return true;
    }

    unsigned PlacementsQuadTree::GetMaxResults() const
    {
        return _pimpl->_maxCullResults;
    }

    PlacementsQuadTree::PlacementsQuadTree(
        const BoundingBox objCellSpaceBoundingBoxes[], size_t objStride,
        size_t objCount)
    {
            //  Find the minimum and maximum XY of the placements in "placements", and
            //  divide this space up into a quad tree (ignoring height)
            //
            //  Perhaps there are some cases where we might need to use an oct tree
            //  instead of a quad tree? What about buildings with multiple floors?
            //  can we intelligently detect where an oct-tree is required, and where it 
            //  should just be a quad tree?
            //
            //  Ideally we want to support input data that can have either a world space
            //  bounding box or a local space bounding box (or perhaps even other bounding
            //  primitives?)

        std::vector<Pimpl::WorkingObject> workingObjects;
        workingObjects.reserve(objCount);

        for (unsigned c=0; c<objCount; ++c) {
            auto& objBoundary = *PtrAdd(objCellSpaceBoundingBoxes, c * objStride);
            Pimpl::WorkingObject o;
            o._boundary = objBoundary;
            o._id = c;
            workingObjects.push_back(o);
        }

            //  we need to filter each object into nodes as we iterate through the tree
            //  once we have a fixed number of objects a given node, we can make that node
            //  a leaf. Objects should be placed into the smallest node that contains them
            //  completely. We want to avoid cases where objects end up in the dividing line
            //  between nodes. So we'll use a system that adjusts the bounding box of each
            //  node based on the objects assigned to it.

        auto pimpl = std::make_unique<Pimpl>();
        pimpl->PushNode(~unsigned(0x0), 0, workingObjects);
        pimpl->_maxCullResults = pimpl->CalculateMaxResults();

        _pimpl = std::move(pimpl);
    }

    PlacementsQuadTree::~PlacementsQuadTree() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void    PlacementsQuadTreeDebugger::Render(
        RenderOverlays::IOverlayContext& context, Layout& layout, 
        Interactables& interactables, InterfaceState& interfaceState)
    {
		assert(0);	// broken because we can't access the device context via IOverlayContext anymore
#if 0
        RenderCore::Metal::DeviceContext::Get(*context.GetDeviceContext())->Bind(Techniques::CommonResources()._dssDisable);
        static signed treeDepthFilter = -1;
        static bool drawObjects = false;

        using namespace RenderOverlays;
        using namespace RenderOverlays::DebuggingDisplay;

        ColorB cols[]= {
            ColorB(196, 230, 230),
            ColorB(255, 128, 128),
            ColorB(128, 255, 128),
            ColorB(128, 128, 255),
            ColorB(255, 255, 128)
        };

        if (!drawObjects) {
                // Find all of the quad trees that are used by the manager for
                // the current camera position. For each quad-tree we find, let's
                // render some debugging information (including bounding boxes for
                // the nodes in the quad tree).
            auto quadTrees = _placementsManager->GetRenderer()->GetVisibleQuadTrees(
                *_cells, context.GetProjectionDesc()._worldToProjection);
            for (auto i=quadTrees.cbegin(); i!=quadTrees.cend(); ++i) {
                auto cellToWorld = i->first;
                auto quadTree = i->second;
                if (!quadTree) continue;

                auto& nodes = quadTree->_pimpl->_nodes;
                for (auto n=nodes.cbegin(); n!=nodes.cend(); ++n) {
                    if (treeDepthFilter < 0 || signed(n->_treeDepth) == treeDepthFilter) {
                        DrawBoundingBox(
                            &context, n->_boundary, cellToWorld,
                            cols[std::min((unsigned)dimof(cols), n->_treeDepth)], 0x1);
                    }
                }
                for (auto n=nodes.cbegin(); n!=nodes.cend(); ++n) {
                    if (treeDepthFilter < 0 || signed(n->_treeDepth) == treeDepthFilter) {
                        DrawBoundingBox(
							&context, n->_boundary, cellToWorld,
                            cols[std::min((unsigned)dimof(cols), n->_treeDepth)], 0x2);
                    }
                }
            }
        } else {
            auto cells = _placementsManager->GetRenderer()->GetObjectBoundingBoxes(*_cells, context.GetProjectionDesc()._worldToProjection);
            for (auto cell=cells.cbegin(); cell!=cells.cend(); ++cell) {
                auto cellToWorld = cell->first;
                auto objs = cell->second;

                for (unsigned c=0; c<objs._count; ++c) {
                    auto& boundary = *PtrAdd(objs._boundingBox, c*objs._stride);
                    DrawBoundingBox(&context, boundary, cellToWorld, cols[0], 0x1);
                }
                for (unsigned c=0; c<objs._count; ++c) {
                    auto& boundary = *PtrAdd(objs._boundingBox, c*objs._stride);
                    DrawBoundingBox(&context, boundary, cellToWorld, cols[0], 0x2);
                }
            }
        }
#endif
    }

    bool    PlacementsQuadTreeDebugger::ProcessInput(
        InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input)
    {
        return false;
    }

    PlacementsQuadTreeDebugger::PlacementsQuadTreeDebugger(
        std::shared_ptr<PlacementsManager> placementsManager, 
        std::shared_ptr<PlacementCellSet> cells)
    : _placementsManager(placementsManager)
    , _cells(cells)
    {}

    PlacementsQuadTreeDebugger::~PlacementsQuadTreeDebugger()
    {}


}