#pragma once

#define GJK_MAX_ITERATIONS 20

#include "../Common.hpp"
#include "../Math/Types.hpp"

// https://gist.github.com/vurtun/29727217c269a2fbf4c0ed9a1d11cb40

struct GjkSupport
{
    int mIdA; // In.
    int mIdB; // In.
    Vec3 mA; // In.
    Vec3 mB; // In.
    Vec3 mDirectionA; // Out.
    Vec3 mDirectionB; // Out.
};
struct GjkSimplex
{
    int mIterations;
    int mHit;
    int mCount;
    struct Vertex
    {
        Vec3 mA;
        Vec3 mB;
        Vec3 mP;
        int mIdA;
        int mIdB;
    } mVertex[4];
    Vec4 mBC;
    f32 mD;
};
struct GjkResult
{
    int mHit;
    Vec3 mP0;
    Vec3 mP1;
    int mIterations;
};
bool Gjk(GjkSimplex& simplex, GjkSupport& support);
void GjkAnalyze(GjkResult& result, const GjkSimplex& simplex);
