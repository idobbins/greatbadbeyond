#include <greadbadbeyond.h>
#include <config.h>
#include <utils.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace std;

#ifndef ASSET_SOURCE_DIRECTORY
#define ASSET_SOURCE_DIRECTORY ""
#endif

static constexpr const char *AssetSourceDirectory = ASSET_SOURCE_DIRECTORY;
static constexpr const char *DefaultManifestBlobPath = "resources/external/kenney_assets.pack";

static struct ManifestBlobData
{
    const byte *mappedBytes;
    usize mappedSize;
    vector<byte> fallbackBytes;
#if defined(_WIN32)
    HANDLE fileHandle;
    HANDLE mappingHandle;
#else
    i32 fileDescriptor;
#endif
    bool ready;
} ManifestBlob = {
    .mappedBytes = nullptr,
    .mappedSize = 0,
    .fallbackBytes = {},
#if defined(_WIN32)
    .fileHandle = INVALID_HANDLE_VALUE,
    .mappingHandle = nullptr,
#else
    .fileDescriptor = -1,
#endif
    .ready = false,
};

void CreateManifestBlob()
{
    if (ManifestBlob.ready)
    {
        return;
    }

    string blobPath = DefaultManifestBlobPath;
    if (AssetSourceDirectory[0] != '\0')
    {
        blobPath = string(AssetSourceDirectory) + "/external/kenney_assets.pack";
    }

    auto readBufferedFallback = [&]() -> bool
    {
        ifstream file(blobPath, ios::binary | ios::ate);
        if (!file.is_open())
        {
            return false;
        }

        streamsize size = file.tellg();
        if (size <= 0)
        {
            return false;
        }

        file.seekg(0, ios::beg);
        ManifestBlob.fallbackBytes.resize(static_cast<size_t>(size));
        bool readOk = static_cast<bool>(file.read(reinterpret_cast<char *>(ManifestBlob.fallbackBytes.data()), size));
        if (!readOk)
        {
            ManifestBlob.fallbackBytes.clear();
            return false;
        }

        ManifestBlob.mappedBytes = nullptr;
        ManifestBlob.mappedSize = 0;
        ManifestBlob.ready = true;
        LogWarn("[manifest] Using buffered pack fallback (%llu bytes)",
            static_cast<unsigned long long>(ManifestBlob.fallbackBytes.size()));
        return true;
    };

#if defined(_WIN32)
    HANDLE fileHandle = CreateFileA(
        blobPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE)
    {
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Pack not found at %s", blobPath.c_str());
            ManifestBlob.ready = false;
        }
        return;
    }

    LARGE_INTEGER fileSize = {};
    bool fileSizeOk = GetFileSizeEx(fileHandle, &fileSize) != 0;
    if (!fileSizeOk || (fileSize.QuadPart <= 0))
    {
        CloseHandle(fileHandle);
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Pack is empty or unreadable at %s", blobPath.c_str());
            ManifestBlob.ready = false;
        }
        return;
    }

    if (static_cast<u64>(fileSize.QuadPart) > static_cast<u64>(numeric_limits<usize>::max()))
    {
        CloseHandle(fileHandle);
        Assert(false, "Pack is too large for address space");
        return;
    }

    HANDLE mappingHandle = CreateFileMappingA(fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mappingHandle == nullptr)
    {
        CloseHandle(fileHandle);
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Failed to create read-only file mapping");
            ManifestBlob.ready = false;
        }
        return;
    }

    void *mapped = MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, 0);
    if (mapped == nullptr)
    {
        CloseHandle(mappingHandle);
        CloseHandle(fileHandle);
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Failed to map pack view");
            ManifestBlob.ready = false;
        }
        return;
    }

    ManifestBlob.fileHandle = fileHandle;
    ManifestBlob.mappingHandle = mappingHandle;
    ManifestBlob.mappedBytes = reinterpret_cast<const byte *>(mapped);
    ManifestBlob.mappedSize = static_cast<usize>(fileSize.QuadPart);
    ManifestBlob.fallbackBytes.clear();
    ManifestBlob.ready = true;
    LogInfo("[manifest] Mapped pack (%llu bytes)", static_cast<unsigned long long>(ManifestBlob.mappedSize));
