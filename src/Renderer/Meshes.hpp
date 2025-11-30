#pragma once

#include "../Common.hpp"
#include "../Arena.hpp"
#include "../Math/Types.hpp"

void GetCubeData(Slice<Vec3>& positions, Slice<u16>& indices, Slice<Vec3>* normals, Arena& arena);
void GetSphereData(Slice<Vec3>& positions, Slice<u16>& indices, Slice<Vec3>* normals, Arena& arena);
void GetTetrahedronData(
    Slice<Vec3>& positions,
    Slice<u16>& indices,
    Slice<Vec3>* normals,
    Arena& arena
);
