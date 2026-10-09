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
#include "frontend/GraphicsAutoDetect.h"
#include "frontend/Characters.h"
#include "frontend/FrontendScene.h"
#include "frontend/Hud.h"
#include "frontend/InputPrompts.h"
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
    // [integration M06] The movie's own sound (its Bink audio tracks, decoded and played by Systems beside the muted
    // game mix): start with the first video frame, stop at its end / skip. False: the movie has no audio (loading Binks).
    virtual bool startMovieAudio(const std::string& moviePath) { (void)moviePath; return false; }
    virtual void stopMovieAudio() {}
};

// DEV TOOL: a text label at a window pixel (QA bot overlay names, projected from the world).
struct WorldLabel { float x = 0, y = 0; std::string text; };

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
    // The in-match HUD movie (TnHUD.HudMovie): open for the match, shown per UI state; never takes key focus.
    virtual void setHud(bool open, bool visible) { (void)open; (void)visible; }
    virtual void hudCall(const std::string& fn, const std::vector<BridgeValue>& args) { (void)fn; (void)args; }
    virtual void setWorldLabels(const std::vector<WorldLabel>& labels) { (void)labels; }   // DEV TOOL overlay text
    // The HUD movie's visible stage in its own units (Hud_GFX Stage.width / height): marker screen coordinates.
    virtual bool hudStageSize(double& w, double& h) const { (void)w; (void)h; return false; }
    // Keyboard prompts: pad glyphs (pad = true) or key text in the movies' button glyph slots.
    virtual void setPadPrompts(bool pad) { (void)pad; }
    // A function of a notification movie (e.g. UI_GFxChallengeNotifies_p.ChallengeNotify_GFX ChallengeUnlocked): the
    // movie is opened without focus if it is not, and the call is made once it has run its first frame.
    virtual void movieCall(const std::string& movie, const std::string& fn, const std::vector<BridgeValue>& args) {
        (void)movie; (void)fn; (void)args;
    }
    // TnHUD.ScoreboardMovie (InGameStats_GFX): open = shown with input focus.
    virtual void setScoreboard(bool open) { (void)open; }
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
    std::function<void(const std::string& button, bool down)> padHook;   // pad:<button> (IWindow::injectPad)
    std::function<void(const std::string& fn, const std::vector<std::string>& args)> hudCallHook;   // hudcall:<fn>,<args>
    std::function<void(const std::string& file)> shotHook;     // shot:<file>
    std::function<void(const std::string& movie)> dumpHook;    // dump:<movie substring>
    std::function<void(const std::string& label)> navCheckHook;   // navcheck:<label> (navigation stress harness)
    std::function<void(int w, int h, bool fullscreen)> displayHook;   // display:<w>,<h>,<0|1> (runtime resolution change)
    // call:<fn>,<args>: the same bridge the movies use (PCSettings.*, Customize.*, Online.*, Game.* ...); flow.call if unset.
    std::function<void(const std::string& fn, const std::vector<std::string>& args)> bridgeHook;
    // clickclip:<clip target path>: window position of a clip's centre in the focused movie (false = not found).
    std::function<bool(const std::string& path, int& x, int& y)> clipHook;
    static std::string autoplayScript(const std::string& tagAndMap);   // "TDM,508"
    // Synthetic device input (ui:<Action>, mouse:x,y, click:x,y, clickclip:<path>) merged into the frame's input,
    // so automation exercises the same logical-action and pointer paths as a player.
    void applySynthetic(platform::InputFrame& in) const;
private:
    struct Synth { uint32_t uiDown = 0; int mouseX = -1, mouseY = -1; bool mouseLeft = false; bool pointer = false; };
    Synth synth_;
    // One-shot text entry for the next frame (type:<text>, vk:<code>).
    mutable std::u32string typeText_;
    mutable std::vector<uint16_t> typeKeys_;
    std::vector<Synth> synthQueue_;   // one entry per frame
    void queuePress(uint32_t uiBit);
    void queueClick(int x, int y);
    std::vector<std::string> steps_;
    size_t pos_ = 0;
    float waitTimer_ = 0.0f;
    int keyUp_ = -1;
    std::string padUp_;
};

