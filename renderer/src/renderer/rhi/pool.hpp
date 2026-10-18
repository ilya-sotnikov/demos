#pragma once

#include "common.hpp"

#include <EASTL/vector.h>

template<typename Handle, typename Resource>
struct Pool {
    Handle create_handle(const Resource& resource) {
        DEBUG_ASSERT(m_data.size() == m_generations.size());

        if (m_free_list.empty()) {
            m_data.push_back(resource);
            m_generations.push_back(0);
            return {
                .idx = u32(m_data.size()) - 1U,
                .generation = 0,
            };
        }

        const u32 idx = m_free_list.back();
        m_free_list.pop_back();

        m_data[idx] = resource;

        return {
            .idx = idx,
            .generation = m_generations[idx],
        };
    }

    void destroy_handle(Handle handle) {
        DEBUG_ASSERT(m_data.size() == m_generations.size());
        DEBUG_ASSERT(handle.idx < m_data.size());

        m_free_list.push_back(handle.idx);

        ++m_generations[handle.idx];
    }

    Resource* get_ptr(Handle handle) {
        DEBUG_ASSERT(m_data.size() == m_generations.size());
        DEBUG_ASSERT(handle.idx < m_data.size());

        if (handle.generation != m_generations[handle.idx]) {
            return nullptr;
        }

        return &m_data[handle.idx];
    }

    const Resource* get_ptr(Handle handle) const {
        DEBUG_ASSERT(m_data.size() == m_generations.size());
        DEBUG_ASSERT(handle.idx < m_data.size());

        if (handle.generation != m_generations[handle.idx]) {
            return nullptr;
        }

        return &m_data[handle.idx];
    }

private:
    // TODO: Fixed size.
    // TODO: Split to cold and hot data?
    // TODO: Generations are only relevant in debug builds.
    eastl::vector<Resource> m_data;
    eastl::vector<u32> m_generations;
    eastl::vector<u32> m_free_list;
};
