#include "ui/GfxHost.h"
#include "assets/Json.h"
#include "core/Log.h"
#include "frontend/Catalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ui {

namespace {
std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string baseNoExt(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    std::string b = s == std::string::npos ? p : p.substr(s + 1);
    size_t d = b.rfind('.');
    return d == std::string::npos ? b : b.substr(0, d);
}
} // namespace

bool GfxLibrary::load(const std::string& manifestRoot, const std::string& extractedRoot) {
    extracted_ = extractedRoot;
    std::ifstream f(manifestRoot + "/frontend_gfx.json", std::ios::binary);
    if (!f) { LOG_WARN("GFX library: no frontend_gfx.json under %s", manifestRoot.c_str()); return false; }
    std::stringstream ss; ss << f.rdbuf();
    assets::Json j;
    if (!assets::Json::parse(ss.str(), j)) return false;
    for (const auto& [k, m] : j["movies"].obj) {
        std::string file = m["file"].asString();
        if (file.empty()) continue;
        std::string path = extractedRoot + "/" + file;
        byBase_[lower(baseNoExt(file))] = path;
        const assets::Json& tex = m["ue_objects"]["movie_instance"]["ExternalTextures"];
        for (size_t i = 0; i < tex.size(); ++i) {
            std::string res = tex[i]["Resource"].asString(), obj = tex[i]["Texture"].asString();
            size_t dot = obj.find('.');
            if (res.empty() || dot == std::string::npos) continue;
            extTextures_[res] = extractedRoot + "/content/" + obj.substr(0, dot) + "/" + obj.substr(dot + 1) + ".png";
        }
    }
    fontLib_ = byBase_.count("fonts_efigs") ? byBase_["fonts_efigs"] : "";
    // [Fonts] of TransGame GFxUI.int: HeaderFont / NormalFont / TitleFont [CONFIRMED].
    std::ifstream g(extractedRoot + "/config/Coalesced_int/TransGame/Localization/INT/GFxUI.int");
    std::string line, section;
    while (std::getline(g, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') { section = line; continue; }
        size_t eq = line.find('=');
        if (section == "[Fonts]" && eq != std::string::npos) fontMap_["$" + line.substr(0, eq)] = line.substr(eq + 1);
    }
    LOG_INFO("GFX library: %zu movies, %zu external textures, font lib %s, %zu font aliases", byBase_.size(), extTextures_.size(),
             fontLib_.c_str(), fontMap_.size());
    return !byBase_.empty();
}

std::string GfxLibrary::movieFileForObject(const std::string& object) const {
    // "UI_GFxFrontEnd_p.FrontEnd_GFX_1" (movie instance) or "UI_GFxLoading_p.LoadScreen_GFX" (movie info).
    size_t dot = object.find('.');
    std::string name = dot == std::string::npos ? object : object.substr(dot + 1);
    if (name.size() > 2 && name[name.size() - 2] == '_' && std::isdigit((unsigned char)name.back())) name = name.substr(0, name.size() - 2);
    auto it = byBase_.find(lower(name));
    return it == byBase_.end() ? "" : it->second;
}

std::string GfxLibrary::resolveUrl(const std::string& url) const {
    std::string b = lower(baseNoExt(url));
    if (b == "gfxfontlib") return fontLib_;
    auto it = byBase_.find(b + "_gfx");
    if (it != byBase_.end()) return it->second;
    it = byBase_.find(b);
    return it == byBase_.end() ? "" : it->second;
}

std::string GfxLibrary::externalTexture(const std::string& resource) const {
    auto it = extTextures_.find(resource);
    return it == extTextures_.end() ? "" : it->second;
}

bool GfxMovie::open(const GfxLibrary& lib, const frontend::Catalog* catalog, const std::string& object, ExternalCall ec, FsCommand fs) {
    object_ = object;
    std::string path = lib.movieFileForObject(object);
    if (path.empty()) { LOG_WARN("GFX movie object %s has no file", object.c_str()); return false; }
    player_ = std::make_unique<gfx::Player>();
    gfx::Player& p = *player_;
    p.resolveMovieUrl = [&lib](const std::string& url, const std::string&) { return lib.resolveUrl(url); };
    p.externalTexture = [this, &lib](const std::string& r) {
        auto o = textureOverrides.find(r);
        return o != textureOverrides.end() ? o->second : lib.externalTexture(r);
    };
    p.fontLibPath = lib.fontLib();
    p.fontMap = lib.fontMap();
    if (catalog) p.translator = [catalog](const std::string& k) { return catalog->localizeKey(k); };
    p.vm().externalCall = [this, ec](const std::string& fn, gfx::avm1::Args& a) { return ec ? ec(*this, fn, a) : gfx::avm1::Value(); };
    p.vm().fsCommand = [this, fs](const std::string& c, const std::string& a) { if (fs) fs(*this, c, a); };
    if (!p.loadRoot(path)) return false;
    LOG_INFO("GFX opened %s (%s)", object.c_str(), path.c_str());
    return true;
}

float GfxMovie::frameRate() const {
    const gfx::MovieDef* d = player_ ? player_->rootDef() : nullptr;
    return d && d->frameRate > 0 ? d->frameRate : 30.0f;
}

void GfxMovie::advance(float dt) {
    if (!player_) return;
    accum_ += dt;
    float step = 1.0f / frameRate();
    int n = 0;
    while (accum_ >= step && n < 4) { accum_ -= step; player_->advance(step); ++n; }
    if (n == 4) accum_ = 0;
}

void GfxMovie::setExternalTexture(const std::string& resource, const std::string& png) {
    textureOverrides[resource] = png;
    std::function<void(gfx::DisplayObject*)> rec = [&](gfx::DisplayObject* d) {
        if (d->kind == gfx::DisplayObject::Kind::Bitmap) {
            auto* b = static_cast<gfx::BitmapInstance*>(d);
            if (b->bitmap && b->bitmap->exportName == resource) b->path = png;
        }
        if (d->kind == gfx::DisplayObject::Kind::Clip)
            for (auto& [k, ch] : static_cast<gfx::MovieClip*>(d)->children) rec(ch.get());
    };
    if (player_ && player_->root()) rec(player_->root());
}

void GfxMovie::key(int code, bool down) { if (player_) player_->keyEvent(code, down); }
bool GfxMovie::textInput(char32_t c) { return player_ && player_->textInput(c); }
bool GfxMovie::hasTextFocus() const { return player_ && player_->textFocus() != nullptr; }

gfx::avm1::Value GfxMovie::invoke(const std::string& path, gfx::avm1::Args args) {
    if (!player_) return {};
    gfx::avm1::Value v = player_->vm().invokePath(path, std::move(args), player_->root());
    player_->drainActions();
    return v;
}

std::string GfxMovie::dumpTree() const {
    std::string out;
    std::function<void(const gfx::DisplayObject*, int)> rec = [&](const gfx::DisplayObject* d, int ind) {
        char b[512];
        const char* k = d->kind == gfx::DisplayObject::Kind::Clip ? "clip" : d->kind == gfx::DisplayObject::Kind::Shape ? "shape"
                       : d->kind == gfx::DisplayObject::Kind::Text ? "text" : d->kind == gfx::DisplayObject::Kind::Bitmap ? "bitmap" : "stext";
        std::string extra;
        if (d->kind == gfx::DisplayObject::Kind::Clip) {
            auto* mc = static_cast<const gfx::MovieClip*>(d);
            extra = " frame " + std::to_string(mc->frame + 1) + "/" + std::to_string(mc->totalFrames()) + (mc->playing ? " playing" : " stopped");
        }
        if (d->kind == gfx::DisplayObject::Kind::Text) extra = " \"" + static_cast<const gfx::TextField*>(d)->plainText() + "\"";
        if (d->kind == gfx::DisplayObject::Kind::Bitmap) extra = " " + static_cast<const gfx::BitmapInstance*>(d)->path;
        if (d->blend > 1) extra += " BLEND=" + std::to_string(d->blend);
        std::snprintf(b, sizeof b, "%*s%s '%s' depth %d char %u (%s) x=%.1f y=%.1f sx=%.2f sy=%.2f a=%.2f%s%s%s\n", ind * 2, "", k,
                      d->name.c_str(), d->depth - gfx::kDepthOffset, d->charId, d->def ? d->def->baseName().c_str() : "-",
                      d->matrix.tx / 20, d->matrix.ty / 20, d->matrix.a, d->matrix.d, d->cx.ma, d->visible ? "" : " HIDDEN",
                      d->clipDepth ? " MASK" : "", extra.c_str());
        out += b;
        if (d->kind == gfx::DisplayObject::Kind::Clip)
            for (const auto& [dd, ch] : static_cast<const gfx::MovieClip*>(d)->children) rec(ch.get(), ind + 1);
    };
    if (player_ && player_->root()) rec(player_->root(), 0);
    return out;
}

} // namespace ui