#else
    i32 fileDescriptor = open(blobPath.c_str(), O_RDONLY);
    if (fileDescriptor < 0)
    {
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Pack not found at %s", blobPath.c_str());
            ManifestBlob.ready = false;
        }
        return;
    }

    struct stat fileInfo = {};
    if (fstat(fileDescriptor, &fileInfo) != 0)
    {
        close(fileDescriptor);
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Failed to stat pack at %s (%s)", blobPath.c_str(), std::strerror(errno));
            ManifestBlob.ready = false;
        }
        return;
    }

    if (fileInfo.st_size <= 0)
    {
        close(fileDescriptor);
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Pack is empty at %s", blobPath.c_str());
            ManifestBlob.ready = false;
        }
        return;
    }

    if (static_cast<u64>(fileInfo.st_size) > static_cast<u64>(numeric_limits<usize>::max()))
    {
        close(fileDescriptor);
        Assert(false, "Pack is too large for address space");
        return;
    }

    void *mapped = mmap(nullptr, static_cast<size_t>(fileInfo.st_size), PROT_READ, MAP_PRIVATE, fileDescriptor, 0);
    if (mapped == MAP_FAILED)
    {
        close(fileDescriptor);
        if (!readBufferedFallback())
        {
            LogWarn("[manifest] Failed to mmap pack at %s (%s)", blobPath.c_str(), std::strerror(errno));
            ManifestBlob.ready = false;
        }
        return;
    }

    ManifestBlob.fileDescriptor = fileDescriptor;
    ManifestBlob.mappedBytes = reinterpret_cast<const byte *>(mapped);
    ManifestBlob.mappedSize = static_cast<usize>(fileInfo.st_size);
    ManifestBlob.fallbackBytes.clear();
    ManifestBlob.ready = true;
    LogInfo("[manifest] Mapped pack (%llu bytes)", static_cast<unsigned long long>(ManifestBlob.mappedSize));
#endif
}

void DestroyManifestBlob()
{
#if defined(_WIN32)
    if (ManifestBlob.mappedBytes != nullptr)
    {
        UnmapViewOfFile(ManifestBlob.mappedBytes);
        ManifestBlob.mappedBytes = nullptr;
    }
    if (ManifestBlob.mappingHandle != nullptr)
    {
        CloseHandle(ManifestBlob.mappingHandle);
        ManifestBlob.mappingHandle = nullptr;
    }
    if (ManifestBlob.fileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(ManifestBlob.fileHandle);
        ManifestBlob.fileHandle = INVALID_HANDLE_VALUE;
    }
#else
    if ((ManifestBlob.mappedBytes != nullptr) && (ManifestBlob.mappedSize > 0))
    {
        munmap(const_cast<byte *>(ManifestBlob.mappedBytes), ManifestBlob.mappedSize);
        ManifestBlob.mappedBytes = nullptr;
    }
    if (ManifestBlob.fileDescriptor >= 0)
    {
        close(ManifestBlob.fileDescriptor);
        ManifestBlob.fileDescriptor = -1;
    }
#endif

    ManifestBlob.mappedBytes = nullptr;
    ManifestBlob.mappedSize = 0;
    ManifestBlob.fallbackBytes.clear();
    ManifestBlob.ready = false;
}

auto IsManifestBlobReady() -> bool
{
    return ManifestBlob.ready;
}

auto GetManifestBlobBytes() -> span<const byte>
{
    if (!ManifestBlob.ready)
    {
        return {};
    }

    if ((ManifestBlob.mappedBytes != nullptr) && (ManifestBlob.mappedSize > 0))
    {
        return {ManifestBlob.mappedBytes, ManifestBlob.mappedSize};
    }

    if (!ManifestBlob.fallbackBytes.empty())
    {
        return {ManifestBlob.fallbackBytes.data(), ManifestBlob.fallbackBytes.size()};
    }

    return {};
}
