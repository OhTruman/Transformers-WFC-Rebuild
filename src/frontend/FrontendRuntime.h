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
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "frontend/Catalog.h"
#include "frontend/DataStores.h"
#include "frontend/GameFlow.h"
#include "platform/Input.h"
#include "platform/Movie.h"

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
    virtual void levelEvent(const std::string& trigger) { (void)trigger; }     // "FsCommand:<cmd>", "MovieStopped:<movie>"
    virtual void setMoviePlaying(bool playing) { (void)playing; }             // CINE_MUTE_FOR_BINK while a Bink is up
    virtual void prefetchLevel(const std::string& level) { (void)level; }     // during a loading screen
};

class IMoviePresenter {
public:
    virtual ~IMoviePresenter() = default;
    // True when the presenter runs this movie's ActionScript itself (no native shim needed).
    virtual bool runsMovie(const std::string& movie) const = 0;
    virtual void update(GameFlow& flow, const platform::InputFrame& in, float dt) = 0;
    virtual void draw(const GameFlow& flow, int w, int h) = 0;
    // Automation: a key press on the focused movie (Flash key code).
    virtual void injectKey(int code, bool down) { (void)code; (void)down; }
    // This frame's full-screen movie frame (nullptr = none): over the GFx movies (SeqAct_MoviePlayer) or under them
    // (the loading Bink under LoadScreen_GFX).
    virtual void setVideoFrame(const uint8_t* rgba, int w, int h, uint64_t serial, bool over) {
        (void)rgba; (void)w; (void)h; (void)serial; (void)over;
    }
};

class ScriptDriver {
public:
    bool load(const std::string& script);
    bool active() const { return pos_ < steps_.size(); }
    bool finished() const { return !steps_.empty() && pos_ >= steps_.size(); }
    void update(GameFlow& flow, float dt);
    std::function<void(int code, bool down)> keyHook;          // key:<code>
    std::function<void(const std::string& file)> shotHook;     // shot:<file>
    std::function<void(const std::string& movie)> dumpHook;    // dump:<movie substring>
    static std::string autoplayScript(const std::string& tagAndMap);   // "TDM,508"
private:
    std::vector<std::string> steps_;
    size_t pos_ = 0;
    float waitTimer_ = 0.0f;
    int keyUp_ = -1;
};

class FrontendRuntime {
public:
    bool init();
    void setPresenter(std::unique_ptr<IMoviePresenter> p) { presenter_ = std::move(p); }
    // Full-screen movie decoding (platform). Without one, each movie reports Stopped at once.
    void setMoviePlayerFactory(std::function<platform::IMoviePlayer*()> f) { movieFactory_ = std::move(f); }
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
    ScriptDriver& script() { return script_; }
    bool scriptFinished() const { return script_.finished(); }

private:
    void runNativeShims();
    void updateMoviePlayer(float dt, const platform::InputFrame& in);
    bool openVideo(const std::string& name, bool loop);

    Catalog catalog_;
    GameFlow flow_;
    ScriptDriver script_;
    std::unique_ptr<IMoviePresenter> presenter_;
    std::vector<std::string> shimmed_;
    uint32_t prevUi_ = 0;
    IFrontendAudio* audio_ = nullptr;
    LevelKind lastAudioLevel_ = LevelKind::None;
    bool frontEndMusic_ = false;
    std::function<platform::IMoviePlayer*()> movieFactory_;
    std::unique_ptr<platform::IMoviePlayer> video_;   // SeqAct_MoviePlayer movie or the loading underlay
    std::string videoName_;
    std::string underlayFor_, underlay_;   // loading Bink name -> localized file
    bool videoLoops_ = false;
    bool videoFramed_ = false;
    uint64_t videoGen_ = 0;
    bool moviePlaying_ = false;
    std::string prefetched_;
    size_t seenFs_ = 0;
    std::unique_ptr<DataStores> stores_;
    void updateAudio(float dt);
};

} // namespace frontend
