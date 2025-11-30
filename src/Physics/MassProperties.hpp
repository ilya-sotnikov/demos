#pragma once

#include "../Common.hpp"
#include "../Math/Types.hpp"

namespace MassProperties
{

void CalculateSphere(f32 radius, f32 density, Mat3& inverseInertia, f32& inverseMass);
void CalculateRectangularCuboid(Vec3 size, f32 density, Mat3& inverseInertia, f32& inverseMass);
void CalculatePolyhedronTriangleMesh(
    const Vec3* positions,
    const u16* indices,
    int indicesCount,
    f32 density,
    Vec3 scale,
    Mat3& inverseInertia,
    Vec3& centerOfMass,
    f32& inverseMass
);

}
