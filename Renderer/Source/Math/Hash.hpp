#pragma once

#include "../Common.hpp"

namespace Hash
{

// https://nullprogram.com/blog/2018/07/31/
inline u64 Splittable64(u64 x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9U;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebU;
    x ^= x >> 31;
    return x;
}

}
