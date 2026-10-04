#include "frontend/FrontendRuntime.h"
#include "frontend/FlowTrace.h"
#include "frontend/MovieAudio.h"
#include "core/Config.h"
#include "core/Log.h"
#include "platform/UiBindings.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
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

void ScriptDriver::queuePress(uint32_t uiBit) {
    Synth s = synth_;
    s.uiDown = uiBit;
    synthQueue_.push_back(s);   // held one frame
    s.uiDown = 0;
    synthQueue_.push_back(s);   // released
}

void ScriptDriver::queueClick(int x, int y) {
    Synth s = synth_;
    s.pointer = true; s.mouseX = x; s.mouseY = y; s.mouseLeft = false;
    synthQueue_.push_back(s);   // move (roll over)
    s.mouseLeft = true;
    synthQueue_.push_back(s);   // press
    s.mouseLeft = false;
    synthQueue_.push_back(s);   // release
}

void ScriptDriver::applySynthetic(platform::InputFrame& in) const {
    in.uiDown |= synth_.uiDown;
    if (synth_.pointer) { in.mouseX = synth_.mouseX; in.mouseY = synth_.mouseY; in.mouseLeft = synth_.mouseLeft; }
}

void ScriptDriver::update(GameFlow& flow, float dt) {
    if (!synthQueue_.empty()) { synth_ = synthQueue_.front(); synthQueue_.erase(synthQueue_.begin()); return; }
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
        if (st.rfind("ui:", 0) == 0) {   // a logical UI command (UiBindings action name), pressed for one frame
            for (int k = 0; k < (int)platform::UiKey::Count; ++k)
                if (st.substr(3) == platform::UiBindings::actionName((platform::UiKey)k)) {
                    FlowTrace::emit("script.ui", {{"action", st.substr(3)}});
                    queuePress(1u << k);
                    synth_ = synthQueue_.front(); synthQueue_.erase(synthQueue_.begin());
                    return;
                }
            LOG_WARN("FRONTEND script: unknown UI action '%s'", st.c_str());
            continue;
        }
        if (st.rfind("mouse:", 0) == 0 || st.rfind("click:", 0) == 0) {
            std::vector<std::string> p = split(st.substr(6), ',');
            int x = p.size() > 0 ? std::atoi(p[0].c_str()) : 0, y = p.size() > 1 ? std::atoi(p[1].c_str()) : 0;
            FlowTrace::emit(st[0] == 'm' ? "script.mouse" : "script.click", {{"x", std::to_string(x)}, {"y", std::to_string(y)}});
            if (st[0] == 'm') { synth_.pointer = true; synth_.mouseX = x; synth_.mouseY = y; synth_.mouseLeft = false; }
            else queueClick(x, y);
            return;
        }
        if (st.rfind("clickclip:", 0) == 0) {
            int x = 0, y = 0;
            bool ok = clipHook && clipHook(st.substr(10), x, y);
            FlowTrace::emit("script.clickclip", {{"path", st.substr(10)}, {"found", FlowTrace::boolean(ok)},
                                                 {"x", std::to_string(x)}, {"y", std::to_string(y)}});
            if (ok) queueClick(x, y);
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
    if (const char* p = std::getenv("WFC_PLATFORM")) platform_ = p;
    if (platform_ != "WIN" && platform_ != "XBOX360" && platform_ != "PS3") platform_ = "WIN";
    FlowTrace::emit("platform", {{"sku", platform_}});
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

namespace {
std::string movieAudioCacheDir() { return std::getenv("WFC_CACHE") ? std::getenv("WFC_CACHE") : "wfc_cache"; }
}

bool FrontendRuntime::buildMovieAudio(platform::IMoviePlayer& p, const std::string& wav, std::string& log) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::vector<int16_t>> tracks;
    int rate = 0;
    if (!p.decodeAudio(tracks, rate)) { log = "no audio"; return false; }
    std::vector<int16_t> stereo;
    MovieAudioLayout L = downmixMovieAudio(tracks, 0, stereo);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(wav).parent_path(), ec);
    // Written under a temporary name, then renamed: a reader never sees a partial file.
    std::string tmp = wav + ".part";
    bool ok = !stereo.empty() && writeWav16Stereo(tmp, stereo, rate);
    if (ok) { std::filesystem::rename(tmp, wav, ec); ok = !ec; }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    log = std::to_string(L.tracks) + " tracks, " + L.description + ", " + std::to_string(rate) + " Hz, " +
          std::to_string((int)ms) + " ms";
    return ok;
}

