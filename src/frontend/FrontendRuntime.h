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
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "frontend/Catalog.h"
#include "frontend/DataStores.h"
#include "frontend/FrontendScene.h"
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
    // A full-screen movie's own audio (stereo WAV folded from its Bink tracks), 2D, outside the mixer categories
    // that CINE_MUTE_FOR_BINK ducks. Returns a handle (< 0: not played).
    virtual int playMovieAudio(const std::string& wavPath) { (void)wavPath; return -1; }
    virtual void stopMovieAudio(int handle) { (void)handle; }
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
    // The UI draws its own pointer (Cursor_GFX); the OS cursor is hidden over the window.
    virtual bool drawsCursor() const { return false; }
    // During a synchronous map load (core::loadYield): only the loading movie animates.
    virtual void advanceLoading(float dt) { (void)dt; }
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
    // clickclip:<clip target path>: window position of a clip's centre in the focused movie (false = not found).
    std::function<bool(const std::string& path, int& x, int& y)> clipHook;
    static std::string autoplayScript(const std::string& tagAndMap);   // "TDM,508"
    // Synthetic device input (ui:<Action>, mouse:x,y, click:x,y, clickclip:<path>) merged into the frame's input,
    // so automation exercises the same logical-action and pointer paths as a player.
    void applySynthetic(platform::InputFrame& in) const;
private:
    struct Synth { uint32_t uiDown = 0; int mouseX = -1, mouseY = -1; bool mouseLeft = false; bool pointer = false; };
    Synth synth_;
    std::vector<Synth> synthQueue_;   // one entry per frame
    void queuePress(uint32_t uiBit);
    void queueClick(int x, int y);
    std::vector<std::string> steps_;
    size_t pos_ = 0;
    float waitTimer_ = 0.0f;
    int keyUp_ = -1;
};

class FrontendRuntime {
public:
    bool init();
    void setPresenter(std::unique_ptr<IMoviePresenter> p) { presenter_ = std::move(p); }
    // The live level under the menus (Rendering draws it; without a renderer the menus sit on black).
    void setSceneRenderer(IFrontendSceneRenderer* r) { sceneRenderer_ = r; }
    const FrontendScene& scene() const { return scene_; }
    // Full-screen movie decoding (platform). Without one, each movie reports Stopped at once.
    void setMoviePlayerFactory(std::function<platform::IMoviePlayer*()> f) { movieFactory_ = std::move(f); }
    // One frontend frame (frontend levels and the loading screen).
    void update(const platform::InputFrame& in, float dt);
    void draw(int w, int h);
    // In-match per-frame hook (pause / end-game movies, script driver, UI events).
    void updateInMatch(const platform::InputFrame& in, float dt);
    // Inside a synchronous map load (core::loadYield): the loading movie and its Bink underlay keep animating; the
    // flow, the script driver and input are not processed.
    void updateLoading(float dt);

    GameFlow& flow() { return flow_; }
    void setAudio(IFrontendAudio* a) { audio_ = a; }
    IFrontendAudio* audio() const { return audio_; }
    // ExternalInterface.call routing for every open movie: Game / Online -> GameFlow, DataStores -> data stores,
    // Sound -> Systems audio, Self / Debug -> movie host. movie = the calling GFx movie object.
    BridgeValue bridge(const std::string& movie, const std::string& fn, const std::vector<std::string>& args);
    const Catalog& catalog() const { return catalog_; }
    // The SKU the shipped movies present (HmUtility.Platform, from $version): "WIN" = the PC SKU's authored branches
    // (default; WFC shipped on PC) or "XBOX360" (WFC_PLATFORM=XBOX360, the console presentation of the dump).
    const std::string& platform() const { return platform_; }
    bool isPC() const { return platform_ == "WIN"; }
    DataStores& dataStores() { return *stores_; }
    std::string titleText() const;
    ScriptDriver& script() { return script_; }
    bool scriptFinished() const { return script_.finished(); }

private:
    void runNativeShims();
    void updateMoviePlayer(float dt, const platform::InputFrame& in);
    bool openVideo(const std::string& name, bool loop);
    std::string prepareMovieAudio(const std::string& name, platform::IMoviePlayer& p);   // cached WAV path or ""
    void stopMovieAudio();
    static bool buildMovieAudio(platform::IMoviePlayer& p, const std::string& wav, std::string& log);   // thread-safe
    std::future<void> audioPrefetch_;   // queued intro movies' audio, decoded while the current one plays

    Catalog catalog_;
    std::string platform_ = "WIN";
    GameFlow flow_;
    ScriptDriver script_;
    std::unique_ptr<IMoviePresenter> presenter_;
    std::vector<std::string> shimmed_;
    uint32_t prevUi_ = 0;
    void updateScene(float dt);
    FrontendScene scene_;
    IFrontendSceneRenderer* sceneRenderer_ = nullptr;
    bool sceneDrawable_ = false;
    std::string sceneLevel_;
    size_t sceneSeen_ = 0;
    float sceneTraceTimer_ = 0.0f;
    IFrontendAudio* audio_ = nullptr;
    LevelKind lastAudioLevel_ = LevelKind::None;
    bool frontEndMusic_ = false;
    std::function<platform::IMoviePlayer*()> movieFactory_;
    std::unique_ptr<platform::IMoviePlayer> video_;   // SeqAct_MoviePlayer movie or the loading underlay
    std::string videoName_;
    std::string underlayFor_, underlay_;   // loading Bink name -> localized file
    bool videoLoops_ = false;
    bool videoFramed_ = false;
    std::string movieAudioWav_;       // the open movie's audio (cache), started with its first frame
    int movieAudioHandle_ = -1;
    uint64_t videoGen_ = 0;
    bool moviePlaying_ = false;
    std::string prefetched_;
    size_t seenFs_ = 0;
    std::unique_ptr<DataStores> stores_;
    void updateAudio(float dt);
};

} // namespace frontend
