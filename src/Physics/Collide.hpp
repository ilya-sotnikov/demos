#pragma once

#include "Config.hpp"
#include "World.hpp"

void Collide(ContactManifold& manifold, const World& world, const Body& body1, const Body& body2);