std::string FrontendRuntime::prepareMovieAudio(const std::string& name, platform::IMoviePlayer& p) {
    // Decoded and folded to stereo once, then cached (WFC_CACHE, default ./wfc_cache) so later boots start at once.
    std::string wav = movieAudioCacheDir() + "/movies/" + name + ".wav";
    if (!std::ifstream(wav).good() && audioPrefetch_.valid()) audioPrefetch_.wait();   // being decoded in the background
    if (std::ifstream(wav).good()) {
        FlowTrace::emit("movie.audio", {{"movie", name}, {"cached", "true"}});
    } else {
        std::string log;
        bool ok = buildMovieAudio(p, wav, log);
        FlowTrace::emit("movie.audio", {{"movie", name}, {"decoded", log}, {"cached", FlowTrace::boolean(ok)}});
        if (!ok) return "";
    }
    // The rest of the chain decodes in the background while this movie plays.
    if (!audioPrefetch_.valid() || audioPrefetch_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::vector<std::string> todo;
        for (const std::string& q : flow_.queuedMovies())
            if (!std::ifstream(movieAudioCacheDir() + "/movies/" + q + ".wav").good()) todo.push_back(q);
        if (!todo.empty() && movieFactory_) {
            auto factory = movieFactory_;
            FlowTrace::emit("movie.audioPrefetch", {{"movies", std::to_string(todo.size())}});
            audioPrefetch_ = std::async(std::launch::async, [todo, factory] {
                for (const std::string& q : todo) {
                    std::unique_ptr<platform::IMoviePlayer> mp(factory());
                    std::string log;
                    if (mp && mp->open(Catalog::defaultExtractedRoot() + "/movies/" + q + ".mkv"))
                        buildMovieAudio(*mp, movieAudioCacheDir() + "/movies/" + q + ".wav", log);
                }
            });
        }
    }
    return wav;
}

void FrontendRuntime::stopMovieAudio() {
    if (movieAudioHandle_ >= 0 && audio_) audio_->stopMovieAudio(movieAudioHandle_);
    movieAudioHandle_ = -1;
    movieAudioWav_.clear();
}

bool FrontendRuntime::openVideo(const std::string& name, bool loop) {
    stopMovieAudio();
    video_.reset();
    videoName_ = name;
    videoLoops_ = loop;
    std::string path = Catalog::defaultExtractedRoot() + "/movies/" + name + ".mkv";
    std::unique_ptr<platform::IMoviePlayer> p(movieFactory_ && !std::getenv("WFC_NO_VIDEO") ? movieFactory_() : nullptr);
    if (!p || !p->open(path)) {
        FlowTrace::emit("movie.unavailable", {{"movie", name}, {"file", path}, {"decoder", FlowTrace::boolean(p != nullptr)}});
        return false;
    }
    FlowTrace::emit("movie.open", {{"movie", name}, {"seconds", std::to_string(p->duration())}, {"loop", FlowTrace::boolean(loop)}});
    // SeqAct_MoviePlayer movies carry their audio; the loading underlays have none (AssetTools video_audio probe).
    if (!loop && audio_) movieAudioWav_ = prepareMovieAudio(name, *p);
    video_ = std::move(p);
    videoFramed_ = false;
    ++videoGen_;
    return true;
}

