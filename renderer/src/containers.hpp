#pragma once

#include "temp_allocator.hpp"

#include <EASTL/vector.h>

template<typename T>
using TempVector = eastl::vector<T, TempAllocator>;