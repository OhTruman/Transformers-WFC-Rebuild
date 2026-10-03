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
    // Diagnostics.
    std::vector<std::string> openMovieObjects() const;
    std::string dumpMovie(const std::string& object) const;
    bool hasLoadingMovie() const { return loading_ != nullptr; }
    int loadingFrames() const { return loadingFrames_; }

private:
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
    std::unique_ptr<GfxMovie> loading_;     // the loading movie while a travel is in progress
    std::string loadingUrl_;
    int loadingFrames_ = 0;
    uint32_t prevUi_ = 0;
    std::vector<gfx::Player::RenderItem> items_;
};

} // namespace ui
