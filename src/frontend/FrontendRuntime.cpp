#include "frontend/FrontendRuntime.h"
#include "frontend/FlowTrace.h"
#include "core/Config.h"
#include "core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace frontend {

namespace {
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::stringstream ss(s);
    while (std::getline(ss, cur, sep)) {
        size_t a = cur.find_first_not_of(" \t\r\n"), b = cur.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
    }
    return out;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Script driver: "step;step;..." where a step is
//   wait:level=<FrontEnd|PartyLobby|GameLobby|Match>   wait:ui=<UIState>   wait:frontend   wait:loading=0
//   wait:movie=<substring of an open movie>             wait:t=<seconds>
//   call:<Interface.Method>[,arg...]                    fscommand:<movie>,<cmd>
//   showmenu   uievent:<code>   snapshot:<why>   quit

std::string ScriptDriver::autoplayScript(const std::string& tagAndMap) {
    std::vector<std::string> p = split(tagAndMap, ',');
    std::string tag = p.size() > 0 ? p[0] : "TDM";
    std::string map = p.size() > 1 ? p[1] : "508";
    // FrontEnd_GFX multiplayerBtn_mc -> Online.OpenPartyLobby("GTS_TeamGame"); PartyLobby_GFX custom match ->
    // Online.EditGameMode / Online.PlayPrivateGame; GameLobby_GFX -> Online.SetSelectedMapID(id), host start ->
    // Online.BeginLobbyExitCountdown (TnGameLobbyGame.HostRequestsGameStart). Bridge names CONFIRMED (RE 1.5, 2.1,
    // 2.4, binding decomp); the EditGameMode / PlayPrivateGame argument string is PARTIAL (mode SettingsConfigName).
    std::string gts = tag == "DM" ? "GTS_FreeForAllGame" : "GTS_TeamGame";
    return "wait:frontend;wait:ui=FrontEnd;snapshot:mainmenu;call:Online.OpenPartyLobby," + gts +
           ";wait:level=PartyLobby;wait:ui=InLobby;snapshot:partylobby;call:Online.EditGameMode," + tag +
           ";call:Online.PlayPrivateGame," + tag + ";wait:level=GameLobby;wait:ui=InLobby;call:Online.SetSelectedMapID," + map +
           ";snapshot:gamelobby;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;snapshot:ingame";
}

bool ScriptDriver::load(const std::string& script) {
    steps_ = split(script, ';');
    pos_ = 0;
    LOG_INFO("FRONTEND script: %zu steps", steps_.size());
    return !steps_.empty();
}

void ScriptDriver::update(GameFlow& flow, float dt) {
    if (keyUp_ >= 0) { if (keyHook) keyHook(keyUp_, false); keyUp_ = -1; return; }
    while (pos_ < steps_.size()) {
        const std::string& st = steps_[pos_];
        if (st.rfind("wait:", 0) == 0) {
            std::string c = st.substr(5);
            bool ok = false;
            if (c.rfind("level=", 0) == 0) ok = !flow.loading().active && levelKindName(flow.level()) == c.substr(6);
            else if (c.rfind("ui=", 0) == 0) ok = uiStateName(flow.ui().state()) == c.substr(3);
            else if (c == "frontend") ok = flow.frontEndStarted();
            else if (c == "loading=0") ok = !flow.loading().active;
            else if (c == "loading=1") ok = flow.loading().active;
            else if (c.rfind("movie=", 0) == 0) {
                for (const std::string& m : flow.openMovies()) if (m.find(c.substr(6)) != std::string::npos) ok = true;
            } else if (c.rfind("t=", 0) == 0) {
                waitTimer_ += dt;
                ok = waitTimer_ >= (float)std::atof(c.c_str() + 2);
                if (ok) waitTimer_ = 0.0f;
                else return;
            }
            if (!ok) return;
            FlowTrace::emit("script.wait", {{"cond", c}});
            ++pos_;
            continue;
        }
        ++pos_;
        if (st.rfind("call:", 0) == 0) {
            std::vector<std::string> p = split(st.substr(5), ',');
            std::string fn = p.empty() ? std::string() : p[0];
            p.erase(p.begin());
            flow.call(fn, p);
            return;   // one action per frame (the movies issue calls from input / frame events)
        }
        if (st.rfind("fscommand:", 0) == 0) {
            std::vector<std::string> p = split(st.substr(10), ',');
            if (p.size() >= 2) flow.fsCommand(p[0], p[1], p.size() > 2 ? p[2] : "");
            return;
        }
        if (st == "showmenu") { flow.showMenu(); return; }
        if (st.rfind("key:", 0) == 0) {
            int code = std::atoi(st.c_str() + 4);
            FlowTrace::emit("script.key", {{"code", std::to_string(code)}});
            if (keyHook) keyHook(code, true);
            keyUp_ = code;
            return;
        }
        if (st.rfind("dump:", 0) == 0) { if (dumpHook) dumpHook(st.substr(5)); continue; }
        if (st.rfind("shot:", 0) == 0) { if (shotHook) shotHook(st.substr(5)); return; }
        if (st.rfind("uievent:", 0) == 0) { flow.onUIEvent(std::atoi(st.c_str() + 8)); return; }
        if (st.rfind("snapshot:", 0) == 0) { flow.traceSnapshot(st.c_str() + 9); continue; }
        if (st == "quit") { flow.call("Game.ExitGame"); return; }
        LOG_WARN("FRONTEND script: unknown step '%s'", st.c_str());
    }
}

// ---------------------------------------------------------------------------------------------------------------

bool FrontendRuntime::init() {
    std::string vs = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    if (!catalog_.load(Catalog::defaultManifestRoot(), Catalog::defaultExtractedRoot(), vs + "/Maps")) return false;
    GameFlow::Options o;
    o.skipIntroMovies = std::getenv("WFC_SKIPINTRO") != nullptr;
    if (const char* s = std::getenv("WFC_FLOWSEED")) o.seed = (unsigned)std::strtoul(s, nullptr, 10);
    if (const char* s = std::getenv("WFC_FRONTEND_SCRIPT")) script_.load(s);
    else if (const char* a = std::getenv("WFC_FRONTEND_AUTOPLAY")) script_.load(ScriptDriver::autoplayScript(a));
    stores_ = std::make_unique<DataStores>(flow_, catalog_);
    return flow_.init(catalog_, o);
}

BridgeValue FrontendRuntime::bridge(const std::string& movie, const std::string& fn, const std::vector<std::string>& args) {
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    if (fn.rfind("DataStores.", 0) == 0) return stores_->call(fn.substr(11), args, movie);
    if (fn == "Sound.PlaySound") {
        FlowTrace::emit("ui.sound", {{"name", arg(0)}, {"movie", movie}, {"audio", audio_ ? "systems" : "none"}});
        if (audio_) return BridgeValue(audio_->playUiSound(arg(0)) >= 0);
        return {};
    }
    if (fn == "Sound.StopSound") {
        if (audio_) audio_->stopUiSound(arg(0), args.size() > 1 ? (float)std::atof(arg(1).c_str()) : 0.5f);
        return {};
    }
    // TnGameActionScriptBinding: language / region drive the localized logo (FrontEnd_GFX logo clip). The rebuild
    // runs the INT data; the region code of the dumped build is UNKNOWN - "NA" selects the TM logo variant [PARTIAL].
    if (fn == "Game.GetLanguageCode") return BridgeValue("INT");
    if (fn == "Game.GetRegionCode") return BridgeValue("NA");
    if (fn == "Game.SetHasWatchedIntroMovie") { FlowTrace::emit("profile", {{"SetHasWatchedIntroMovie", "movie"}}); return {}; }
    if (fn == "Debug.ShouldDisplayBuildInfo") return BridgeValue(false);
    if (fn == "Debug.GetBuildInfo") return BridgeValue(std::string());
    if (fn == "Customize.IsPrimeModeAvailable") return BridgeValue(false);
    if (fn == "Console.SaveProfileSettings" || fn == "Console.CheckCanSaveProfileSettings") return BridgeValue(true);
    return flow_.call(fn, args);
}

void FrontendRuntime::updateAudio(float dt) {
    // Music follows the UI levels' Kismet (Systems FrontendAudio plays it): UI_FrontEnd_m starts its track at
    // [FRONTEND START]; the lobby maps at level start; any travel replaces the level's music player.
    LevelKind lv = flow_.loading().active ? LevelKind::None : flow_.level();
    if (lv != lastAudioLevel_) {
        if (lastAudioLevel_ != LevelKind::None || flow_.loading().active) {
            if (audio_) audio_->levelChange();
            FlowTrace::emit("audio.levelChange", {{"from", levelKindName(lastAudioLevel_)}});
        }
        frontEndMusic_ = false;
        if (lv == LevelKind::PartyLobby || lv == LevelKind::GameLobby) {
            if (audio_) audio_->uiLevelStarted(flow_.levelMap());
            FlowTrace::emit("audio.uiLevel", {{"level", flow_.levelMap()}});
        }
        lastAudioLevel_ = lv;
    }
    if (lv == LevelKind::FrontEnd && flow_.frontEndStarted() && !frontEndMusic_) {
        frontEndMusic_ = true;
        if (audio_) audio_->uiLevelStarted("UI_FrontEnd_m");
        FlowTrace::emit("audio.uiLevel", {{"level", "UI_FrontEnd_m"}});
    }
    // Frontend-owned Kismet triggers for the level audio (fscommands other than the [FRONTEND START] one, which
    // uiLevelStarted fires; movie Stopped outputs).
    const auto& ev = flow_.kismetTriggers();
    for (; seenFs_ < ev.size(); ++seenFs_) {
        if (audio_) audio_->levelEvent(ev[seenFs_]);
        FlowTrace::emit("audio.levelEvent", {{"trigger", ev[seenFs_]}});
    }
    // A Bink movie is "up" for the intro chain and while a loading movie shows (TF_LoadingScreen under the GFx).
    bool movie = !flow_.kismetMovie().empty() || flow_.loading().active;
    if (movie != moviePlaying_) {
        moviePlaying_ = movie;
        if (audio_) audio_->setMoviePlaying(movie);
        FlowTrace::emit("audio.moviePlaying", {{"playing", FlowTrace::boolean(movie)}});
    }
    // Prefetch the destination level's streamed audio while its loading screen is up.
    if (flow_.loading().active) {
        // Match maps are known by their runtime directory (MP_IAC_Streets_Base_m -> MP_IAC_Streets); UI levels by name.
        std::string dest = Url::parse(flow_.loading().url).map();
        if (const MapInfo* mi = catalog_.mapByFilename(dest)) dest = mi->runtimeDir;
        if (!dest.empty() && dest != prefetched_) {
            prefetched_ = dest;
            if (audio_) audio_->prefetchLevel(dest);
            FlowTrace::emit("audio.prefetch", {{"level", dest}});
        }
    } else prefetched_.clear();
    if (audio_) audio_->tick(dt);
}

void FrontendRuntime::runNativeShims() {
    // MovieLoader_GFX: its AS2 calls Game.HasWatchedIntroMovie() and sends fscommand "enterMovieSequence" or
    // "enterFrontEnd" (RE 1.2: pool hasWatchedMovie | checkForMovieWatch | Game.HasWatchedIntroMovie; the branch
    // body is HIGH, not disassembled). Shimmed only while the presenter does not run that movie.
    const std::string loader = "UI_GFxFrontEnd_p.MovieLoader_GFX_1";
    const auto& open = flow_.openMovies();
    bool loaderOpen = std::find(open.begin(), open.end(), loader) != open.end();
    if (loaderOpen && !(presenter_ && presenter_->runsMovie(loader)) &&
        std::find(shimmed_.begin(), shimmed_.end(), loader) == shimmed_.end()) {
        shimmed_.push_back(loader);
        FlowTrace::emit("shim", {{"movie", loader}, {"what", "HasWatchedIntroMovie branch"}, {"provenance", "HIGH (RE 1.2)"}});
        bool watched = flow_.call("Game.HasWatchedIntroMovie").truthy();
        flow_.fsCommand(loader, watched ? "enterFrontEnd" : "enterMovieSequence");
    }
    if (!loaderOpen) shimmed_.erase(std::remove(shimmed_.begin(), shimmed_.end(), loader), shimmed_.end());
}

void FrontendRuntime::updateMoviePlayer(float dt) {
    // SeqAct_MoviePlayer: the Bink movies are extracted as H.264/FLAC .mkv (ExtractedAssets/movies). No decoder is
    // integrated yet [PARTIAL]: each movie reports Stopped immediately, logged so validation sees the chain order.
    const std::string& m = flow_.kismetMovie();
    if (m.empty()) { playingMovie_.clear(); return; }
    if (m != playingMovie_) {
        playingMovie_ = m;
        movieTime_ = 0.0f;
        FlowTrace::emit("movie.unavailable", {{"movie", m}, {"file", "movies/" + m + ".mkv"}, {"why", "no video decoder integrated"}});
    }
    movieTime_ += dt;
    flow_.movieStopped(m);
}

void FrontendRuntime::update(const platform::InputFrame& in, float dt) {
    flow_.tick(dt);
    runNativeShims();
    updateMoviePlayer(dt);
    if (presenter_) presenter_->update(flow_, in, dt);
    script_.update(flow_, dt);
    updateAudio(dt);
}

void FrontendRuntime::updateInMatch(const platform::InputFrame& in, float dt) {
    flow_.tick(dt);
    if (presenter_) presenter_->update(flow_, in, dt);
    script_.update(flow_, dt);
    if (audio_) audio_->tick(dt);   // UI sounds of in-match movies (pause menu); match audio is the World's
}

void FrontendRuntime::draw(int w, int h) {
    if (presenter_) presenter_->draw(flow_, w, h);
}

std::string FrontendRuntime::titleText() const {
    return "WFC Rebuild | " + flow_.stateSummary();
}

} // namespace frontend
