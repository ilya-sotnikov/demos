#include "GJK.hpp"

#include "../Math/Vec3.hpp"
#include "../Math/Vec4.hpp"

#include <assert.h>
#include <float.h>
#include <math.h>

bool Gjk(GjkSimplex& simplex, GjkSupport& support)
{
    if (simplex.mIterations >= GJK_MAX_ITERATIONS)
    {
        return false;
    }

    // Initialize.
    if (simplex.mCount == 0)
    {
        simplex.mD = FLT_MAX;
    }
    // Check for duplications.
    for (int i = 0; i < simplex.mCount; ++i)
    {
        if (support.mIdA != simplex.mVertex[i].mIdA)
        {
            continue;
        }
        if (support.mIdB != simplex.mVertex[i].mIdB)
        {
            continue;
        }
        return false;
    }
    // Add vertex into simplex.
    simplex.mVertex[simplex.mCount].mA = support.mA;
    simplex.mVertex[simplex.mCount].mB = support.mB;
    simplex.mVertex[simplex.mCount].mP = support.mB - support.mA;
    simplex.mVertex[simplex.mCount].mIdA = support.mIdA;
    simplex.mVertex[simplex.mCount].mIdB = support.mIdB;
    simplex.mBC[simplex.mCount++] = 1.0f;

    // Find closest simplex point.
    switch (simplex.mCount)
    {
    case 1:
        break;
    case 2:
    {
        // Line.
        const Vec3 a = simplex.mVertex[0].mP;
        const Vec3 b = simplex.mVertex[1].mP;

        // Compute barycentric coordinates.
        const Vec3 ab = a - b;
        const Vec3 ba = b - a;

        const f32 u = Dot(b, ba);
        const f32 v = Dot(a, ab);

        if (v <= 0.0f)
        {
            // Region A.
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        if (u <= 0.0f)
        {
            // Region B.
            simplex.mVertex[0] = simplex.mVertex[1];
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        // Region AB.
        simplex.mBC[0] = u;
        simplex.mBC[1] = v;
        simplex.mCount = 2;
    }
    break;
    case 3:
    {
        // Triangle.
        const Vec3 a = simplex.mVertex[0].mP;
        const Vec3 b = simplex.mVertex[1].mP;
        const Vec3 c = simplex.mVertex[2].mP;

        const Vec3 ab = a - b;
        const Vec3 ba = b - a;
        const Vec3 bc = b - c;
        const Vec3 cb = c - b;
        const Vec3 ca = c - a;
        const Vec3 ac = a - c;

        // Compute barycentric coordinates.
        const f32 uAB = Dot(b, ba);
        const f32 vAB = Dot(a, ab);

        const f32 uBC = Dot(c, cb);
        const f32 vBC = Dot(b, bc);

        const f32 uCA = Dot(a, ac);
        const f32 vCA = Dot(c, ca);

        if (vAB <= 0.0f && uCA <= 0.0f)
        {
            // Region A.
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        if (uAB <= 0.0f && vBC <= 0.0f)
        {
            // Region B.
            simplex.mVertex[0] = simplex.mVertex[1];
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        if (uBC <= 0.0f && vCA <= 0.0f)
        {
            // Region C.
            simplex.mVertex[0] = simplex.mVertex[2];
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        // Calculate fractional area.
        const Vec3 n = Cross(ba, ca);
        const Vec3 n1 = Cross(b, c);
        const Vec3 n2 = Cross(c, a);
        const Vec3 n3 = Cross(a, b);

        const f32 uABC = Dot(n1, n);
        const f32 vABC = Dot(n2, n);
        const f32 wABC = Dot(n3, n);

        if (uAB > 0.0f && vAB > 0.0f && wABC <= 0.0f)
        {
            // Region AB.
            simplex.mBC[0] = uAB;
            simplex.mBC[1] = vAB;
            simplex.mCount = 2;
            break;
        }
        if (uBC > 0.0f && vBC > 0.0f && uABC <= 0.0f)
        {
            // Region BC.
            simplex.mVertex[0] = simplex.mVertex[1];
            simplex.mVertex[1] = simplex.mVertex[2];
            simplex.mBC[0] = uBC;
            simplex.mBC[1] = vBC;
            simplex.mCount = 2;
            break;
        }
        if (uCA > 0.0f && vCA > 0.0f && vABC <= 0.0f)
        {
            // Region CA.
            simplex.mVertex[1] = simplex.mVertex[0];
            simplex.mVertex[0] = simplex.mVertex[2];
            simplex.mBC[0] = uCA;
            simplex.mBC[1] = vCA;
            simplex.mCount = 2;
            break;
        }
        // Region ABC.
        assert(uABC > 0.0f && vABC > 0.0f && wABC > 0.0f);
        simplex.mBC[0] = uABC;
        simplex.mBC[1] = vABC;
        simplex.mBC[2] = wABC;
        simplex.mCount = 3;
    }
    break;
    case 4:
    {
        // Tetrahedron.
        const Vec3 a = simplex.mVertex[0].mP;
        const Vec3 b = simplex.mVertex[1].mP;
        const Vec3 c = simplex.mVertex[2].mP;
        const Vec3 d = simplex.mVertex[3].mP;

        const Vec3 ab = a - b;
        const Vec3 ba = b - a;
        const Vec3 bc = b - c;
        const Vec3 cb = c - b;
        const Vec3 ca = c - a;
        const Vec3 ac = a - c;

        const Vec3 db = d - b;
        const Vec3 bd = b - d;
        const Vec3 dc = d - c;
        const Vec3 cd = c - d;
        const Vec3 da = d - a;
        const Vec3 ad = a - d;

        // Compute barycentric coordinates.
        const f32 uAB = Dot(b, ba);
        const f32 vAB = Dot(a, ab);

        const f32 uBC = Dot(c, cb);
        const f32 vBC = Dot(b, bc);

        const f32 uCA = Dot(a, ac);
        const f32 vCA = Dot(c, ca);

        const f32 uBD = Dot(d, db);
        const f32 vBD = Dot(b, bd);

        const f32 uDC = Dot(c, cd);
        const f32 vDC = Dot(d, dc);

        const f32 uAD = Dot(d, da);
        const f32 vAD = Dot(a, ad);

        // Check verticies for closest point.
        if (vAB <= 0.0f && uCA <= 0.0f && vAD <= 0.0f)
        {
            // Region A.
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        if (uAB <= 0.0f && vBC <= 0.0f && vBD <= 0.0f)
        {
            // Region B.
            simplex.mVertex[0] = simplex.mVertex[1];
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        if (uBC <= 0.0f && vCA <= 0.0f && uDC <= 0.0f)
        {
            // Region C.
            simplex.mVertex[0] = simplex.mVertex[2];
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        if (uBD <= 0.0f && vDC <= 0.0f && uAD <= 0.0f)
        {
            // Region D.
            simplex.mVertex[0] = simplex.mVertex[3];
            simplex.mBC[0] = 1.0f;
            simplex.mCount = 1;
            break;
        }
        // Calculate fractional area.
        Vec3 n = Cross(da, ba);
        Vec3 n1 = Cross(d, b);
        Vec3 n2 = Cross(b, a);
        Vec3 n3 = Cross(a, d);

        const f32 uADB = Dot(n1, n);
        const f32 vADB = Dot(n2, n);
        const f32 wADB = Dot(n3, n);

        n = Cross(ca, da);
        n1 = Cross(c, d);
        n2 = Cross(d, a);
        n3 = Cross(a, c);

        const f32 uACD = Dot(n1, n);
        const f32 vACD = Dot(n2, n);
        const f32 wACD = Dot(n3, n);

        n = Cross(bc, dc);
        n1 = Cross(b, d);
        n2 = Cross(d, c);
        n3 = Cross(c, b);

        const f32 uCBD = Dot(n1, n);
        const f32 vCBD = Dot(n2, n);
        const f32 wCBD = Dot(n3, n);

        n = Cross(ba, ca);
        n1 = Cross(b, c);
        n2 = Cross(c, a);
        n3 = Cross(a, b);

        const f32 uABC = Dot(n1, n);
        const f32 vABC = Dot(n2, n);
        const f32 wABC = Dot(n3, n);

        // Check edges for closest point.
        if (wABC <= 0.0f && vADB <= 0.0f && uAB > 0.0f && vAB > 0.0f)
        {
            // Region AB.
            simplex.mBC[0] = uAB;
            simplex.mBC[1] = vAB;
            simplex.mCount = 2;
            break;
        }
        if (uABC <= 0.0f && wCBD <= 0.0f && uBC > 0.0f && vBC > 0.0f)
        {
            // Region BC.
            simplex.mVertex[0] = simplex.mVertex[1];
            simplex.mVertex[1] = simplex.mVertex[2];
            simplex.mBC[0] = uBC;
            simplex.mBC[1] = vBC;
            simplex.mCount = 2;
            break;
        }
        if (vABC <= 0.0f && wACD <= 0.0f && uCA > 0.0f && vCA > 0.0f)
        {
            // Region CA.
            simplex.mVertex[1] = simplex.mVertex[0];
            simplex.mVertex[0] = simplex.mVertex[2];
            simplex.mBC[0] = uCA;
            simplex.mBC[1] = vCA;
            simplex.mCount = 2;
            break;
        }
        if (vCBD <= 0.0f && uACD <= 0.0f && uDC > 0.0f && vDC > 0.0f)
        {
            // Region DC.
            simplex.mVertex[0] = simplex.mVertex[3];
            simplex.mVertex[1] = simplex.mVertex[2];
            simplex.mBC[0] = uDC;
            simplex.mBC[1] = vDC;
            simplex.mCount = 2;
            break;
        }
        if (vACD <= 0.0f && wADB <= 0.0f && uAD > 0.0f && vAD > 0.0f)
        {
            // Region AD.
            simplex.mVertex[1] = simplex.mVertex[3];
            simplex.mBC[0] = uAD;
            simplex.mBC[1] = vAD;
            simplex.mCount = 2;
            break;
        }
        if (uCBD <= 0.0f && uADB <= 0.0f && uBD > 0.0f && vBD > 0.0f)
        {
            // Region BD.
            simplex.mVertex[0] = simplex.mVertex[1];
            simplex.mVertex[1] = simplex.mVertex[3];
            simplex.mBC[0] = uBD;
            simplex.mBC[1] = vBD;
            simplex.mCount = 2;
            break;
        }
        // Calculate fractional volume (volume can be negative!).
        const f32 denominator = TripleProduct(cb, ab, db);
        const f32 volume = (denominator == 0.0f) ? 1.0f : 1.0f / denominator;
        const f32 uABCD = TripleProduct(c, d, b) * volume;
        const f32 vABCD = TripleProduct(c, a, d) * volume;
        const f32 wABCD = TripleProduct(d, a, b) * volume;
        const f32 xABCD = TripleProduct(b, a, c) * volume;

        // Check faces for closest point.
        if (xABCD < 0.0f && uABC > 0.0f && vABC > 0.0f && wABC > 0.0f)
        {
            // Region ABC.
            simplex.mBC[0] = uABC;
            simplex.mBC[1] = vABC;
            simplex.mBC[2] = wABC;
            simplex.mCount = 3;
            break;
        }
        if (uABCD < 0.0f && uCBD > 0.0f && vCBD > 0.0f && wCBD > 0.0f)
        {
            // Region CBD.
            simplex.mVertex[0] = simplex.mVertex[2];
            simplex.mVertex[2] = simplex.mVertex[3];
            simplex.mBC[0] = uCBD;
            simplex.mBC[1] = vCBD;
            simplex.mBC[2] = wCBD;
            simplex.mCount = 3;
            break;
        }
        if (vABCD < 0.0f && uACD > 0.0f && vACD > 0.0f && wACD > 0.0f)
        {
            // Region ACD.
            simplex.mVertex[1] = simplex.mVertex[2];
            simplex.mVertex[2] = simplex.mVertex[3];
            simplex.mBC[0] = uACD;
            simplex.mBC[1] = vACD;
            simplex.mBC[2] = wACD;
            simplex.mCount = 3;
            break;
        }
        if (wABCD < 0.0f && uADB > 0.0f && vADB > 0.0f && wADB > 0.0f)
        {
            // Region ADB.
            simplex.mVertex[2] = simplex.mVertex[1];
            simplex.mVertex[1] = simplex.mVertex[3];
            simplex.mBC[0] = uADB;
            simplex.mBC[1] = vADB;
            simplex.mBC[2] = wADB;
            simplex.mCount = 3;
            break;
        }
        // Region ABCD.
        assert(uABCD >= 0.0f && vABCD >= 0.0f && wABCD >= 0.0f && xABCD >= 0.0f);
        simplex.mBC[0] = uABCD;
        simplex.mBC[1] = vABCD;
        simplex.mBC[2] = wABCD;
        simplex.mBC[3] = xABCD;
        simplex.mCount = 4;
    }
    break;
    }

    // Check if origin is enclosed by tetrahedron.
    if (simplex.mCount == 4)
    {
        simplex.mHit = 1;
        return false;
    }
    // Ensure closing in on origin to prevent multi-step cycling.
    Vec3 point{};
    f32 denominator = 0.0f;
    for (int i = 0; i < simplex.mCount; ++i)
    {
        denominator += simplex.mBC[i];
    }
    assert(denominator != 0.0f);
    denominator = 1.0f / denominator;

    switch (simplex.mCount)
    {
    case 1:
        point = simplex.mVertex[0].mP;
        break;
    case 2:
    {
        // Line.
        const Vec3 a = simplex.mVertex[0].mP * (denominator * simplex.mBC[0]);
        const Vec3 b = simplex.mVertex[1].mP * (denominator * simplex.mBC[1]);
        point = a + b;
    }
    break;
    case 3:
    {
        // Triangle.
        const Vec3 a = simplex.mVertex[0].mP * (denominator * simplex.mBC[0]);
        const Vec3 b = simplex.mVertex[1].mP * (denominator * simplex.mBC[1]);
        const Vec3 c = simplex.mVertex[2].mP * (denominator * simplex.mBC[2]);
        point = a + b + c;
    }
    break;
    }

    const f32 d2 = Dot(point, point);
    if (d2 >= simplex.mD)
    {
        return false;
    }
    simplex.mD = d2;

    // New search direction.
    Vec3 d{};
    switch (simplex.mCount)
    {
    default:
        assert(0);
        break;
    case 1:
    {
        // Point.
        d = -simplex.mVertex[0].mP;
    }
    break;
    case 2:
    {
        // Line segment.
        const Vec3 ba = simplex.mVertex[1].mP - simplex.mVertex[0].mP;
        const Vec3 b0 = -simplex.mVertex[1].mP;
        d = Cross(Cross(ba, b0), ba);
    }
    break;
    case 3:
    {
        // Triangle.
        const Vec3 ab = simplex.mVertex[1].mP - simplex.mVertex[0].mP;
        const Vec3 ac = simplex.mVertex[2].mP - simplex.mVertex[0].mP;
        const Vec3 n = Cross(ab, ac);
        if (Dot(n, simplex.mVertex[0].mP) <= 0.0f)
        {
            d = n;
        }
        else
        {
            d = -n;
        }
    }
    }
    if (Dot(d, d) < FLT_EPSILON * FLT_EPSILON)
    {
        return false;
    }

    support.mDirectionA = -d;
    support.mDirectionB = d;
    ++simplex.mIterations;

    return true;
}

void GjkAnalyze(GjkResult& result, const GjkSimplex& simplex)
{
    result.mIterations = simplex.mIterations;
    result.mHit = simplex.mHit;

    // Calculate normalization denominator.
    f32 denominator = 0.0f;
    for (int i = 0; i < simplex.mCount; ++i)
    {
        denominator += simplex.mBC[i];
    }
    denominator = 1.0f / denominator;

    // Compute closest points.
    switch (simplex.mCount)
    {
    default:
        assert(0);
        break;
    case 1:
    {
        // Point.
        result.mP0 = simplex.mVertex[0].mA;
        result.mP1 = simplex.mVertex[0].mB;
    }
    break;
    case 2:
    {
        // Line.
        const f32 as = denominator * simplex.mBC[0];
        const f32 bs = denominator * simplex.mBC[1];

        const Vec3 a = simplex.mVertex[0].mA * as;
        const Vec3 b = simplex.mVertex[1].mA * bs;
        const Vec3 c = simplex.mVertex[0].mB * as;
        const Vec3 d = simplex.mVertex[1].mB * bs;
        result.mP0 = a + b;
        result.mP1 = c + d;
    }
    break;
    case 3:
    {
        // Triangle.
        const f32 as = denominator * simplex.mBC[0];
        const f32 bs = denominator * simplex.mBC[1];
        const f32 cs = denominator * simplex.mBC[2];

        const Vec3 a = simplex.mVertex[0].mA * as;
        const Vec3 b = simplex.mVertex[1].mA * bs;
        const Vec3 c = simplex.mVertex[2].mA * cs;
        const Vec3 d = simplex.mVertex[0].mB * as;
        const Vec3 e = simplex.mVertex[1].mB * bs;
        const Vec3 f = simplex.mVertex[2].mB * cs;

        result.mP0 = a + b + c;
        result.mP1 = d + e + f;
    }
    break;
    case 4:
    {
        // Tetrahedron.
        const Vec3 a = simplex.mVertex[0].mA * (denominator * simplex.mBC[0]);
        const Vec3 b = simplex.mVertex[1].mA * (denominator * simplex.mBC[1]);
        const Vec3 c = simplex.mVertex[2].mA * (denominator * simplex.mBC[2]);
        const Vec3 d = simplex.mVertex[3].mA * (denominator * simplex.mBC[3]);

        result.mP0 = a + b + c + d;
        result.mP1 = result.mP0;
    }
    break;
    }
}
