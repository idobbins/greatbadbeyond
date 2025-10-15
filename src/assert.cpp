#pragma once

#include <iostream>
#include <string_view>

inline void runtime_assert(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "Runtime assertion failed: " << message << std::endl;
        exit(EXIT_FAILURE);
    }
}
