#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <config.h>

inline void Assert(bool condition, std::string_view message)
{
    if (!condition) {
        std::fprintf(stderr, "Runtime assertion failed: %.*s\n", static_cast<int>(message.size()), message.data());
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }
}

inline void LogWrite(FILE *stream, const char *prefix, const char *format, va_list args)
{
    fprintf(stream, "%s ", prefix);
    vfprintf(stream, format, args);
    fputc('\n', stream);
}

inline void LogError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stderr, LogErrorPrefix, format, args);
    va_end(args);
}

inline void LogWarn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stderr, LogWarnPrefix, format, args);
    va_end(args);
}

inline void LogInfo(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    LogWrite(stdout, LogInfoPrefix, format, args);
    va_end(args);
}
