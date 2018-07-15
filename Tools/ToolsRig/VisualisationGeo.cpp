// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "VisualisationGeo.h"
#include "../../RenderCore/Types.h"
#include "../../RenderCore/Format.h"
#include "../../Utility/MemoryUtils.h"

namespace ToolsRig
{
    namespace Internal
    {
        static RenderCore::InputElementDesc Vertex2D_InputLayout_[] = {
            RenderCore::InputElementDesc( "POSITION", 0, RenderCore::Format::R32G32_FLOAT ),
            RenderCore::InputElementDesc( "TEXCOORD", 0, RenderCore::Format::R32G32_FLOAT )
        };

        static RenderCore::InputElementDesc Vertex3D_InputLayout_[] = {
            RenderCore::InputElementDesc( "POSITION", 0, RenderCore::Format::R32G32B32_FLOAT ),
            RenderCore::InputElementDesc(   "NORMAL", 0, RenderCore::Format::R32G32B32_FLOAT ),
            RenderCore::InputElementDesc( "TEXCOORD", 0, RenderCore::Format::R32G32_FLOAT ),
            RenderCore::InputElementDesc( "TEXTANGENT", 0, RenderCore::Format::R32G32B32A32_FLOAT )//,
            //RenderCore::Metal::InputElementDesc( "TEXBITANGENT", 0, RenderCore::Metal::NativeFormat::R32G32B32_FLOAT )
        };
    }

    IteratorRange<const RenderCore::InputElementDesc*> Vertex2D_InputLayout = MakeIteratorRange(Internal::Vertex2D_InputLayout_);
    IteratorRange<const RenderCore::InputElementDesc*> Vertex3D_InputLayout = MakeIteratorRange(Internal::Vertex3D_InputLayout_);

