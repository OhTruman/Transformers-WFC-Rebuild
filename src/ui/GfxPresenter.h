// Clean-room reconstruction — frontend presenter: runs and draws the shipped GFx movies the flow has open
// (GFxAction_OpenMovie / UIController OpenUI / the loading movie), routes their ExternalInterface calls and
// fscommands to the frontend runtime, feeds engine -> AS callbacks (loading text, data-store changes) and gives the
// focused movie the UI keys (the key codes the movies' KeyListener expects).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "frontend/FrontendRuntime.h"
#include "ui/GfxHost.h"
#include "ui/gl/GfxRendererGL.h"

namespace ui {

class GfxPresenter : public frontend::IMoviePresenter {
public:
    explicit GfxPresenter(frontend::FrontendRuntime& rt) : rt_(rt) {}
    bool init();
    bool runsMovie(const std::string& movie) const override;
    void update(frontend::GameFlow& flow, const platform::InputFrame& in, float dt) override;
    void draw(const frontend::GameFlow& flow, int w, int h) override;
    void injectKey(int code, bool down) override;
    void setVideoFrame(const uint8_t* rgba, int w, int h, uint64_t serial, bool over) override {
        video_ = rgba; videoW_ = w; videoH_ = h; videoSerial_ = serial; videoOver_ = over;
    }
    // Diagnostics.
    std::vector<std::string> openMovieObjects() const;
    std::string dumpMovie(const std::string& object) const;
    bool hasLoadingMovie() const { return loading_ != nullptr; }
    float loadingSeconds() const { return loadingTime_; }
    void ownedGl(GlCensus::Owned& o) const { gl_.ownedNames(o); }
    bool drawsCursor() const override { return cursor_ != nullptr; }
    void advanceLoading(float dt) override {
        if (loading_) { loading_->advance(dt); loadingTime_ += dt; }
        if (cursor_) cursor_->advance(dt);
    }
    // Automation (clickclip:): the window position of a clip's centre in the focused movie.
    bool clipWindowCenter(const std::string& path, int& x, int& y);

private:
    void deliverMouse(const platform::InputFrame& in);
    gfx::Player* focusPlayer();
    GfxMovie* openMovie(const std::string& object);
    gfx::avm1::Value bridge(GfxMovie& m, const std::string& fn, gfx::avm1::Args& a);
    void fsCommand(GfxMovie& m, const std::string& cmd, const std::string& arg);
    void syncMovies(frontend::GameFlow& flow);
    void deliverKeys(const platform::InputFrame& in);

    frontend::FrontendRuntime& rt_;
    GfxLibrary lib_;
    GfxRendererGL gl_;
    bool glReady_ = false;
    struct Open { std::string object; std::unique_ptr<GfxMovie> movie; };
    std::vector<Open> movies_;              // flow-open movies in open order (last = focus)
    const uint8_t* video_ = nullptr;
    int videoW_ = 0, videoH_ = 0;
    uint64_t videoSerial_ = 0;
    bool videoOver_ = false;
    std::unique_ptr<GfxMovie> loading_;     // the loading movie while a travel is in progress
    std::string loadingUrl_;
    float loadingTime_ = 0.0f;
    uint32_t prevUi_ = 0;
    // TnUIController.MouseCursorUI (Cursor_GFX, Depth 1000000): started by Initialize, always on top [CONFIRMED].
    std::unique_ptr<GfxMovie> cursor_;
    int viewW_ = 1280, viewH_ = 720;          // last drawn window size (pointer -> stage mapping)
    bool prevMouseLeft_ = false;
    gfx::Player* mouseTarget_ = nullptr;      // movie that last received the pointer
    std::vector<gfx::Player::RenderItem> items_;
};

} // namespace ui
