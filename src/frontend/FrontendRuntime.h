// Clean-room reconstruction — frontend runtime host: catalog + game flow + presentation drivers.
//
// Drivers of the flow (all talk to GameFlow through the original bridge/fscommand surface only):
//   * Movie presenter  - draws the open GFx movies and runs their ActionScript (see src/ui/gfx).
//   * Movie player     - SeqAct_MoviePlayer Bink movies (Logo_*, FMV_intro, TF_LoadingScreen).
//   * Script driver    - automation for validation: WFC_FRONTEND_SCRIPT / WFC_FRONTEND_AUTOPLAY issue the same
//                        bridge calls the original movies make (docs/FRONTEND.md).
//   * Native shims     - stand in for ActionScript that the presenter does not execute yet, each logged with its
//                        provenance (e.g. MovieLoader's HasWatchedIntroMovie branch, HIGH per RE 1.2).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "frontend/Catalog.h"
#include "frontend/DataStores.h"
#include "frontend/GameFlow.h"
#include "platform/Input.h"

namespace frontend {

// Systems audio seam (game::FrontendAudio on agents/systems): the frontend requests, Systems plays.
class IFrontendAudio {
public:
    virtual ~IFrontendAudio() = default;
    virtual int playUiSound(const std::string& name) = 0;                     // GFx Sound.PlaySound
    virtual bool stopUiSound(const std::string& name, float fade) = 0;         // GFx Sound.StopSound
    virtual void uiLevelStarted(const std::string& uiLevel) = 0;              // the level's SeqAct_PlayMusic track
    virtual void levelChange() = 0;                                           // the level's music player goes away
    virtual void tick(float dt) = 0;
};

class IMoviePresenter {
public:
    virtual ~IMoviePresenter() = default;
    // True when the presenter runs this movie's ActionScript itself (no native shim needed).
    virtual bool runsMovie(const std::string& movie) const = 0;
    virtual void update(GameFlow& flow, const platform::InputFrame& in, float dt) = 0;
    virtual void draw(const GameFlow& flow, int w, int h) = 0;
};

class ScriptDriver {
public:
    bool load(const std::string& script);
    bool active() const { return pos_ < steps_.size(); }
    bool finished() const { return !steps_.empty() && pos_ >= steps_.size(); }
    void update(GameFlow& flow, float dt);
    static std::string autoplayScript(const std::string& tagAndMap);   // "TDM,508"
private:
    std::vector<std::string> steps_;
    size_t pos_ = 0;
    float waitTimer_ = 0.0f;
};

class FrontendRuntime {
public:
    bool init();
    void setPresenter(std::unique_ptr<IMoviePresenter> p) { presenter_ = std::move(p); }
    // One frontend frame (frontend levels and the loading screen).
    void update(const platform::InputFrame& in, float dt);
    void draw(int w, int h);
    // In-match per-frame hook (pause / end-game movies, script driver, UI events).
    void updateInMatch(const platform::InputFrame& in, float dt);

    GameFlow& flow() { return flow_; }
    void setAudio(IFrontendAudio* a) { audio_ = a; }
    IFrontendAudio* audio() const { return audio_; }
    // ExternalInterface.call routing for every open movie: Game / Online -> GameFlow, DataStores -> data stores,
    // Sound -> Systems audio, Self / Debug -> movie host. movie = the calling GFx movie object.
    BridgeValue bridge(const std::string& movie, const std::string& fn, const std::vector<std::string>& args);
    const Catalog& catalog() const { return catalog_; }
    DataStores& dataStores() { return *stores_; }
    std::string titleText() const;
    bool scriptFinished() const { return script_.finished(); }

private:
    void runNativeShims();
    void updateMoviePlayer(float dt);

    Catalog catalog_;
    GameFlow flow_;
    ScriptDriver script_;
    std::unique_ptr<IMoviePresenter> presenter_;
    std::vector<std::string> shimmed_;
    std::string playingMovie_;
    float movieTime_ = 0.0f;
    IFrontendAudio* audio_ = nullptr;
    LevelKind lastAudioLevel_ = LevelKind::None;
    bool frontEndMusic_ = false;
    std::unique_ptr<DataStores> stores_;
    void updateAudio(float dt);
};

} // namespace frontend
