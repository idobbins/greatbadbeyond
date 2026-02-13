#include <greadbadbeyond.h>
#include <config.h>
#include <utils.h>

#include <fstream>
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
    vector<byte> bytes;
    bool ready;
} ManifestBlob;

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

    ifstream file(blobPath, ios::binary | ios::ate);
    if (!file.is_open())
    {
        LogWarn("[manifest] Pack not found at %s", blobPath.c_str());
        ManifestBlob.ready = false;
        return;
    }

    streamsize size = file.tellg();
    if (size <= 0)
    {
        LogWarn("[manifest] Pack is empty at %s", blobPath.c_str());
        ManifestBlob.bytes.clear();
        ManifestBlob.ready = false;
        return;
    }

    file.seekg(0, ios::beg);
    ManifestBlob.bytes.resize(static_cast<size_t>(size));
    bool readOk = static_cast<bool>(file.read(reinterpret_cast<char *>(ManifestBlob.bytes.data()), size));
    Assert(readOk, "Failed to read manifest pack file");

    ManifestBlob.ready = true;
    LogInfo("[manifest] Loaded pack (%u bytes)", static_cast<u32>(ManifestBlob.bytes.size()));
}

void DestroyManifestBlob()
{
    ManifestBlob.bytes.clear();
    ManifestBlob.ready = false;
}

auto IsManifestBlobReady() -> bool
{
    return ManifestBlob.ready;
}

auto GetManifestBlobBytes() -> span<const byte>
{
    if (!ManifestBlob.ready || ManifestBlob.bytes.empty())
    {
        return {};
    }

    return {ManifestBlob.bytes.data(), ManifestBlob.bytes.size()};
}
