#include "frontend/GraphicsAutoDetect.h"
#include "assets/Json.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace frontend {

std::string HardwareFacts::gpuId() const {
    char b[32];
    std::snprintf(b, sizeof b, "%04X:", gpuVendorId);
    return b + gpuName;
}

bool GraphicsPresetTable::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return loadFromString(ss.str());
}

bool GraphicsPresetTable::loadFromString(const std::string& text) {
    assets::Json j;
    if (!assets::Json::parse(text, j) || !j["tiers"].isArray() || j["tiers"].size() == 0) return false;
    integrated_.clear();
    for (size_t i = 0; i < j["integratedKeywords"].size(); ++i) integrated_.push_back(j["integratedKeywords"][i].asString());
    std::vector<Tier> tiers;
    for (size_t i = 0; i < j["tiers"].size(); ++i) {
        const assets::Json& t = j["tiers"][i];
        Tier x;
        x.name = t["name"].asString();
        x.minVramMB = t["minVramMB"].asInt(0);
        x.minCpuCores = t["minCpuCores"].asInt(0);
        x.allowIntegrated = t["allowIntegrated"].asBool(true);
        x.nativePixelBudget = (long long)t["nativePixelBudget"].asInt(921600);
        x.upscalingOverBudget = t["upscalingOverBudget"].asInt(2);
        x.maxHeight = t["maxHeight"].asInt(0);
        x.anisotropy = t["anisotropy"].asInt(4);
        x.hdTextures = t["hdTextures"].asBool(false);
        x.textureQuality = t["textureQuality"].asInt(2);
        x.fullscreen = t["fullscreen"].asBool(true);
        x.vsync = t["vsync"].asBool(false);
        x.frameLimit = t["frameLimit"].asString() == "refresh" ? -1 : t["frameLimit"].asInt(0);
        tiers.push_back(x);
    }
    tiers_ = tiers;
    return true;
}

bool GraphicsPresetTable::isIntegrated(const std::string& name) const {
    for (const std::string& k : integrated_) if (!k.empty() && name.find(k) != std::string::npos) return true;
    return false;
}

GraphicsPreset GraphicsPresetTable::pick(const HardwareFacts& f) const {
    const bool integrated = isIntegrated(f.gpuName);
    const Tier* chosen = &tiers_.back();   // the last tier takes everything
    for (const Tier& t : tiers_) {
        if (f.vramMB < t.minVramMB || f.cpuCores < t.minCpuCores) continue;
        if (integrated && !t.allowIntegrated) continue;
        chosen = &t;
        break;
    }
    const Tier& t = *chosen;
    GraphicsPreset p;
    p.tier = t.name;
    // Resolution: the monitor's native mode, or the largest same-aspect size with height <= maxHeight.
    int w = f.nativeW > 0 ? f.nativeW : 1920, h = f.nativeH > 0 ? f.nativeH : 1080;
    if (t.maxHeight > 0 && h > t.maxHeight) { w = (int)((long long)w * t.maxHeight / h); w -= w % 2; h = t.maxHeight; }
    p.width = w;
    p.height = h;
    p.fullscreen = t.fullscreen;
    p.vsync = t.vsync;
    p.frameLimit = t.frameLimit < 0 ? (f.nativeHz > 0 ? f.nativeHz : 0) : t.frameLimit;
    p.anisotropy = t.anisotropy >= 16 ? 16 : t.anisotropy >= 8 ? 8 : 4;
    p.textureQuality = t.textureQuality < 0 ? 0 : t.textureQuality > 2 ? 2 : t.textureQuality;
    p.hdTextures = t.hdTextures && f.hdTexturePack;
    p.upscaling = (long long)w * h > t.nativePixelBudget ? t.upscalingOverBudget : 0;
    char why[256];
    std::snprintf(why, sizeof why, "vram %d MB, %d cores%s; %dx%d = %lld px vs native budget %lld: %s", f.vramMB, f.cpuCores,
                  integrated ? ", integrated" : "", w, h, (long long)w * h, t.nativePixelBudget, p.upscaling ? "upscaling" : "native");
    p.why = why;
    return p;
}

} // namespace frontend
