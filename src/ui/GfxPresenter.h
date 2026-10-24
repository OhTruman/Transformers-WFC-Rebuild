// Clean-room reconstruction — frontend presenter: runs and draws the shipped GFx movies the flow has open
// (GFxAction_OpenMovie / UIController OpenUI / the loading movie), routes their ExternalInterface calls and
// fscommands to the frontend runtime, feeds engine -> AS callbacks (loading text, data-store changes) and gives the
// focused movie the UI keys (the key codes the movies' KeyListener expects).
#pragma once
#include <chrono>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "frontend/FrontendRuntime.h"
#include "frontend/FpsMeter.h"
#include "frontend/PackageCredits.h"
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
    void setHud(bool open, bool visible) override;
    void setScoreboard(bool open) override;
    void hudCall(const std::string& fn, const std::vector<frontend::BridgeValue>& args) override;
    bool hudStageSize(double& w, double& h) const override;
    void setSeatHud(int seat, bool open, bool visible) override;
    void seatHudCall(int seat, const std::string& fn, const std::vector<frontend::BridgeValue>& args) override;
    bool seatHudStageSize(int seat, double& w, double& h) const override;
    void setPadPrompts(bool pad) override;
    void installGlyphLabels();
    void setWorldLabels(const std::vector<frontend::WorldLabel>& labels) override { worldLabels_ = labels; }
    void movieCall(const std::string& movie, const std::string& fn, const std::vector<frontend::BridgeValue>& args) override;
    void advanceLoading(float dt) override {
        if (loading_) { loading_->advance(dt); loadingTime_ += dt; }
        if (cursor_) cursor_->advance(dt);
#if WFC_DLSS_PACKAGE
        syncPackageCredit();
#endif
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
    // TnUIController message box: MessageBoxUI (UI_GFxShared_p.MessagePrompt_GFX_1) with focus while the flow's popup
    // is open; _global.DisplayMessage is invoked whenever the popup changes.
    void syncPopup(frontend::GameFlow& flow);
    // Hud_GFX's native GFx extensions (_global.gfxExtensions): MovieClip.interp -> HmObjectInterpolator.addInterp and
    // setColor -> HmActionScript.setColor, served here with the semantics of the menus' own AS interpolator.
    gfx::avm1::Value hudInterpAdd(GfxMovie& m, gfx::avm1::Args& a);
    void hudInterpUpdate(GfxMovie& m);
    static void setColorHex(gfx::avm1::VM& vm, const gfx::avm1::Value& target, const gfx::avm1::Value& color);
public:
    // Navigation harness: the movies, the focus target and its input owner (_global.currentMenu), modal state and
    // resource counters, as key=value pairs.
    std::vector<std::pair<std::string, std::string>> navReport();
    // HmExternalInterface.Input.RegisterLeftStickCallback(path): the engine calls the AS function at path every frame
    // with the left stick (StickX, StickY; +Y down) - the Create a Character colour picker (HmPickablePalette
    // .updatePaletteCursor). Unregister clears it.
    struct StickCallback { std::string movie, path; };
    std::vector<StickCallback> stickCallbacks_;
    // The texture a movie's external resource is bound to (GFxMovie ExternalTextures), as an extracted PNG path.
    std::string externalTexturePath(const std::string& resource) const { return lib_.externalTexture(resource); }
private:
    uint64_t popupSerial_ = 0;
    void deliverKeys(const platform::InputFrame& in);

    frontend::FrontendRuntime& rt_;
    GfxLibrary lib_;
    bool padPrompts_ = false;
    GfxRendererGL gl_;
    bool glReady_ = false;
    struct Open { std::string object; std::unique_ptr<GfxMovie> movie; };
    std::vector<Open> movies_;              // flow-open movies in open order (last = focus)
    // Every movie the flow has opened (the UI controller's screens: respawn, pause, end game ...). movieCall never opens
    // a second copy of one as an extra - a copy outlives its state and every travel (user playtest 2026-10-23 #2: the
    // respawn timer stuck after respawn and back to the main menu).
    std::set<std::string> flowMovieNames_;
    frontend::LevelKind lastLevel_ = frontend::LevelKind::None;
    static bool isUiControllerMovie(const std::string& movie);
    GfxMovie* openMovieNamed(const std::string& movie);
    void syncPauseChooseCharacter(gfx::Player& p, frontend::GameFlow& flow);
    void scrollSettingsSubMenu(gfx::Player& p, const platform::InputFrame& in);
    void syncIconicPlayerList(gfx::Player& p, frontend::GameFlow& flow);
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
    std::unique_ptr<GfxMovie> hud_;           // TnHUD.HudMovie (Hud_GFX), under the UIController movies
    bool hudVisible_ = false;
    std::unique_ptr<GfxMovie> hud2_;          // split screen: seat 2's Hud_GFX
    bool hud2Visible_ = false;
    bool splitHud_ = false;                   // two seats in a match: each HUD in its view rect (title-safe)
    int hudRect_[2][4] = {};                  // x, y, w, h per seat (window pixels)
    std::unique_ptr<GfxMovie> scoreboard_;   // InGameStats_GFX: one instance per HUD (TnHUD.ScoreboardMovie)
    bool scoreboardShown_ = false;            // started (advanced, drawn, focused); false = Close(KeepLoaded)
    // Movies a movie opens itself (Self.OpenMovieWithPath(path, CaptureInput, CaptureFocus), e.g. Brightness_GFX ->
    // CalibrationImage_GFX): drawn above their opener; with CaptureFocus they take the keys.
    struct Extra { std::string object; std::unique_ptr<GfxMovie> movie; bool focus = false; };
    std::vector<Extra> extras_;
    std::vector<std::string> deferredErase_;    // TnHUD.ScoreboardMovie (InGameStats_GFX), above the HUD, with focus
    bool shapesStale_ = false;                // a movie was destroyed since the last draw
    // Engine -> AS invokes made after the AS call that caused them returns (UnrealScript OwnerMovie.Invoke).
    std::vector<std::string> languageNames() const;   // the Settings Language row's choices (Catalog::languages, own names)
    struct Deferred { std::string movie, fn; gfx::avm1::Args args; };
    std::vector<Deferred> deferred_;
    int viewW_ = 1280, viewH_ = 720;          // last drawn window size (pointer -> stage mapping)
    // waitFrames: a flow movie the flow opened this frame is presented by the next syncMovies; the call waits that long
    struct PendingCall { std::string movie, fn; std::vector<frontend::BridgeValue> args; bool advanced = false; int waitFrames = 0; };
    std::vector<PendingCall> pendingCalls_;   // movieCall: delivered after the movie's first frame
    int frameLimitShown_ = 0;
    // Graphics -> Recommended Settings: re-apply the auto-detect preset, then leave and re-enter the page (queued
    // Escape / Enter, frames from now) so the original rows re-read their values.
    void recommendedSettingsPressed();
    void syncSettingsCommit();   // Commit Changes' confirmation (requestSettingsCommit) -> the original commit + apply + the result
    gfx::Player* commitPlayer_ = nullptr;
    gfx::avm1::Value commitFn_, commitSelf_;   // the original writeGraphicsSettings and the button it ran on
    struct QueuedKey { int frames; int code; bool down; };
    std::vector<QueuedKey> keyQueue_;                 // [PCSettings] FrameLimit for the graphics menu's Frame Rate Limit entry
    float scoreScroll_ = 0.0f;   // in-match scoreboard scroll (stage units), PC EXTENSION for extended matches
    float endScoreScroll_ = 0.0f;   // the same for the end-of-match View Scores list (EndGameStats_GFX)
    bool extendedMatch_ = false;
    std::vector<frontend::WorldLabel> worldLabels_;   // DEV TOOL: QA bot overlay labels (window pixels)
    void syncWorldLabels();   // more than the original 10 participants (kill feed PC EXTENSION)
    void extendedKillFeed(const std::vector<gfx::avm1::Value>& a);
    void checkKillFeed(float dt);   // reveals the entering line after the shift; asserts the 22 px step
    std::string feedManagerPath_;
    float feedRevealIn_ = 0.0f;
    float feedCheckFor_ = 0.0f;     // the feed's 22 px assertion runs only while lines move (after a new line)
    bool labelsShown_ = false;      // QA labels container on the HUD (removed once when the list empties)
    std::vector<std::string> objsScratch_;
    float feedMinGapLogged_ = 1e9f;
    float feedMinGapSeen_ = 1e9f;   // smallest visible-line gap measured (reported with each extended feed line)
    void scrollPlayerList(gfx::Player& p, float& scroll, const platform::InputFrame& in, float dt);
    void syncModeOptionBots(gfx::Player& p, frontend::GameFlow& flow);
    // Online (PC EXTENSION, ONLINE_MP_UI_STRINGS.md 1 / 4): the party lobby's Join Game row (-> TextPrompt -> Online.JoinGame)
    // and the host's join panel in the game lobby (join code, address, copy key, privacy line). Online only.
    void syncJoinGameRow(gfx::Player& p, frontend::GameFlow& flow);
    void syncIconicVersusMenu(gfx::Player& p);
    void syncJoinPanel(gfx::Player& p, frontend::GameFlow& flow, const platform::InputFrame& in);
    float codeCopiedFor_ = 0.0f;
    std::string loadingMessageSent_;
    bool playerListInputGiven_ = false;   // online: _global.GivePlayerListInput while the scoreboard is up (row actions: mute)   // the loading movie's SetLevelText message (re-sent when it changes: the load gate)
    // Settings > Graphics: the movie whose Graphics page got the PC rows (null when none is open); the Volumetric
    // Lighting row follows the Ray Tracing row's current choice live (Realistic: Off / On; otherwise "Realistic only").
    gfx::Player* graphicsPagePlayer_ = nullptr;
    gfx::Player* audioPagePlayer_ = nullptr;   // Settings > Audio with the voice rows (online)
    std::string micMeterShown_;
    void syncAudioPage(gfx::Player& p, frontend::GameFlow& flow);
    void runFirstRunUi(gfx::Player& p, frontend::GameFlow& flow, const std::string& movie);
    // Lobby flow v2 (ONLINE_LOBBY_FLOW_V2.md): Private Match hosts at once; the host's Game Settings row and page.
    void syncHostNowRow(gfx::Player& p, frontend::GameFlow& flow);
    void syncLoadBar();   // the loading screen's progress bar (core::loadProgress)
    void syncGameSettings(gfx::Player& p, frontend::GameFlow& flow);
    void syncVoteRows(gfx::Player& p, frontend::GameFlow& flow);   // the public lobby's map vote rows (ONLINE_LOBBY_FLOW_V2 4.1)
    void attachGameSettingsPage(gfx::Player& p, frontend::GameFlow& flow);
    gfx::Player* gsPlayer_ = nullptr;   // the game lobby whose Game Settings page is open / loading
    int gsState_ = 0, gsFrames_ = 0;    // 0 closed, 1 loading the settings movie, 2 the page is up
    bool gsOpenRequested_ = false;
    std::string gsMode_;                // the mode the page's rows were built for
    gfx::avm1::Object* gsMenu_ = nullptr;   // the settings movie's top menu (the page's _backMenuRef)
    gfx::Player* firstRunAudioPending_ = nullptr;   // Voice Settings: open the Audio page once the settings movie is in
    int firstRunAudioFrames_ = 0;
    gfx::Player* firstRunFooterHidden_ = nullptr;   // the lobby footer hidden under the settings menu (restored on close)
    int volumetricGateShown_ = -1;   // 1 = Off / On offered, 0 = "Realistic only", -1 unknown
    void syncVolumetricRow(gfx::Player& p);
    bool prevMouseLeft_ = false;
    gfx::Player* mouseTarget_ = nullptr;      // movie that last received the pointer
    std::vector<gfx::Player::RenderItem> items_;
    // Corner text overlays (the FPS counter; the DLSS credit): a text field on its own instance of the cursor movie
    // (the font library's $NormalFont), never advanced after its first frame, drawn above everything but the cursor.
    bool openOverlayMovie(std::unique_ptr<GfxMovie>& m, const char* keep);
    std::unique_ptr<GfxMovie> fps_;
    bool fpsFailed_ = false;
    int fpsW_ = 0, fpsH_ = 0;
    std::string fpsText_;
    frontend::FpsMeter fpsMeter_;
    void drawFps(int w, int h);   // Settings > Graphics > FPS Counter (PC EXTENSION)
#if WFC_DLSS_PACKAGE
    // PackageCredits: the NVIDIA DLSS credit line, a text field on its own instance of the cursor movie (the font
    // library's $NormalFont), drawn above the full-screen movie.
    std::unique_ptr<GfxMovie> credit_;
    bool creditShown_ = false, creditFailed_ = false;
    bool creditOnLogo_ = false;                          // shown for a boot logo (bridges the gap to the next one)
    std::chrono::steady_clock::time_point creditGapFrom_{};
    int creditW_ = 0, creditH_ = 0;   // the window size the line was laid out for
    void syncPackageCredit();
#endif
};

} // namespace ui
