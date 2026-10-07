// Clean-room reconstruction — host configuration of the GFx runtime with the shipped resource mapping:
//   * movie object -> .gfx file (UE3 GFxMovie / GFxMovieInfo objects, AssetTools frontend_gfx.json);
//   * relative .swf URLs used by imports / loadClip ("../_Shared/SharedComponents.swf") -> cooked "<Name>_GFX" movie;
//   * gfxfontlib.swf -> [FontLib] FontLib=UI_GFxFonts_p.Fonts_EFIGS, font aliases from [Fonts] (GFxUI.int) [CONFIRMED];
//   * ExternalTextures of the movie instances (e.g. TransformersWFCLogo_INT -> UI_TransformersWFCLogo_p texture) [CONFIRMED];
//   * GFx translation of "$File.Section.Key" strings ([Translation] Enable=1) through the frontend catalog.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/gfx/Display.h"

namespace frontend { class Catalog; }

namespace ui {

class GfxLibrary {
public:
    bool load(const std::string& manifestRoot, const std::string& extractedRoot);
    std::string movieFileForObject(const std::string& object) const;   // "UI_GFxFrontEnd_p.FrontEnd_GFX_1" -> .gfx
    std::string resolveUrl(const std::string& url) const;               // relative .swf -> .gfx
    std::string externalTexture(const std::string& resource) const;     // resource -> PNG ("" = none)
    const std::string& fontLib() const { return fontLib_; }
    const std::map<std::string, std::string>& fontMap() const { return fontMap_; }
    const std::string& extractedRoot() const { return extracted_; }
    // Keyboard prompts (PC ADAPTATION): a Gamepad* glyph's key text for a movie object ("" = draw the glyph), and the
    // generation every movie's text layout follows (bumped when the input device switches).
    std::function<std::string(const std::string& object, const std::string& image)> glyphLabel;
    unsigned promptGen = 0;

private:
    std::map<std::string, std::string> byBase_;      // lower("SharedComponents_GFX") -> path
    std::map<std::string, std::string> extTextures_; // resource -> png path
    std::map<std::string, std::string> fontMap_;     // "$TitleFont" -> face
    std::string fontLib_, extracted_;
};

// One open GFx movie (a GFxMoviePlayer instance) running its own ActionScript.
class GfxMovie {
public:
    using ExternalCall = std::function<gfx::avm1::Value(GfxMovie& m, const std::string& fn, gfx::avm1::Args& args)>;
    using FsCommand = std::function<void(GfxMovie& m, const std::string& cmd, const std::string& arg)>;

    bool open(const GfxLibrary& lib, const frontend::Catalog* catalog, const std::string& object, ExternalCall ec, FsCommand fs);
    const std::string& object() const { return object_; }
    gfx::Player& player() { return *player_; }
    float frameRate() const;
    // Runs whole movie frames at the authored frame rate.
    void advance(float dt);
    void key(int flashKeyCode, bool down);
    // A typed character for the focused input field (Selection focus); false when no input field has focus.
    bool textInput(char32_t c);
    bool hasTextFocus() const;
    gfx::avm1::Value invoke(const std::string& path, gfx::avm1::Args args);   // engine -> AS (_global.SetLevelText ...)
    std::string dumpTree() const;
    // Self.SetExternalTextureWithPath(resource, "Package.Texture"): replaces an external image at runtime.
    void setExternalTexture(const std::string& resource, const std::string& pngPath);
    bool closeRequested = false;
    std::map<std::string, std::string> textureOverrides;

private:
    std::unique_ptr<gfx::Player> player_;
    std::string object_;
    float accum_ = 0.0f;
};

} // namespace ui