void FrontendRuntime::updateMoviePlayer(float dt, const platform::InputFrame& in) {
    // SeqAct_MoviePlayer (intro chain): the Bink movies, extracted by AssetTools as H.264/FLAC .mkv, decoded by the
    // platform movie player. Stopped fires at the end of the movie. The movie's audio (frontend/MovieAudio: Bink tracks
    // folded to stereo) plays on the Systems device from the first frame and stops with the movie.
    // The loading underlay: [LoadingMovie] InitialStartupFileName / DefaultFileName (Xe-TransGame.ini), looped under
    // LoadScreen_GFX while a loading screen is up [HIGH]. The extracted files carry region / language suffixes; the
    // rebuild picks <name>_NA_INT, then <name>_INT, then <name> [PARTIAL: region of the dump UNKNOWN, see GetRegionCode].
    const std::string& m = flow_.kismetMovie();
    std::string want = m;
    if (want.empty() && flow_.loading().active && !flow_.loading().binkMovie.empty()) {
        const std::string& b = flow_.loading().binkMovie;
        if (b != underlayFor_) {
            underlayFor_ = b;
            underlay_ = b;
            for (const std::string& c : {b + "_NA_INT", b + "_INT"})
                if (std::ifstream(Catalog::defaultExtractedRoot() + "/movies/" + c + ".mkv").good()) { underlay_ = c; break; }
        }
        want = underlay_;
    }
    if (want != videoName_) {
        if (want.empty()) { stopMovieAudio(); video_.reset(); videoName_.clear(); }
        else if (!openVideo(want, m.empty()) && !m.empty()) { flow_.movieStopped(m); videoName_.clear(); }
    }
    if (!video_) return;
    video_->advance(dt);
    const uint8_t* px = nullptr;
    int vw = 0, vh = 0;
    uint64_t serial = 0;
    if (!videoFramed_ && video_->frame(px, vw, vh, serial)) {
        videoFramed_ = true;
        FlowTrace::emit("movie.firstFrame", {{"movie", videoName_}, {"w", std::to_string(vw)}, {"h", std::to_string(vh)},
                                             {"t", std::to_string(video_->position())}});
        if (!movieAudioWav_.empty() && audio_) {   // audio starts with the picture
            movieAudioHandle_ = audio_->playMovieAudio(movieAudioWav_);
            FlowTrace::emit("movie.audioStart", {{"movie", videoName_}, {"handle", std::to_string(movieAudioHandle_)}});
        }
    }
    // Skip with A / Start / B on intro movies [PROVISIONAL: the original skip rule (UE3 bUserCanSkip) is UNKNOWN].
    uint32_t pressed = in.uiDown & ~prevUi_;
    prevUi_ = in.uiDown;
    auto bit = [](platform::UiKey k) { return 1u << (int)k; };
    bool skip = !m.empty() && (pressed & (bit(platform::UiKey::Accept) | bit(platform::UiKey::Start) | bit(platform::UiKey::Back)));
    if (video_->finished() || skip) {
        if (videoLoops_ && !skip) { video_->restart(); return; }
        FlowTrace::emit("movie.finished", {{"movie", videoName_}, {"skipped", FlowTrace::boolean(skip)},
                                           {"position", std::to_string(video_->position())}});
        stopMovieAudio();
        video_.reset();
        std::string done = videoName_;
        videoName_.clear();
        if (!m.empty()) flow_.movieStopped(done);
    }
}

void FrontendRuntime::update(const platform::InputFrame& input, float dt) {
    platform::InputFrame in = input;
    script_.applySynthetic(in);
    flow_.tick(dt);
    runNativeShims();
    updateMoviePlayer(dt, in);
    if (presenter_) presenter_->update(flow_, in, dt);
    script_.update(flow_, dt);
    updateAudio(dt);
}

void FrontendRuntime::updateInMatch(const platform::InputFrame& input, float dt) {
    platform::InputFrame in = input;
    script_.applySynthetic(in);
    flow_.tick(dt);
    // [integration M05] The movie player runs in the match too: the loading underlay (TF_LoadingScreen Bink) is
    // released once the loading screen closes. Without this its last frame (black + "LOADING..." spinner) stayed
    // composited over the 3D world for the whole match (also seen by Experimental on agents/frontend 08ef880).
    updateMoviePlayer(dt, in);
    if (presenter_) presenter_->update(flow_, in, dt);
    script_.update(flow_, dt);
    if (audio_) audio_->tick(dt);   // UI sounds of in-match movies (pause menu); match audio is the World's
}

void FrontendRuntime::updateLoading(float dt) {
    platform::InputFrame none;
    updateMoviePlayer(dt, none);
    if (presenter_) presenter_->advanceLoading(dt);
}

void FrontendRuntime::draw(int w, int h) {
    if (!presenter_) return;
    const uint8_t* px = nullptr;
    int vw = 0, vh = 0;
    uint64_t serial = 0;
    bool over = !flow_.kismetMovie().empty();
    // The serial is unique across movies (the presenter re-uploads on change).
    if (video_ && video_->frame(px, vw, vh, serial)) presenter_->setVideoFrame(px, vw, vh, (videoGen_ << 40) | serial, over);
    else presenter_->setVideoFrame(nullptr, 0, 0, 0, false);
    presenter_->draw(flow_, w, h);
}

std::string FrontendRuntime::titleText() const {
    return "WFC Rebuild | " + flow_.stateSummary();
}

} // namespace frontend
