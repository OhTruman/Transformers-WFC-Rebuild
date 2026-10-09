// Clean-room reconstruction — graphics auto-detect (PC EXTENSION: not in the original).
// On first launch (no saved PC settings) or when the GPU changes, the PC's facts pick a preset from
// data/frontend/graphics_presets.json ("the best look the PC can hold"); Graphics -> Recommended Settings re-applies it.
// The user can change every value afterwards. Ray Tracing / Frame Generation are never enabled here.
#pragma once
#include <string>
#include <vector>

namespace frontend {

struct HardwareFacts {
    std::string gpuVendor, gpuName;   // GL vendor / renderer strings (Rendering's gpuFacts)
    unsigned gpuVendorId = 0;         // PCI vendor (0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel), 0 unknown
    int vramMB = 0;
    bool rayTracing = false, hdTexturePack = false;
    int cpuCores = 0;
    int nativeW = 0, nativeH = 0, nativeHz = 0;   // the monitor's current (desktop) mode
    std::string gpuId() const;                    // stored with the settings to re-detect when the GPU changes
};

struct GraphicsPreset {
    std::string tier, why;
    int width = 1280, height = 720;
    bool fullscreen = false, vsync = false;
    int frameLimit = 0, anisotropy = 4, upscaling = 0, textureQuality = 2;
    bool hdTextures = false;
};

class GraphicsPresetTable {
public:
    bool load(const std::string& path);           // false: built-in defaults stay (one conservative tier)
    bool loadFromString(const std::string& json);
    GraphicsPreset pick(const HardwareFacts& f) const;
    bool isIntegrated(const std::string& gpuName) const;

private:
    struct Tier {
        std::string name;
        int minVramMB = 0, minCpuCores = 0;
        bool allowIntegrated = true;
        long long nativePixelBudget = 921600;
        int upscalingOverBudget = 2, maxHeight = 1080, anisotropy = 4, textureQuality = 1;
        bool hdTextures = false, fullscreen = true, vsync = false;
        int frameLimit = -1;                      // -1 = the monitor's refresh rate
    };
    std::vector<std::string> integrated_;
    std::vector<Tier> tiers_{Tier{"low"}};
};

} // namespace frontend
