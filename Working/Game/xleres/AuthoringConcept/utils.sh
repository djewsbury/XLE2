
#include "../Transform.h"
#include "../MainGeometry.h"

float GetSceneTime()
{
    return Time;
}

float2 GetPixelCoord();

float2 GetPixelCoordZeroToOne(VSOutput geo)
{
    // todo -- use true viewport dimensions here
    return float2(geo.position.x / 1024.f, geo.position.y / 1024.f);
}
