#pragma once

#define PHYSICS_DEBUG
// #define PHYSICS_COLLIDE_ONLY
// #define PHYSICS_NO_BROADPHASE

static constexpr int PHYSICS_MAX_BODIES = 256;
static constexpr int PHYSICS_MAX_CONVEX_HULLS = 128;
// NOTE: arbitrary choice, 4 manifolds per body, *2 to reduce hash table load.
static constexpr int PHYSICS_MAX_CONTACT_MANIFOLDS = PHYSICS_MAX_BODIES * 4 * 2;
