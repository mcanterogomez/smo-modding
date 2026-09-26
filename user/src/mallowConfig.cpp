#include <mallow/mallow.hpp>
#include <nn/fs.h>
#include <nn/fs/fs_directories.h>
#include <nn/fs/fs_types.h>

#include "ModConfig.h"

namespace mallow::config {
    const char* path = "sd:/atmosphere/contents/0100000000010000/mod_config.json";
    const char* pathEmu = "sd:/mod_config.json";
    const char* defaultConfig = R"({
        "attackButton":"Y",
        "spinOnly":false,
        "galaxySfx":false,
        "enableMario":false
    })";

    Allocator* getAllocator() {
        static DefaultAllocator allocator = {};
        return &allocator;
    }
    ConfigBase* getConfig() {
        static ModConfig modConfig = {};
        return &modConfig;
    }
    bool isEmu() {
        nn::fs::DirectoryEntryType type = nn::fs::DirectoryEntryType_Directory; // stays set when the lookup fails (no package3 on emulator)
        nn::fs::GetEntryType(&type, "sd:/atmosphere/package3");
        return type != nn::fs::DirectoryEntryType_File;
    }
}