class FrontendRuntime {
public:
    bool init();
    void setPresenter(std::unique_ptr<IMoviePresenter> p) { presenter_ = std::move(p); }
    // Hud_GFX post-process chain [CONFIRMED, RE notes/MILESTONE_E_HUD_POSTPROCESS_CHAINS.md]: one slot, -1 none,
    // 0 StaticDischarge (HUD scrambled), 1 LowHealth (downed warning). The renderer presents it (setHudScreenEffect).
    int hudPostProcessChain() const { return hudPostChain_; }
    void setWorldLabels(const std::vector<WorldLabel>& labels) { if (presenter_) presenter_->setWorldLabels(labels); }
    // The PC SKU's display settings (PCSettings.*): the platform window applies them.
    struct DisplayHooks {
        std::function<std::vector<std::pair<int, int>>()> modes;
        std::function<void(int, int, bool)> apply;   // width, height, fullscreen
        std::function<void(bool)> vsync;
    };
    void setDisplayHooks(DisplayHooks h) { display_ = std::move(h); }
    // Graphics auto-detect (PC EXTENSION): the boot-time hardware facts, kept for Graphics -> Recommended Settings.
    void setHardwareFacts(const HardwareFacts& f) { hardware_ = f; hardwareKnown_ = true; }
    // Picks the preset for the known facts and writes it into the profile (saved); why: "first launch", "gpu changed",
    // "recommended" (the menu action). The caller applies the display mode / renderer settings (profile apply).
    GraphicsPreset applyRecommendedGraphics(const char* why);
    // At boot: first launch (no saved PC settings) or a different GPU than the one stored -> applyRecommendedGraphics.
    // Returns true when it changed the settings. Scripted runs (WFC_FRONTEND_SCRIPT) skip it unless WFC_AUTODETECT=1;
    // WFC_AUTODETECT=0 disables it.
    bool autoDetectGraphicsAtBoot();
    // The profile's display settings to their owners now (window mode / VSync like Commit Changes, then the profile
    // apply: frame limit, upscaling / HD textures / anisotropy).
    void applyDisplaySettings();
    // The live level under the menus (Rendering draws it; without a renderer the menus sit on black).
    void setSceneRenderer(IFrontendSceneRenderer* r) { sceneRenderer_ = r; }
    const FrontendScene& scene() const { return scene_; }
    HudController& hud() { return hud_; }
    const CharacterRoster& roster() const { return roster_; }
    // The selected-character contract for a custom slot (GameFlow::SelectedCharacter), as Customize.SelectCharacter
    // records it; automation uses it too, so every path hands Gameplay the same data.
    GameFlow::SelectedCharacter selectionFor(const std::string& name) const;
    // Create a Character: the palette swatch sampler (PNG pixel; platform image decoding) and the preview-pawn request
    // (Customize.UpdatePreviewCharacter / TransformPreviewCharacter*), forwarded to the preview owner.
    std::function<bool(const std::string& png, int x, int y, int& r, int& g, int& b)> sampleImage;
    // GFxMovie ExternalTextures: resource name -> bound texture PNG (the movie's own bitmaps are placeholders).
    std::function<std::string(const std::string& resource)> externalTexturePath;
    struct PreviewRequest {
        std::string call;                                    // UpdatePreviewCharacter / TransformPreviewCharacter / ...ToRobot
        std::vector<std::string> chassis, primary, secondary;   // per preview controller (Autobot, Decepticon), as the movie sent
        // Per slot, ready for the renderer's preview draw (Rendering: setFrontendSceneDraw + setDrawOwner(1 / 2) +
        // setCharacterColors + drawDynamicMesh(ueActorMatrix(pos, rot))): the authored spawn point of the slot's
        // SeqAct_TnPawnFactory (UI_CharacterCustomization_m Preview_Characters: Autobot -> PathNode_16191, Decepticon ->
        // PathNode_7537; UE units / degrees) and the colours as linear RGB (FLinearColor(FColor): sRGB -> linear).
        struct Slot { std::string chassis, robotGltf, vehicleGltf; std::vector<std::string> robotAnimSets;
                      float posUE[3] = {0, 0, 0}; float rotUEdeg[3] = {0, 0, 0};
                      float primaryLinear[3] = {0, 0, 0}, secondaryLinear[3] = {0, 0, 0}; };
        std::vector<Slot> slots;
    };
    std::function<void(const PreviewRequest&)> previewHook;
    static constexpr const char* kCharactersFile = "wfc_characters.ini";
    // Full-screen movie decoding (platform). Without one, each movie reports Stopped at once.
    void setMoviePlayerFactory(std::function<platform::IMoviePlayer*()> f) { movieFactory_ = std::move(f); }
    // One frontend frame (frontend levels and the loading screen).
    void update(const platform::InputFrame& in, float dt);
    void draw(int w, int h);
    // In-match per-frame hook (pause / end-game movies, script driver, UI events).
    void updateInMatch(const platform::InputFrame& in, float dt);
    bool scoreboardOpen() const { return scoreboard_; }
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
    const InputPrompts& prompts() const { return prompts_; }
    bool padPrompts() const { return device_.pad(); }
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
    void releaseVideo();   // the travel underlay is parked for the next loading screen; other movies are destroyed
    void stopMovieAudio();

