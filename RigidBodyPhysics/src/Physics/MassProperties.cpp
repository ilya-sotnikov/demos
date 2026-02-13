#include "MassProperties.hpp"

#include "../Math/Vec3.hpp"
#include "../Math/Mat3.hpp"

#include <float.h>

// https://en.wikipedia.org/wiki/List_of_moments_of_inertia

namespace MassProperties
{

void CalculateSphere(f32 radius, f32 density, Mat3& inverseInertia, f32& inverseMass)
{
    assert(radius > 0.0f);
    assert(density > 0.0f);

    const f32 radius2 = radius * radius;

    const f32 volume = 4.0f / 3.0f * M_PIf * radius2 * radius;

    inverseInertia = Mat3::Zero();

    if (density < FLT_MAX)
    {
        const f32 mass = density * volume;
        inverseMass = 1.0f / mass;

        const f32 inverseInertiaDiag = 1.0f / (2.0f / 5.0f * mass * radius2);
        inverseInertia(0, 0) = inverseInertiaDiag;
        inverseInertia(1, 1) = inverseInertiaDiag;
        inverseInertia(2, 2) = inverseInertiaDiag;
    }
    else
    {
        inverseMass = 0.0f;
    }
}

void CalculateRectangularCuboid(Vec3 size, f32 density, Mat3& inverseInertia, f32& inverseMass)
{
    assert(size.X() > 0.0f);
    assert(size.Y() > 0.0f);
    assert(size.Z() > 0.0f);
    assert(density > 0.0f);

    const f32 volume = size.X() * size.Y() * size.Z();

    inverseInertia = Mat3::Zero();

    if (density < FLT_MAX)
    {
        const f32 mass = density * volume;

        inverseMass = 1.0f / mass;

        const f32 massScaled = 1.0f / 12.0f * mass;
        const f32 w2 = size.X() * size.X();
        const f32 h2 = size.Y() * size.Y();
        const f32 d2 = size.Z() * size.Z();

        inverseInertia(0, 0) = 1.0f / (massScaled * (h2 + d2));
        inverseInertia(1, 1) = 1.0f / (massScaled * (w2 + d2));
        inverseInertia(2, 2) = 1.0f / (massScaled * (w2 + h2));
    }
    else
    {
        inverseMass = 0.0f;
    }
}

// Polyhedral Mass Properties (Revisited) David Eberly, Geometric Tools.
// https://www.geometrictools.com/Documentation/PolyhedralMassProperties.pdf
// Assumes counterclockwise ordered triangles.
void CalculatePolyhedronTriangleMesh(
    const Vec3* positions,
    const u16* indices,
    int indicesCount,
    f32 density,
    Vec3 scale,
    Mat3& inverseInertia,
    Vec3& centerOfMass,
    f32& inverseMass
)
{
    assert(positions);
    assert(indices);
    assert(indicesCount > 0);
    assert(scale.X() > 0.0f);
    assert(scale.Y() > 0.0f);
    assert(scale.Z() > 0.0f);
    assert(density > 0.0f);

#define SUBEXPRESSIONS(w0, w1, w2, f1, f2, f3, g0, g1, g2) \
    temp0 = w0 + w1; \
    const f32 f1 = temp0 + w2; \
    temp1 = w0 * w0; \
    temp2 = temp1 + w1 * temp0; \
    const f32 f2 = temp2 + w2 * f1; \
    const f32 f3 = w0 * temp1 + w1 * temp2 + w2 * f2; \
    const f32 g0 = f2 + w0 * (f1 + w0); \
    const f32 g1 = f2 + w1 * (f1 + w1); \
    const f32 g2 = f2 + w2 * (f1 + w2);

    // clang-format off
    constexpr f32 integralsCoefficients[] = {
        1.0f / 6.0f,
        1.0f / 24.0f,
        1.0f / 24.0f,
        1.0f / 24.0f,
        1.0f / 60.0f,
        1.0f / 60.0f,
        1.0f / 60.0f,
        1.0f / 120.0f,
        1.0f / 120.0f,
        1.0f / 120.0f
    };
    // clang-format on
    f32 integrals[10] = {}; // Order: 1, x, y, z, xˆ2, yˆ2, zˆ2, xy, yz, zx.
    f32 temp0 = 0.0f;
    f32 temp1 = 0.0f;
    f32 temp2 = 0.0f;

    for (int t = 0; t < indicesCount / 3; ++t)
    {
        // Get vertices of triangle t.
        const u16 i0 = indices[3 * t];
        const u16 i1 = indices[3 * t + 1];
        const u16 i2 = indices[3 * t + 2];
        const f32 x0 = positions[i0].X() * scale.X();
        const f32 y0 = positions[i0].Y() * scale.Y();
        const f32 z0 = positions[i0].Z() * scale.Z();
        const f32 x1 = positions[i1].X() * scale.X();
        const f32 y1 = positions[i1].Y() * scale.Y();
        const f32 z1 = positions[i1].Z() * scale.Z();
        const f32 x2 = positions[i2].X() * scale.X();
        const f32 y2 = positions[i2].Y() * scale.Y();
        const f32 z2 = positions[i2].Z() * scale.Z();
        // Get edges and cross product of edges.
        const f32 a1 = x1 - x0;
        const f32 b1 = y1 - y0;
        const f32 c1 = z1 - z0;
        const f32 a2 = x2 - x0;
        const f32 b2 = y2 - y0;
        const f32 c2 = z2 - z0;
        const f32 d0 = b1 * c2 - b2 * c1;
        const f32 d1 = a2 * c1 - a1 * c2;
        const f32 d2 = a1 * b2 - a2 * b1;
        // Compute integral terms.
        SUBEXPRESSIONS(x0, x1, x2, f1x, f2x, f3x, g0x, g1x, g2x);
        SUBEXPRESSIONS(y0, y1, y2, f1y, f2y, f3y, g0y, g1y, g2y);
        SUBEXPRESSIONS(z0, z1, z2, f1z, f2z, f3z, g0z, g1z, g2z);
        // Update integrals.
        integrals[0] += d0 * f1x;
        integrals[1] += d0 * f2x;
        integrals[2] += d1 * f2y;
        integrals[3] += d2 * f2z;
        integrals[4] += d0 * f3x;
        integrals[5] += d1 * f3y;
        integrals[6] += d2 * f3z;
        integrals[7] += d0 * (y0 * g0x + y1 * g1x + y2 * g2x);
        integrals[8] += d1 * (z0 * g0y + z1 * g1y + z2 * g2y);
        integrals[9] += d2 * (x0 * g0z + x1 * g1z + x2 * g2z);
    }
    for (int i = 0; i < 10; ++i)
    {
        integrals[i] *= integralsCoefficients[i];
    }

    f32 mass = integrals[0];

    centerOfMass = {integrals[1] / mass, integrals[2] / mass, integrals[3] / mass};

    if (density == FLT_MAX)
    {
        inverseMass = 0.0f;
        inverseInertia = Mat3::Zero();
    }
    else
    {
        mass *= density;
        inverseMass = 1.0f / mass;

        //     [  Ixx -Ixy -Ixz ]
        // J = | -Ixy  Iyy -Iyz |
        //     [ -Ixz -Iyz  Izz ]
        Mat3 inertia;

        inertia(0, 0) = integrals[5] + integrals[6]
            - mass * (centerOfMass.Y() * centerOfMass.Y() + centerOfMass.Z() * centerOfMass.Z());
        inertia(1, 1) = integrals[4] + integrals[6]
            - mass * (centerOfMass.Z() * centerOfMass.Z() + centerOfMass.X() * centerOfMass.X());
        inertia(2, 2) = integrals[4] + integrals[5]
            - mass * (centerOfMass.X() * centerOfMass.X() + centerOfMass.Y() * centerOfMass.Y());

        inertia(0, 1) = -(integrals[7] - mass * centerOfMass.X() * centerOfMass.Y());
        inertia(1, 0) = inertia(0, 1);

        inertia(1, 2) = -(integrals[8] - mass * centerOfMass.Y() * centerOfMass.Z());
        inertia(2, 1) = inertia(1, 2);

        inertia(0, 2) = -(integrals[9] - mass * centerOfMass.Z() * centerOfMass.X());
        inertia(2, 0) = inertia(0, 2);

        inertia *= density;

        inverseInertia = Inverse(inertia);
    }

#undef SUBEXPRESSIONS
}

}