	extern std::vector<RenderCore::MiniInputElementDesc> Vertex2D_MiniInputLayout {
		RenderCore::MiniInputElementDesc{ Hash64("POSITION"), RenderCore::Format::R32G32_FLOAT },
		RenderCore::MiniInputElementDesc{ Hash64("TEXCOORD"), RenderCore::Format::R32G32_FLOAT }
	};
    extern std::vector<RenderCore::MiniInputElementDesc> Vertex3D_MiniInputLayout {
		RenderCore::MiniInputElementDesc{ Hash64("POSITION"), RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::MiniInputElementDesc{ Hash64("NORMAL"), RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::MiniInputElementDesc{ Hash64("TEXCOORD"), RenderCore::Format::R32G32_FLOAT },
		RenderCore::MiniInputElementDesc{ Hash64("TEXTANGENT"), RenderCore::Format::R32G32B32A32_FLOAT }
	};

    static void GeodesicSphere_Subdivide(const Float3 &v1, const Float3 &v2, const Float3 &v3, std::vector<Float3> &sphere_points, unsigned int depth) 
    {
        if(depth == 0) 
        {
            sphere_points.push_back(v1);
            sphere_points.push_back(v2);
            sphere_points.push_back(v3);
            return;
        }

        Float3 v12 = Normalize(v1 + v2);
        Float3 v23 = Normalize(v2 + v3);
        Float3 v31 = Normalize(v3 + v1);
        GeodesicSphere_Subdivide( v1, v12, v31, sphere_points, depth - 1);
        GeodesicSphere_Subdivide( v2, v23, v12, sphere_points, depth - 1);
        GeodesicSphere_Subdivide( v3, v31, v23, sphere_points, depth - 1);
        GeodesicSphere_Subdivide(v12, v23, v31, sphere_points, depth - 1);
    }

    static std::vector<Float3>     BuildGeodesicSpherePts(int detail)
    {

            //  
            //      Basic geodesic sphere generation code
            //          Based on a document from http://www.opengl.org.ru/docs/pg/0208.html
            //
        const float X = 0.525731112119133606f;
        const float Z = 0.850650808352039932f;
        const Float3 vdata[12] = 
        {
            Float3(  -X, 0.0,   Z ), Float3(   X, 0.0,   Z ), Float3(  -X, 0.0,  -Z ), Float3(   X, 0.0,  -Z ),
            Float3( 0.0,   Z,   X ), Float3( 0.0,   Z,  -X ), Float3( 0.0,  -Z,   X ), Float3( 0.0,  -Z,  -X ),
            Float3(   Z,   X, 0.0 ), Float3(  -Z,   X, 0.0 ), Float3(   Z,  -X, 0.0 ), Float3(  -Z,  -X, 0.0 )
        };

        int tindices[20][3] = 
        {
            { 0,  4,  1 }, { 0, 9,  4 }, { 9,  5, 4 }, {  4, 5, 8 }, { 4, 8,  1 },
            { 8, 10,  1 }, { 8, 3, 10 }, { 5,  3, 8 }, {  5, 2, 3 }, { 2, 7,  3 },
            { 7, 10,  3 }, { 7, 6, 10 }, { 7, 11, 6 }, { 11, 0, 6 }, { 0, 1,  6 },
            { 6,  1, 10 }, { 9, 0, 11 }, { 9, 11, 2 }, {  9, 2, 5 }, { 7, 2, 11 }
        };

        std::vector<Float3> spherePoints;
        for(int i = 0; i < 20; i++) {
                // note -- flip here to flip the winding
            GeodesicSphere_Subdivide(
                vdata[tindices[i][0]], vdata[tindices[i][2]], 
                vdata[tindices[i][1]], spherePoints, detail);
        }
        return spherePoints;
    }

    std::vector<Internal::Vertex3D>   BuildGeodesicSphere(int detail)
    {
            //      build a geodesic sphere at the origin with radius 1     //
        auto pts = BuildGeodesicSpherePts(detail);

        std::vector<Internal::Vertex3D> result;
        result.reserve(pts.size());

        const float texWrapsX = 8.f;
        const float texWrapsY = 4.f;

        for (auto i=pts.cbegin(); i!=pts.cend(); ++i) {
            Internal::Vertex3D vertex;
            vertex._position    = *i;
            vertex._normal      = Normalize(*i);        // centre is the origin, so normal points towards the position

                //  Texture coordinates based on longitude / latitude
                //  2 texture wraps horizontally, and 1 wrap vertically
                //      let's map [-0.5f*pi, .5f*pi] -> [0.f, 1.f];

            float latitude  = XlASin((*i)[2]);
            float longitude = XlATan2((*i)[1], (*i)[0]);
            latitude = 1.f - (latitude + .5f * gPI) / gPI * texWrapsY;
            longitude = (longitude + .5f * gPI) / gPI * (texWrapsX / 2.f);

            vertex._texCoord = Float2(longitude, latitude);

            Float3 bt(0.f, 0.f, -1.f);
            bt = bt - vertex._normal * Dot(vertex._normal, bt);
            if (MagnitudeSquared(bt) < 1e-3f) {
                    // this a vertex on a singularity (straight up or straight down)
                vertex._tangent = Float4(0.f, 0.f, 0.f, 0.f);
                // vertex._bitangent = Float3(0.f, 0.f, 0.f);
            } else {
                bt = Normalize(bt);
                // vertex._bitangent = bt;
                    // cross(bitangent, tangent) * handiness == normal, so...
                Float3 t = Normalize(Cross(vertex._normal, bt));
                vertex._tangent = Expand(t, 1.f);
            
                auto test = Float3(Cross(bt, Truncate(vertex._tangent)) * vertex._tangent[3]);
                assert(Equivalent(test, vertex._normal, 1e-4f));

                    // tangent should also be the 2d cross product of the XY position (according to the shape of a sphere)
                auto test2 = Normalize(Float3(-(*i)[1], (*i)[0], 0.f));
                assert(Equivalent(test2, Truncate(vertex._tangent), 1e-4f));

                    // make sure handiness is right
                // assert(Equivalent(vertex._bitangent[2], 0.f, 1e-4f));
                // auto testLong = XlATan2((*i)[1] + 0.05f * vertex._bitangent[1], (*i)[0] + 0.05f * vertex._bitangent[0]);
                // testLong = (testLong + .5f * gPI) / gPI;
                // assert(testLong > longitude);
            }

            result.push_back(vertex);
        }

            // there is a problem case on triangles that wrap around in longitude. Along these triangles, the
            // texture coordinates will appear to wrap backwards through the entire texture. We can use the
            // tangents to find these triangles, because the tangents will appear to be in the wrong direction
            // for these triangles.
        unsigned triCount = unsigned(result.size() / 3);
        for (unsigned t=0; t<triCount; ++t) {
            auto& A = result[t*3+0];
            auto& B = result[t*3+1];
            auto& C = result[t*3+2];

                // problems around the singularity straight up or straight down
            if (MagnitudeSquared(A._tangent) < 1e-4f || MagnitudeSquared(B._tangent) < 1e-4f || MagnitudeSquared(C._tangent) < 1e-4f)
                continue;

            if (XlAbs(B._texCoord[0] - A._texCoord[0]) > 1e-3f) {
                assert(Dot(Truncate(B._tangent), Truncate(A._tangent)) > 0.f);  // both tangents should point in roughly the same direction
                bool rightWay1 = (Dot(B._position - A._position, Truncate(A._tangent)) < 0.f) == ((B._texCoord[0] - A._texCoord[0]) < 0.f);
                if (!rightWay1) {
                    if (B._texCoord[0] < A._texCoord[0]) B._texCoord[0] += texWrapsX;
                    else A._texCoord[0] += texWrapsX;
                }
            }

            if (XlAbs(C._texCoord[0] - A._texCoord[0]) > 1e-3f) {
                assert(Dot(Truncate(C._tangent), Truncate(A._tangent)) > 0.f);  // both tangents should point in roughly the same direction
                bool rightWay1 = (Dot(C._position - A._position, Truncate(A._tangent)) < 0.f) == ((C._texCoord[0] - A._texCoord[0]) < 0.f);
                if (!rightWay1) {
                    if (C._texCoord[0] < A._texCoord[0]) C._texCoord[0] += texWrapsX;
                    else A._texCoord[0] += texWrapsX;
                }
            }

            if (XlAbs(C._texCoord[0] - B._texCoord[0]) > 1e-3f) {
                assert(Dot(Truncate(C._tangent), Truncate(B._tangent)) > 0.f);  // both tangents should point in roughly the same direction
                bool rightWay1 = (Dot(C._position - B._position, Truncate(B._tangent)) < 0.f) == ((C._texCoord[0] - B._texCoord[0]) < 0.f);
                if (!rightWay1) {
                    if (C._texCoord[0] < B._texCoord[0]) C._texCoord[0] += texWrapsX;
                    else B._texCoord[0] += texWrapsX;
                }
            }
        }

        return result;
    }

    std::vector<Internal::Vertex3D> BuildCube()
    {
            // build a basic cube at the origing with radius 1. All edges are "sharp" edges //
        Float3 normals[] = { 
            Float3(0.f, 0.f, -1.f), Float3(0.f, 0.f, 1.f),
            Float3(1.f, 0.f, 0.f), Float3(-1.f, 0.f, 0.f),
            Float3(0.f, 1.f, 0.f), Float3(0.f, -1.f, 0.f)
        };
        Float3 Us[] = {
            Float3(1.f, 0.f, 0.f), Float3(-1.f, 0.f, 0.f),
            Float3(0.f, 1.f, 0.f), Float3(0.f, -1.f, 0.f),
            Float3(-1.f, 0.f, 0.f), Float3(1.f, 0.f,  0.f)
        };
        Float3 Vs[] = {
            Float3(0.f, 1.f, 0.f), Float3(0.f, 1.f, 0.f),
            Float3(0.f, 0.f, -1.f), Float3(0.f, 0.f, -1.f),
            Float3(0.f, 0.f, -1.f), Float3(0.f, 0.f, -1.f)
        };

        float faceCoord[4][2] = {{ -1.f, -1.f }, { -1.f, 1.f }, { 1.f, -1.f }, { 1.f, 1.f }};

        std::vector<Internal::Vertex3D> result;
        for (unsigned c=0; c<6; ++c) {
            auto normal = normals[c], u = Us[c], v = Vs[c];

            Internal::Vertex3D a[4];
            for (unsigned q=0; q<4; ++q) {
                a[q]._position = normal + faceCoord[q][0] * u + faceCoord[q][1] * v;
                a[q]._normal = normal;
                a[q]._texCoord = Float2(.5f * faceCoord[q][0] + .5f, .5f * faceCoord[q][1] + .5f);
                a[q]._tangent = Expand(u, 1.f);
                a[q]._tangent[3] = (Dot(Cross(v, u), normal) < 0.f) ? -1.f : 1.f;
                // a[q]._bitangent = v;
            }
            result.push_back(a[0]); result.push_back(a[1]); result.push_back(a[2]);
            result.push_back(a[2]); result.push_back(a[1]); result.push_back(a[3]);
        }

        return result;
    }
}