    Catalog catalog_;
    std::string platform_ = "WIN";
    GameFlow flow_;
    ScriptDriver script_;
    std::unique_ptr<IMoviePresenter> presenter_;
    std::vector<std::string> shimmed_;
    uint32_t prevUi_ = 0;
    void updateScene(float dt);
    FrontendScene scene_;
    CharacterRoster roster_;
    std::string previewChassis_[2];
public:
    // One scene draw while the loading screen still covers it (the renderer's first draw of a new scene compiles its
    // programs and uploads its textures lazily - a visible stall right after the load otherwise) [PC ADAPTATION].
    void prewarmSceneOnce() { prewarmScene_ = true; }
    // Opens the travel loading underlay ([LoadingMovie] DefaultFileName) and parks it, so the first travel after boot
    // does not open it on the frame its loading screen appears; called under a loading screen. Once.
    void prewarmLoadingUnderlay();
    // Multiplayer progression awards for the local player (Gameplay's XP events / game stats; RE MP_PROGRESSION s2-s7):
    // applied to the profile's progression and shown as the original presents them (Hud_GFX PointEvent / NotifyLevelUp /
    // the TnPlayerLevelUpMessage broadcast, ChallengeNotify_GFX ChallengeUnlocked). Saved when the match ends.
    struct XpEvent { int transactionId = 0; long xp = 0; std::string announcement, description, extra; };
    void progressionXp(const XpEvent& e);
    void progressionStat(int statId, long amount, int updateType);
    bool sceneDrawable() const { return sceneDrawable_; }
private:
    bool prewarmScene_ = false;
    std::map<std::string, bool> emblemOn_;   // scene.emblem trace state (actor.param -> above the midpoint)   // the chassis each preview controller shows (last UpdatePreviewCharacter)
    BridgeValue customize(const std::string& fn, const std::vector<std::string>& args);
    DisplayHooks display_;
    HardwareFacts hardware_;
    bool hardwareKnown_ = false;
    GraphicsPresetTable presets_;
    bool presetsLoaded_ = false;
    BridgeValue account(const std::string& fn, const std::vector<std::string>& args);
    BridgeValue commitCharacter(const std::vector<std::string>& args);
    BridgeValue pcSettings(const std::string& fn, const std::vector<std::string>& args);
    HudController hud_;
    InputPrompts prompts_;          // ^COMMAND -> key text (TnInputCommandToBindingMapper)
    InputDevice device_;            // the last-used device (prompts)
    int lastMouseX_ = -1, lastMouseY_ = -1;
    void updateInputDevice(const platform::InputFrame& in, float dt);
    bool scoreboard_ = false;
    uint32_t prevMatchUi_ = 0;
    IFrontendSceneRenderer* sceneRenderer_ = nullptr;
    bool sceneDrawable_ = false;
    std::string sceneLevel_;
    size_t sceneSeen_ = 0;
    float sceneTraceTimer_ = 0.0f;
    IFrontendAudio* audio_ = nullptr;
    LevelKind lastAudioLevel_ = LevelKind::None;
    LevelKind progressionLevel_ = LevelKind::None;   // the match begin / end edges of the progression
    bool canGainXp_ = false;
    void updateProgression();
    void presentLevelUps(const std::vector<progression::LevelUp>& ups);
    // PC ADAPTATION chassis XP unlocks (data/frontend/chassis_xp_unlocks.json: chassis id -> level of its specialty).
    std::map<std::string, int> chassisXpUnlocks_;
    int chassisUnlockLevel(const std::string& id) const;   // 0 = not in the table
    bool chassisUnlocked(const ChassisInfo& ci) const;
    void updateChassisUnlockTexts();
    bool frontEndMusic_ = false;
    std::function<platform::IMoviePlayer*()> movieFactory_;
    std::unique_ptr<platform::IMoviePlayer> video_;   // SeqAct_MoviePlayer movie or the loading underlay
    std::string videoName_;
    // The last loading underlay's decoder, kept between loading screens: reopening it (Media Foundation reader +
    // H.264 decoder) cost 31-34 ms on the frame each travel began [PC ADAPTATION].
    std::unique_ptr<platform::IMoviePlayer> parkedUnderlay_;
    std::string parkedName_;
    bool underlayPrewarmed_ = false;
    std::string underlayFor_, underlay_;   // loading Bink name -> localized file
    bool videoLoops_ = false;
    bool videoFramed_ = false;
    std::string videoPath_;           // the open movie file (Systems movie audio)
    bool movieAudioWanted_ = false;   // a SeqAct_MoviePlayer movie (the loading underlays author no sound)
    bool movieAudioPlaying_ = false;  // Systems is playing its sound
    bool movieInputHold_ = false;     // full-screen movie owns input (and until its skip key is released)
    // Boot: the initial startup Bink ([LoadingMovie] InitialStartupFileName) stays up full screen until the logo chain
    // starts, as the original's startup movie plays until the front-end map is loaded; the title scene loads behind it.
    std::string bootUnderlay_;
    bool bootDone_ = false;
    bool fullScreenMovie_ = false;    // a movie covers the presentation (startup, intro chain, Game.PlayMovie)
    bool sceneLoading_ = false;
public:
    // Frontend scene loads (title / lobby families) run inside this wrapper: the application presents frames through
    // core::loadYield while the load blocks (the startup movie keeps animating).
    std::function<void(const std::function<void()>&)> sceneLoadWrapper;
    bool fullScreenMovie() const { return fullScreenMovie_; }
private:
    uint64_t videoGen_ = 0;
    bool moviePlaying_ = false;
    int hudPostChain_ = -1;
    std::vector<std::string> unlockedThisGame_;   // TnPlayerController.SkillsUnlockedThisGame ("Specialty.UniqueId")
    std::string prefetched_;
    size_t seenFs_ = 0;
    std::unique_ptr<DataStores> stores_;
    void updateAudio(float dt);
};

} // namespace frontend
