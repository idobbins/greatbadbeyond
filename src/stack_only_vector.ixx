//====================================================================================
// Global module fragment
//====================================================================================
module;

#include <array>
#include <vector>
#include <memory_resource>
#include <cstddef>   // std::byte, std::max_align_t
#include <cassert>

//====================================================================================
// Module: StackOnlyVector
//====================================================================================
export module StackOnlyVector;

export template<typename T, std::size_t Bytes>
class StackOnlyVector
{
    static_assert(Bytes > 0, "StackOnlyVector cannot be empty");
    alignas(std::max_align_t) std::array<std::byte, Bytes> backingBuffer;

    std::pmr::monotonic_buffer_resource resource {
        backingBuffer.data(),
        backingBuffer.size(),
        std::pmr::null_memory_resource()
    };

    std::pmr::vector<T> data {&resource};

    void push_back(const T &value)
    {
        data.push_back(value);
    }
};
