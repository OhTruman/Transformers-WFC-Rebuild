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
    bool rayTracing = false, hdTexturePack = false;   // hdTexturePack: the Faithful HD root (Rendering's hdTexturesAvailable)
    bool hdEnhanced = false;          // the Enhanced HD root (Rendering's hdTextureLevelAvailable(2))
    bool fsr3 = false;                // the FSR 3.1 runtime ships beside the exe (Rendering's upscalingModeAvailable(4))
    bool frameGen = false, frameGenActive = false;   // FSR frame generation can run / did start this session (Rendering)
    bool volumetricAvailable = false; // Rendering's volumetricLightingAvailable(): the X3 volumetrics of Realistic can run
    bool rtAvailable = false;         // ray-traced shadows / lighting can run (Rendering's rayTracingAvailable(): DXR 1.1 +
                                      // the D3D12 runtime) - the Ray Tracing row shows only then
    int cpuCores = 0;
    int nativeW = 0, nativeH = 0, nativeHz = 0;   // the monitor's current (desktop) mode
    std::string gpuId() const;                    // stored with the settings to re-detect when the GPU changes
};

// HD textures are Off by default and never set by auto-detect (user decision 2026-10-12, reversed the same day: "HD
// texture's should be off my default too"). Where the pack is installed and the GPU has at least ~6 GB of video memory
// (6 GB cards report 6000-6144 MB), a one-time message points to the row.
// Below a level's floor the row only warns (Experimental's review): it never blocks. Systems' per-map table: Faithful
// adds up to +317 MB of map textures over the originals, Enhanced (BC4 roughness at HD size) up to +554 MB; the whole
// process measured 2.9-3.3 GB device-local with RT on at 1440p, so Enhanced + RT is ~3.9 GB worst case. 6000 MB leaves
// ~2 GB for the OS / compositor and 4K targets; Enhanced together with ray tracing at 4K output wants 8000 MB.
constexpr int kHdTexturesMinVramMB = 6000;
constexpr int kHdEnhancedMinVramMB = 6000;
constexpr int kHdEnhancedRtMinVramMB = 8000;
// The level handed to the renderer this session: a stored level whose root is missing falls to the next lower installed
// one (Enhanced -> Faithful -> Off, never up). The stored choice is kept as it is.
inline int hdTexturesEffective(int stored, bool faithful, bool enhanced) {
    if (stored >= 2 && enhanced) return 2;
    return stored >= 1 && faithful ? 1 : 0;
}
inline bool hdTexturesSuggested(const HardwareFacts& f) { return (f.hdTexturePack || f.hdEnhanced) && f.vramMB >= kHdTexturesMinVramMB; }

struct GraphicsPreset {
    std::string tier, why;
    int width = 1280, height = 720;
    bool fullscreen = false, vsync = false;
    int frameLimit = 0, anisotropy = 4, upscaling = 0, textureQuality = 2;   // HD textures: never (user choice)
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
        bool fullscreen = true, vsync = false;
        int frameLimit = -1;                      // -1 = the monitor's refresh rate
    };
    std::vector<std::string> integrated_;
    std::vector<Tier> tiers_{Tier{"low"}};
};

} // namespace frontend
