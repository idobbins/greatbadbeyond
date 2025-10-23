//====================================================================================
// Global module fragment
//====================================================================================
module;

#include <cstdio>
#include <cstdlib>
#include <string_view>

//====================================================================================
// Module: assert
//====================================================================================
export module assert;

export inline void Assert(bool condition, std::string_view message)
{
    if (!condition) {
        std::fprintf(stderr, "Runtime assertion failed: %.*s\n",
            static_cast<int>(message.size()), message.data());
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }
}
