#pragma once

#include "../../Common.hpp"

#include <vector>

template <typename Handle, typename Resource>
struct Pool
{
    Handle CreateHandle(const Resource& resource)
    {
        DEBUG_ASSERT(mData.size() == mGenerations.size());

        if (mFreeList.empty())
        {
            mData.push_back(resource);
            mGenerations.push_back(0);
            return {
                .idx = u32(mData.size()) - 1U,
                .generation = 0,
            };
        }

        const u32 idx = mFreeList.back();
        mFreeList.pop_back();

        mData[idx] = resource;

        return {
            .idx = idx,
            .generation = mGenerations[idx],
        };
    }

    void DestroyHandle(Handle handle)
    {
        DEBUG_ASSERT(mData.size() == mGenerations.size());
        DEBUG_ASSERT(handle.idx < mData.size());

        mFreeList.push_back(handle.idx);

        ++mGenerations[handle.idx];
    }

    Resource* GetPtr(Handle handle)
    {
        DEBUG_ASSERT(mData.size() == mGenerations.size());
        DEBUG_ASSERT(handle.idx < mData.size());

        if (handle.generation != mGenerations[handle.idx])
        {
            return nullptr;
        }

        return &mData[handle.idx];
    }

    const Resource* GetPtr(Handle handle) const
    {
        DEBUG_ASSERT(mData.size() == mGenerations.size());
        DEBUG_ASSERT(handle.idx < mData.size());

        if (handle.generation != mGenerations[handle.idx])
        {
            return nullptr;
        }

        return &mData[handle.idx];
    }

private:
    // TODO: fixed size.
    // TODO: split to cold and hot data?
    std::vector<Resource> mData;
    std::vector<u32> mGenerations;
    std::vector<u32> mFreeList;
};
