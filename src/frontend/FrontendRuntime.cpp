#include "frontend/FrontendRuntime.h"
#include "core/FrameProfile.h"
#include "frontend/FlowTrace.h"
#include "core/Config.h"
#include "core/Log.h"
#include "platform/UiBindings.h"

#include <algorithm>
#include <cmath>
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
    in.text += typeText_;
    in.keyPresses.insert(in.keyPresses.end(), typeKeys_.begin(), typeKeys_.end());
    typeText_.clear();
    typeKeys_.clear();
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
            if (bridgeHook) bridgeHook(fn, p);
            else flow.call(fn, p);
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
        if (st.rfind("type:", 0) == 0) {   // typed text (%XX escapes, e.g. %20 for a space)
            std::string t = st.substr(5);
            for (size_t i = 0; i < t.size(); ++i) {
                if (t[i] == '%' && i + 2 < t.size()) { typeText_.push_back((char32_t)std::strtol(t.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
                else typeText_.push_back((char32_t)(unsigned char)t[i]);
            }
            FlowTrace::emit("script.type", {{"text", t}});
            return;
        }
        if (st.rfind("vk:", 0) == 0) {    // a raw key press (Win32 virtual-key code, e.g. 8 Backspace, 46 Delete)
            typeKeys_.push_back((uint16_t)std::atoi(st.c_str() + 3));
            FlowTrace::emit("script.vk", {{"code", st.substr(3)}});
            return;
        }
        if (st.rfind("dump:", 0) == 0) { if (dumpHook) dumpHook(st.substr(5)); continue; }
        if (st.rfind("navcheck:", 0) == 0) { if (navCheckHook) navCheckHook(st.substr(9)); continue; }
        if (st.rfind("display:", 0) == 0) {
            std::vector<std::string> p = split(st.substr(8), ',');
            if (p.size() >= 2 && displayHook) displayHook(std::atoi(p[0].c_str()), std::atoi(p[1].c_str()), p.size() > 2 && p[2] == "1");
            return;
        }
        if (st.rfind("shot:", 0) == 0) { if (shotHook) shotHook(st.substr(5)); return; }
        if (st.rfind("uievent:", 0) == 0) { flow.onUIEvent(std::atoi(st.c_str() + 8)); return; }
        if (st.rfind("snapshot:", 0) == 0) { flow.traceSnapshot(st.c_str() + 9); continue; }
        if (st == "quit") { flow.exitNow(); return; }
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
    script_.bridgeHook = [this](const std::string& fn, const std::vector<std::string>& a) { bridge("", fn, a); };
    if (const char* s = std::getenv("WFC_FRONTEND_SCRIPT")) script_.load(s);
    else if (const char* a = std::getenv("WFC_FRONTEND_AUTOPLAY")) script_.load(ScriptDriver::autoplayScript(a));
    stores_ = std::make_unique<DataStores>(flow_, catalog_);
    roster_.load(Catalog::defaultManifestRoot() + "/mp_content/roster_package.json");
    roster_.loadAuthored(std::string(WFC_SOURCE_DIR) + "/data/frontend/character_presets.json");
    // A fresh profile's characters are created with random palette colours (FillCharacterSlots ->
    // ResetCharacterFromName) and written at once (MarkAsDirty -> the profile save); later runs load them.
    if (!std::filesystem::exists(kCharactersFile)) {
        roster_.randomizeAllColors();
        roster_.save(kCharactersFile);
        FlowTrace::emit("customize.freshProfile", {{"file", kCharactersFile}, {"colors", "random palettes"}});
    }
    roster_.loadSaved(kCharactersFile);   // the player's edits (Create a Character)
    // SeqVar_TnCustomizationCameraId: the preview pawn's chassis provider's CustomizationCameraId (-1 without a pawn).
    scene_.cameraIdForSlot = [this](int slot) {
        if (slot < 0 || slot > 1 || previewChassis_[slot].empty()) return -1;
        for (const Catalog::Provider& p : catalog_.providers("Chassis"))
            if (p.name == previewChassis_[slot] || p.get("UniqueId") == previewChassis_[slot]) {
                std::string id = p.get("CustomizationCameraId");
                return id.empty() ? 0 : std::atoi(id.c_str());
            }
        return -1;
    };
    if (!scene_.load(std::string(WFC_SOURCE_DIR) + "/data/frontend/scenes.json")) LOG_WARN("frontend: data/frontend/scenes.json missing (no scene cameras)");
    return flow_.init(catalog_, o);
}

BridgeValue FrontendRuntime::account(const std::string& fn, const std::vector<std::string>& args) {
    LocalProfile& p = flow_.profile();
    auto arg0 = args.empty() ? std::string() : args[0];
    auto find = [&](const std::string& n) { return std::find(p.accounts.begin(), p.accounts.end(), n); };
    if (fn == "Account.GetAccountNames") {
        // One comma-separated string: HmInterfaceAccount.GetAccountNames splits it (r1.split(',')) [CONFIRMED AS2].
        std::string joined;
        for (const std::string& n : p.accounts) joined += (joined.empty() ? "" : ",") + n;
        return BridgeValue(joined);
    }
    if (fn == "Account.GetLoggedInAccount") return BridgeValue(p.loggedInAccount);
    if (fn == "Account.CreateAccount") {
        // TextPrompt limits the name to 15 characters; an empty / blank or duplicate name is not created.
        bool blank = arg0.find_first_not_of(' ') == std::string::npos;
        bool ok = !blank && arg0.find(',') == std::string::npos && find(arg0) == p.accounts.end();   // ',' separates the list
        if (ok) {
            p.accounts.push_back(arg0);
            // Offline there is no separate login service: a new account is signed in when none is, so the name the
            // player typed is the player name at once (lobbies, scoreboard, kill feed) [PC ADAPTATION].
            if (p.loggedInAccount.empty()) p.loggedInAccount = arg0;
            p.save();
        }
        // OnCreateAccountComplete: the CreateAccountTitle message box with the result [CONFIRMED script]; offline the
        // only failures are local (blank / duplicate / comma).
        flow_.showMessage("$UIText.MessagePrompts.CreateAccountTitle", ok ? "$UIText.MessagePrompts.CreateAccountSucceededMessage"
                                                                         : "$UIText.MessagePrompts.CreateAccountInvalidUsernameMessage");
        FlowTrace::emit("account.create", {{"name", arg0}, {"created", FlowTrace::boolean(ok)}, {"provenance", "PC ADAPTATION (local)"}});
        return {};
    }
    if (fn == "Account.DeleteAccount") {
        auto it = find(arg0);
        if (it != p.accounts.end()) p.accounts.erase(it);
        if (p.loggedInAccount == arg0) p.loggedInAccount.clear();
        p.save();
        FlowTrace::emit("account.delete", {{"name", arg0}});
        return {};
    }
    if (fn == "Account.Login") {
        if (find(arg0) != p.accounts.end()) { p.loggedInAccount = arg0; p.save(); }
        FlowTrace::emit("account.login", {{"name", arg0}, {"playerName", p.playerName()}});
        return {};
    }
    if (fn == "Account.Logout") {
        p.loggedInAccount.clear();
        p.save();
        FlowTrace::emit("account.logout", {{"playerName", p.playerName()}});
        return {};
    }
    if (fn == "Account.SetDefaultAccount") return {};   // the signed-in account is remembered (LocalProfile)
    FlowTrace::emit("bridge.unhandled", {{"fn", fn}});
    return {};
}

namespace {
// Development builds (CMake WFC_DEV_TOOLS): debug-only bridge functions (synthetic progression awards).
bool devTools() {
#if defined(WFC_DEV_TOOLS) && WFC_DEV_TOOLS
    return true;
#else
    return false;
#endif
}
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
    if (fn.rfind("PCSettings.", 0) == 0) return pcSettings(fn, args);
    // TnAccountActionScriptBinding (PC SKU Accounts menu). Original: Demonware online accounts bound to the product
    // key [SERVICE DEPENDENT]. Offline: local account names in the profile [PC ADAPTATION]; the signed-in account is
    // the player name (LocalProfile::playerName).
    if (fn.rfind("Account.", 0) == 0) return account(fn, args);
    // Stats (online stats archive): challenge progress and leaderboards. Offline there is no archive: progress 0,
    // level 0 (a fresh profile), leaderboard reads report nothing [SERVICE DEPENDENT; values as the original offline].
    // Challenge progress from the local profile's progression (the original: the stats archive) [PC ADAPTATION storage].
    if (fn == "Stats.GetChallengeValue" || fn == "Stats.GetChallengeLevel") {
        const ProgressionState& ps = flow_.profile().progression;
        const int id = (int)std::strtol(arg(0).c_str(), nullptr, 0);
        if (fn == "Stats.GetChallengeLevel") { auto t = ps.tiers.find(id); return BridgeValue(t == ps.tiers.end() ? 0 : t->second); }
        auto v = ps.stats.find(progression::challengeStat(catalog_, id));
        return BridgeValue(v == ps.stats.end() ? 0.0 : (double)v->second);
    }
    if (fn.rfind("Stats.", 0) == 0) { FlowTrace::emit("service.unavailable", {{"fn", fn}, {"service", "online stats"}}); return {}; }
    if (fn == "Customize.IsPrimeModeAvailable") {   // all four specialties at the XP cap, Prime not taken [CONFIRMED]
        const ProgressionState& ps = flow_.profile().progression;
        return BridgeValue(progression::levelMaxed(ps) && !ps.prime);
    }
    if (fn == "Customize.UnlockPrimeMode") {
        // UnlockPrimeMode: Prime on (enables the Prime challenges). The original also clears its PrimeModeStatsToClear
        // challenge stats; that list is not recovered [UNKNOWN], so no stat is cleared here.
        ProgressionState& ps = flow_.profile().progression;
        if (progression::levelMaxed(ps) && !ps.prime) { ps.prime = true; flow_.profile().save(); }
        FlowTrace::emit("progression.prime", {{"active", FlowTrace::boolean(ps.prime)}});
        return {};
    }
    // Development builds: synthetic awards for testing the presentation before Gameplay's award feed exists.
    if (devTools() && fn == "Debug.ProgressionXp") {
        XpEvent e; e.transactionId = std::atoi(arg(0).c_str()); e.xp = std::atol(arg(1).c_str());
        e.announcement = arg(2); e.description = arg(3); e.extra = arg(4);
        progressionXp(e);
        return {};
    }
    if (devTools() && fn == "Debug.ProgressionStat") {
        progressionStat((int)std::strtol(arg(0).c_str(), nullptr, 0), std::atol(arg(1).c_str()), std::atoi(arg(2).c_str()));
        return {};
    }
    // TnXpManager (via TnCharacterScriptBinding): XP per specialty (the original: the online stats archive; here the
    // local profile's progression), levels from Default__TnXpManager.LevelTable [CONFIRMED authored].
    {
        const ProgressionState& ps = flow_.profile().progression;
        const int sp = progression::specialtyIndex(arg(0));
        if (fn == "Customize.GetXpEarnedForSpecialty") return BridgeValue(sp < 0 ? 0.0 : (double)ps.xp[(size_t)sp]);
        if (fn == "Customize.GetXpEarnedForSpecialtyLastMatch") return BridgeValue(sp < 0 ? 0.0 : (double)ps.lastMatchXp[(size_t)sp]);
        if (fn == "Customize.GetLevelForSpecialty") return BridgeValue(sp < 0 ? 0 : progression::levelForXp(ps.xp[(size_t)sp]));
        if (fn == "Customize.GetXpNeededForLevel") return BridgeValue((double)progression::xpNeededForLevel(std::atoi(arg(0).c_str())));
    }
    if (fn.rfind("Customize.", 0) == 0) return customize(fn, args);   // TnCharacterScriptBinding
    if (fn == "Console.CheckCanSaveProfileSettings") return BridgeValue(true);
    if (fn == "Console.GetKeyDescription") {
        // TnConsoleActionScriptBinding -> TnPlayerInput.GetKeyDescription(Key, Form) [CONFIRMED script]: the key goes
        // through TnGameViewportClient.MapInputKeyForController (profile SwitchRightThumbstickAndB / alternate scheme),
        // then KeyDescriptions[Key].Description, replaced by the form's own text when it has one. The Controls pages
        // are read-only references: the shipped menus have no key rebinding.
        std::string key = arg(0), form = arg(1);
        const LocalProfile& pr = flow_.profile();
        if (pr.getBool("SwitchRightThumbstickAndB")) {
            if (key == "XboxTypeS_B") key = "XboxTypeS_RightThumbstick";
            else if (key == "XboxTypeS_RightThumbstick") key = "XboxTypeS_B";
        }
        if (pr.getBool("UseAlternateControlScheme")) {
            if (key == "XboxTypeS_Y") key = "XboxTypeS_LeftThumbstick";
            else if (key == "XboxTypeS_LeftThumbstick") key = "NoKey";
            else if (key == "XboxTypeS_DPad_Up") key = "XboxTypeS_Y";
        }
        const Catalog::KeyDescription* kd = catalog_.keyDescription(key);
        if (!kd) return BridgeValue(std::string());
        std::string d = kd->description;
        auto over = [&](const std::string& o) { if (!o.empty()) d = o; };
        if (form == "Robot") over(kd->robot);
        else if (form == "Car") over(kd->car);
        if (form == "Truck") over(kd->truck);
        else if (form == "Tank") over(kd->tank);
        else if (form == "Plane") over(kd->plane);
        return BridgeValue(d);
    }
    return flow_.call(fn, args);
}

namespace {
std::string hexColor(int r, int g, int b) {   // ColorToHexString: "0x" + RRGGBB [digit case PROVISIONAL]
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%02X%02X%02X", r & 0xFF, g & 0xFF, b & 0xFF);
    return buf;
}
std::string floatText(float v) {   // UnrealScript float -> string ("0.00")
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) { if (c == ',') { out.push_back(cur); cur.clear(); } else cur += c; }
    if (!s.empty()) out.push_back(cur);
    return out;
}
std::string join2(const std::vector<std::string>& v) { std::string o; for (const auto& x : v) o += (o.empty() ? "" : ",") + x; return o; }
}

GameFlow::SelectedCharacter FrontendRuntime::selectionFor(const std::string& name) const {
    // TnPlayerController.SelectCharacter(name, 0): the committed custom character, resolved once for Gameplay.
    GameFlow::SelectedCharacter s;
    s.name = name;
    s.type = 0;
    const CharacterPreset* c = roster_.find(name);
    if (!c) return s;   // unknown slot: selection recorded, no chassis (bodyAvailable false)
    s.friendlyName = c->friendlyName;
    s.specialty = c->specialty;
    s.weapons = c->weapons; s.vehicleWeapons = c->vehicleWeapons; s.melee = c->melee; s.abilities = c->abilities; s.skills = c->skills;
    auto color = [](const CharacterColor& in) {
        GameFlow::CharacterColorSel o;
        o.r = in.r; o.g = in.g; o.b = in.b; o.a = in.a; o.palette = in.palette; o.x = in.x; o.y = in.y;
        return o;
    };
    for (int f = 0; f < 2; ++f) {
        s.chassis[f] = c->chassis[f];
        s.primary[f] = color(c->primary[f]);
        s.secondary[f] = color(c->secondary[f]);
        if (const ChassisInfo* ci = roster_.chassis(c->chassis[f])) {
            s.robotGltf[f] = ci->robotGltf;
            s.vehicleGltf[f] = ci->vehicleGltf;
            std::error_code ec;
            s.bodyAvailable[f] = !ci->robotGltf.empty() &&
                                 std::filesystem::exists(Catalog::defaultExtractedRoot() + "/" + ci->robotGltf, ec);
        }
    }
    return s;
}

BridgeValue FrontendRuntime::customize(const std::string& fn, const std::vector<std::string>& args) {
    // TnCharacterScriptBinding ("Customize.*") over the local characters (CharacterRoster: roster package presets).
    // Custom mode (iconic mode = GameTeamStatus 2 / 4 or OnlyAllowIconicCharacters is not wired yet: PARTIAL).
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    auto join = [](const std::vector<std::string>& v) { std::string o; for (const auto& s : v) o += (o.empty() ? "" : ",") + s; return o; };
    const CharacterPreset* c = roster_.find(arg(0));
    if (fn == "Customize.CheckCustomCharacterDataLoaded") return {};   // loaded (no TnLoadCustomCharactersStatusMessageBox)
    if (fn == "Customize.GetCustomCharacters" || fn == "Customize.GetPlayableCharacters") {
        std::vector<std::string> names;
        for (const CharacterPreset& p : roster_.customCharacters()) names.push_back(p.name);
        return BridgeValue(join(names));
    }
    if (fn == "Customize.GetCurrentCharacter") return BridgeValue(flow_.selectedCharacter().name);
    // Default__TnCharacterCustomizationData.UnlockCharacterSlotLevels [5, 10] in the binding's "5,10," format.
    if (fn == "Customize.GetCharacterSlotUnlockLevels") return BridgeValue(std::string("5,10,"));
    // No XP progression offline (TnXpManager returns 0): nothing newly unlocked.
    if (fn == "Customize.GetNewlyUnlockedSkills" || fn == "Customize.GetNewlyUnlockedAbilities") return BridgeValue(std::string());
    if (fn == "Customize.IsChassisUnlocked") { const ChassisInfo* ci = roster_.chassis(arg(0)); return BridgeValue(ci && !ci->lockedChassis); }
    if (fn == "Customize.SelectCharacter") {
        flow_.selectCharacter(selectionFor(arg(0)));
        return {};
    }
    // ---- TnCharacterScriptBinding script bodies [CONFIRMED decompile] ----
    // GetPixelColor(TextureId, X, Y): the palette texture's pixel, packed "0xRRGGBB;TextureId;X;Y" (PackColorData).
    // Palettes 0-4 autobotPalette_N, 5-9 decepticonPalette_N: OwnerMovie.ExternalTextures[TextureId], bound by the lobby
    // movies (PartyLobby_GFX / GameLobby_GFX ExternalTextures -> UI_CustomChar_p.A_green ... D_teal, 128x128). The
    // UI_GFxCustomize_p bitmaps of those names are "EXTERNAL TEXTURE" placeholders.
    auto pixel = [&](int tex, int x, int y) {
        int r = 0, g = 0, b = 0;
        std::string res = (tex < 5 ? "autobotPalette_" : "decepticonPalette_") + std::to_string(tex);
        std::string png = externalTexturePath ? externalTexturePath(res) : std::string();
        if (!sampleImage || !sampleImage(png, x, y, r, g, b))
            FlowTrace::emit("customize.palette", {{"texture", std::to_string(tex)}, {"sampled", "false"}});
        return hexColor(r, g, b) + ";" + std::to_string(tex) + ";" + std::to_string(x) + ";" + std::to_string(y);
    };
    if (fn == "Customize.GetPixelColor") return BridgeValue(pixel(std::atoi(arg(0).c_str()), std::atoi(arg(1).c_str()), std::atoi(arg(2).c_str())));
    if (fn == "Customize.ClearCharacter") {   // PRI.ClearCharacter
        flow_.clearSelectedCharacter();
        return {};
    }
    if (fn == "Customize.WriteCustomizationFile") {
        bool ok = roster_.save(kCharactersFile);
        FlowTrace::emit("customize.write", {{"file", kCharactersFile}, {"ok", FlowTrace::boolean(ok)}, {"provenance", "PC ADAPTATION (file)"}});
        return {};
    }
    if (fn == "Customize.ResetCharacter") {
        roster_.reset(arg(0));
        FlowTrace::emit("customize.reset", {{"character", arg(0)}});
        return {};
    }
    if (fn == "Customize.CommitCharacter") return commitCharacter(args);
    if (fn == "Customize.UpdatePreviewCharacter" || fn == "Customize.TransformPreviewCharacter" ||
        fn == "Customize.TransformPreviewCharacterToRobot") {
        // The preview pawn (UI_CharacterCustomization_m PreviewGuy controllers) is drawn by Rendering from the
        // chassis / colours / form Gameplay's character data resolves; the frontend only forwards the request.
        PreviewRequest pr;
        pr.call = fn.substr(10);
        if (fn == "Customize.UpdatePreviewCharacter") {
            // CustomTransformers_GFX HmExternalInterface.Customize.UpdatePreviewCharacter(CharacterChassis, CharacterFaction,
            // CharacterPrimary, CharacterSecondary) forwards all four; the binding body never reads the faction [CONFIRMED].
            pr.chassis = splitCsv(arg(0)); pr.primary = splitCsv(arg(2)); pr.secondary = splitCsv(arg(3));
            // Authored spawn points (authored.db: SeqAct_TnPawnFactory_8107 "Autobot" / _10632 "Decepticon"; rotation
            // yaw 16-bit units -> degrees). The pawn's ground snap (OnPreviewPawnTick FindGround) is Gameplay's.
            static const float kPos[2][3] = {{-285.862f, 5587.910f, 179.0f}, {-286.862f, 4701.910f, 179.0f}};
            static const float kYaw[2] = {-11264.0f * 360.0f / 65536.0f, 8192.0f * 360.0f / 65536.0f};
            auto linear = [](const std::string& packed, float out[3]) {
                std::string hex = packed.rfind("0x", 0) == 0 ? packed.substr(2, 6) : std::string();
                unsigned v = hex.empty() ? 0u : (unsigned)std::strtoul(hex.c_str(), nullptr, 16);
                const unsigned c[3] = {(v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF};
                for (int i = 0; i < 3; ++i) {   // FLinearColor(FColor): the sRGB decode
                    float f = c[i] / 255.0f;
                    out[i] = f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f);
                }
            };
            for (size_t i = 0; i < pr.chassis.size() && i < 2; ++i) previewChassis_[i] = pr.chassis[i];
            for (size_t i = 0; i < pr.chassis.size() && i < 2; ++i) {
                PreviewRequest::Slot sl;
                sl.chassis = pr.chassis[i];
                if (const ChassisInfo* ci = roster_.chassis(sl.chassis)) {   // roster package
                    sl.robotGltf = ci->robotGltf;
                    sl.vehicleGltf = ci->vehicleGltf;
                    sl.robotAnimSets = ci->robotAnimSets;
                }
                for (int k = 0; k < 3; ++k) sl.posUE[k] = kPos[i][k];
                sl.rotUEdeg[1] = kYaw[i];
                if (i < pr.primary.size()) linear(pr.primary[i], sl.primaryLinear);
                if (i < pr.secondary.size()) linear(pr.secondary[i], sl.secondaryLinear);
                pr.slots.push_back(sl);
            }
        }
        if (previewHook) previewHook(pr);
        FlowTrace::emit("customize.preview", {{"call", pr.call}, {"chassis", arg(0)}, {"primary", arg(2)}, {"secondary", arg(3)},
                                              {"owner", previewHook ? "renderer" : "none"}});
        return {};
    }
    if (fn == "Customize.VerifyCharacterOptionsData" || fn == "Customize.MarkSkillAsOld") return {};
    if (!c) return {};
    const int faction = std::atoi(arg(1).c_str()) == 1 ? 1 : 0;   // FactionFilter
    if (fn == "Customize.GetCharacterSpecialty") return BridgeValue(c->specialty);
    if (fn == "Customize.GetCharacterFriendlyName") return BridgeValue(c->friendlyName.empty() ? c->name : c->friendlyName);
    if (fn == "Customize.GetCharacterChassis") return BridgeValue(c->chassis[faction]);   // ChassisTypes[FactionFilter]
    if (fn == "Customize.GetCharacterWeaponTypes") {
        // WeaponTypes with MeleeWeapons[0] inserted at index 2.
        std::vector<std::string> w = c->weapons;
        if (!c->melee.empty()) w.insert(w.begin() + (long)std::min<size_t>(2, w.size()), c->melee.front());
        return BridgeValue(join(w));
    }
    if (fn == "Customize.GetCharacterAbilities") return BridgeValue(join(c->abilities));
    if (fn == "Customize.GetCharacterVehicleWeapon") return BridgeValue(join(c->vehicleWeapons));
    if (fn == "Customize.GetCharacterSkills") return BridgeValue(join(c->skills));
    if (fn == "Customize.GetCharacterPrimaryColor" || fn == "Customize.GetCharacterSecondaryColor") {
        // A black colour means "the palette swatch": GetPixelColor(palette, coords) (packed); else "0x" + hex.
        const CharacterColor& col = fn == "Customize.GetCharacterPrimaryColor" ? c->primary[faction] : c->secondary[faction];
        if (col.isBlack()) return BridgeValue(pixel(col.palette, (int)col.x, (int)col.y));
        return BridgeValue(hexColor(col.r, col.g, col.b));
    }
    if (fn == "Customize.GetPrimaryPalette" || fn == "Customize.GetSecondaryPalette") {
        const CharacterColor& col = fn == "Customize.GetPrimaryPalette" ? c->primary[faction] : c->secondary[faction];
        return BridgeValue(std::to_string(col.palette) + "," + floatText(col.x) + "," + floatText(col.y));
    }
    if (fn == "Customize.GetCharacterDecal") return BridgeValue(std::string());
    FlowTrace::emit("bridge.unhandled", {{"fn", fn}});
    return {};
}

BridgeValue FrontendRuntime::commitCharacter(const std::vector<std::string>& args) {
    // TnCharacterScriptBinding.CommitCharacter(CharacterName, FriendlyName, ChassisTypes, Specialty, PrimaryColorsHex,
    // SecondaryColorsHex, Skills, WeaponTypes_, Abilities, VehicleWeapons) [CONFIRMED decompile]: empty fields keep
    // the stored value; "Empty" clears skills / abilities; only the first two weapon types are replaced (the melee
    // slot is separate); colours are "0xRRGGBB;palette;x;y" per faction.
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    std::string name = arg(0);
    CharacterPreset* c = roster_.findMutable(name);
    if (!c) { FlowTrace::emit("customize.commit", {{"character", name}, {"found", "false"}}); return {}; }
    if (!arg(1).empty()) c->friendlyName = arg(1);
    if (!arg(2).empty()) { auto ch = splitCsv(arg(2)); for (size_t i = 0; i < ch.size() && i < 2; ++i) c->chassis[i] = ch[i]; }
    if (!arg(3).empty()) c->specialty = arg(3);
    auto colors = [&](const std::string& csv, CharacterColor* out) {
        if (csv.empty()) return;
        auto items = splitCsv(csv);
        for (size_t i = 0; i < items.size() && i < 2; ++i) {
            std::vector<std::string> p;
            std::string cur;
            for (char ch : items[i]) { if (ch == ';') { p.push_back(cur); cur.clear(); } else cur += ch; }
            p.push_back(cur);
            CharacterColor& col = out[i];
            std::string hex = p[0].rfind("0x", 0) == 0 ? p[0].substr(2) : std::string();   // HexStringToColor: "0x" else black
            while (!hex.empty() && hex.size() < 6) hex = "0" + hex;
            unsigned v = hex.empty() ? 0u : (unsigned)std::strtoul(hex.c_str(), nullptr, 16);
            col.r = (int)((v >> 16) & 0xFF); col.g = (int)((v >> 8) & 0xFF); col.b = (int)(v & 0xFF); col.a = 255;
            if (p.size() == 4) { col.palette = std::atoi(p[1].c_str()); col.x = (float)std::atof(p[2].c_str()); col.y = (float)std::atof(p[3].c_str()); }
        }
    };
    colors(arg(4), c->primary);
    colors(arg(5), c->secondary);
    if (!arg(6).empty()) { if (arg(6) == "Empty") c->skills.clear(); else c->skills = splitCsv(arg(6)); }
    if (!arg(7).empty()) {
        auto w = splitCsv(arg(7));
        if (c->weapons.size() < 2) c->weapons.resize(2);
        // An unset slot arrives as "undefined" inside the movie's array; the stored weapon is kept.
        for (size_t i = 0; i < w.size() && i < 2; ++i) if (!w[i].empty() && w[i] != "undefined") c->weapons[i] = w[i];
    }
    if (!arg(8).empty()) { if (arg(8) == "Empty") c->abilities.clear(); else c->abilities = splitCsv(arg(8)); }
    if (!arg(9).empty()) {
        auto v = splitCsv(arg(9));
        if (c->vehicleWeapons.size() < 2) c->vehicleWeapons.resize(std::max<size_t>(c->vehicleWeapons.size(), v.size()));
        for (size_t i = 0; i < v.size() && i < 2; ++i) if (!v[i].empty() && v[i] != "undefined") c->vehicleWeapons[i] = v[i];
    }
    FlowTrace::emit("customize.commit", {{"character", name}, {"friendlyName", c->friendlyName}, {"chassis", c->chassis[0] + "," + c->chassis[1]},
                                         {"weapons", join2(c->weapons)}, {"abilities", join2(c->abilities)}});
    return {};
}

BridgeValue FrontendRuntime::pcSettings(const std::string& fn, const std::vector<std::string>& args) {
    // HmInterfacePCSettings (SettingsMenu_GFX WIN branch) [call names CONFIRMED AS2; native bodies not in the dump:
    // semantics HIGH from the movie's use]. Graphics -> Commit Changes calls SetResolution(w, h, fullscreen),
    // SetTextureQualityLevel(0..2), SetVSyncState(bool); the menu reads the current values back.
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    auto truthy = [](const std::string& s) { return s == "true" || s == "True" || s == "1"; };
    LocalProfile& p = flow_.profile();
    FlowTrace::emit("bridge", {{"fn", fn}, {"args", arg(0) + (args.size() > 1 ? "," + arg(1) : "") + (args.size() > 2 ? "," + arg(2) : "")}});
    if (fn == "PCSettings.GetResolutions") {
        std::string out;
        std::vector<std::pair<int, int>> modes = display_.modes ? display_.modes() : std::vector<std::pair<int, int>>{};
        if (modes.empty()) modes.push_back({p.display.width, p.display.height});
        for (const auto& m : modes) out += (out.empty() ? "" : ",") + std::to_string(m.first) + "x" + std::to_string(m.second);
        return BridgeValue(out);
    }
    if (fn == "PCSettings.GetResolution") return BridgeValue(std::to_string(p.display.width) + "x" + std::to_string(p.display.height));
    if (fn == "PCSettings.IsFullScreen") return BridgeValue(p.display.fullscreen);
    if (fn == "PCSettings.GetTextureQualityLevel") return BridgeValue(p.display.textureQuality);
    if (fn == "PCSettings.GetVSyncState") return BridgeValue(p.display.vsync);
    if (fn == "PCSettings.SetResolution") {
        int w = std::atoi(arg(0).c_str()), h = std::atoi(arg(1).c_str());
        if (w > 0 && h > 0) { p.display.width = w; p.display.height = h; }
        p.display.fullscreen = truthy(arg(2));
        if (display_.apply) display_.apply(p.display.width, p.display.height, p.display.fullscreen);
        p.save();
        return {};
    }
    if (fn == "PCSettings.SetTextureQualityLevel") {
        p.display.textureQuality = std::atoi(arg(0).c_str());
        p.save();
        FlowTrace::emit("settings.owner", {{"setting", "TextureQuality"}, {"owner", "none (Rendering has no texture quality control yet)"}});
        return {};
    }
    if (fn == "PCSettings.SetVSyncState") {
        p.display.vsync = truthy(arg(0));
        if (display_.vsync) display_.vsync(p.display.vsync);
        p.save();
        return {};
    }
    FlowTrace::emit("bridge.unhandled", {{"fn", fn}});
    return {};
}

void FrontendRuntime::updateProgression() {
    // Match begin / end edges: the match's XP starts at 0 (results screen); the profile is saved once at its end
    // (ClientWriteLeaderboardStats -> SaveProfileSettings) [CONFIRMED order, RE MP_PROGRESSION s2].
    const LevelKind lv = flow_.loading().active ? LevelKind::None : flow_.level();
    if (lv == progressionLevel_) return;
    ProgressionState& ps = flow_.profile().progression;
    if (lv == LevelKind::Match) {
        ps.lastMatchXp = {};
        // ORIGINAL: CanGainXp = !IsPrivateGame() (private matches award no XP / challenge progress). PC ADAPTATION:
        // offline private matches (the only kind here, with bots) earn progression; WFC_ORIGINAL_XP_RULE=1 keeps the
        // original rule.
        canGainXp_ = !std::getenv("WFC_ORIGINAL_XP_RULE") || flow_.lobby().playlistId >= 0;
        FlowTrace::emit("progression.match", {{"begin", "1"}, {"canGainXp", FlowTrace::boolean(canGainXp_)},
                                              {"rule", std::getenv("WFC_ORIGINAL_XP_RULE") ? "original" : "PC ADAPTATION offline XP"}});
    } else if (progressionLevel_ == LevelKind::Match) {
        flow_.profile().save();
        std::string lm;
        for (int i = 0; i < 4; ++i) lm += (i ? "," : "") + std::to_string(ps.lastMatchXp[(size_t)i]);
        FlowTrace::emit("progression.match", {{"end", "1"}, {"lastMatchXp", lm}, {"level", std::to_string(progression::playerLevel(ps))},
                                              {"saved", "1"}});
    }
    progressionLevel_ = lv;
}

void FrontendRuntime::presentLevelUps(const std::vector<progression::LevelUp>& ups) {
    for (const progression::LevelUp& u : ups) {
        const std::string sp = progression::specialtyName(u.specialty);
        if (presenter_) presenter_->hudCall("_global.NotifyLevelUp", {BridgeValue(u.level), BridgeValue(sp)});
        // TnPlayerLevelUpMessage "`p is now a level `l `s" (specialty level) [CONFIRMED text, RE s4].
        std::string msg = catalog_.localize("TransGame", "TnPlayerLevelUpMessage", "LevelUpMessage");
        if (msg.empty()) msg = "`p is now a level `l `s";
        auto sub = [&](const std::string& k, const std::string& v) { for (size_t at; (at = msg.find(k)) != std::string::npos;) msg.replace(at, k.size(), v); };
        sub("`p", flow_.profile().playerName()); sub("`l", std::to_string(u.level)); sub("`s", sp);
        if (presenter_) presenter_->hudCall("_global.GameMessage", {BridgeValue(msg)});
        FlowTrace::emit("progression.levelUp", {{"specialty", sp}, {"level", std::to_string(u.level)}, {"message", msg}});
    }
}

void FrontendRuntime::progressionXp(const XpEvent& e) {
    // TnHudDataObserverXpTransactions -> _global.PointEvent(transactionId, xp, announcement, description, extraData); the
    // popup shows even when no XP is awarded (ShowXpEvents) [CONFIRMED]. Only the played specialty earns.
    ProgressionState& ps = flow_.profile().progression;
    const int sp = progression::specialtyIndex(flow_.selectedCharacter().specialty);
    const long award = canGainXp_ ? e.xp : 0;
    if (presenter_)
        presenter_->hudCall("_global.PointEvent", {BridgeValue(e.transactionId), BridgeValue((double)award), BridgeValue(e.announcement),
                                                   BridgeValue(e.description), BridgeValue(e.extra)});
    std::vector<progression::LevelUp> ups;
    if (award > 0) { progression::LevelUp u = progression::addXp(ps, sp, award); if (u.specialty >= 0) ups.push_back(u); }
    FlowTrace::emit("progression.xp", {{"transaction", std::to_string(e.transactionId)}, {"xp", std::to_string(award)},
                                       {"announcement", e.announcement}, {"specialty", progression::specialtyName(sp)},
                                       {"total", sp >= 0 ? std::to_string(ps.xp[(size_t)sp]) : "-"}});
    presentLevelUps(ups);
}

void FrontendRuntime::progressionStat(int statId, long amount, int updateType) {
    if (!canGainXp_) return;   // the original drops challenge stats in private matches (PRI.ShouldUpdateState)
    ProgressionState& ps = flow_.profile().progression;
    const int sp = progression::specialtyIndex(flow_.selectedCharacter().specialty);
    std::vector<progression::LevelUp> ups;
    for (const progression::ChallengeUnlock& u : progression::reportStat(ps, catalog_, statId, amount, updateType, sp, ups)) {
        if (presenter_)
            presenter_->movieCall("UI_GFxChallengeNotifies_p.ChallengeNotify_GFX", "_global.ChallengeUnlocked",
                                  {BridgeValue(u.name), BridgeValue(u.description), BridgeValue(u.tier), BridgeValue((double)u.goal),
                                   BridgeValue((double)u.xp)});
        FlowTrace::emit("progression.challenge", {{"id", std::to_string(u.challengeId)}, {"name", u.name}, {"tier", std::to_string(u.tier)},
                                                  {"goal", std::to_string(u.goal)}, {"xp", std::to_string(u.xp)},
                                                  {"prime", FlowTrace::boolean(u.prime)}});
    }
    presentLevelUps(ups);
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
            if (audio_) { core::prof::Scope prof("audio.levelStart"); audio_->uiLevelStarted(flow_.levelMap()); }
            FlowTrace::emit("audio.uiLevel", {{"level", flow_.levelMap()}});
        }
        lastAudioLevel_ = lv;
    }
    if (lv == LevelKind::FrontEnd && flow_.frontEndStarted() && !frontEndMusic_) {
        frontEndMusic_ = true;
        if (audio_) { core::prof::Scope prof("audio.levelStart"); audio_->uiLevelStarted("UI_FrontEnd_m"); }
        FlowTrace::emit("audio.uiLevel", {{"level", "UI_FrontEnd_m"}});
    }
    // Frontend-owned Kismet triggers for the level audio (fscommands other than the [FRONTEND START] one, which
    // uiLevelStarted fires; movie Stopped outputs).
    const auto& ev = flow_.kismetTriggers();
    for (; seenFs_ < ev.size(); ++seenFs_) {
        if (ev[seenFs_] == "FsCommand:enterFrontEnd") continue;   // the [FRONTEND START] trigger: uiLevelStarted above
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
            core::prof::Scope prof("audio.prefetch");
            if (audio_) audio_->prefetchLevel(dest);
            FlowTrace::emit("audio.prefetch", {{"level", dest}});
        }
    } else prefetched_.clear();
    if (audio_) { core::prof::Scope prof("audio.tick"); audio_->tick(dt); }
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

void FrontendRuntime::stopMovieAudio() {
    // [integration M06] Systems owns the movie sound (Systems M07 MovieAudioPlayer); the frontend only says when.
    if (movieAudioPlaying_ && audio_) audio_->stopMovieAudio();
    movieAudioPlaying_ = false;
}

namespace {
// The extracted movies carry region / language suffixes: <name>_NA_INT, then <name>_INT, then <name>.
std::string localizedMovie(const std::string& b) {
    for (const std::string& c : {b + "_NA_INT", b + "_INT"})
        if (std::ifstream(Catalog::defaultExtractedRoot() + "/movies/" + c + ".mkv").good()) return c;
    return b;
}
}

void FrontendRuntime::prewarmLoadingUnderlay() {
    if (underlayPrewarmed_) return;
    underlayPrewarmed_ = true;
    const std::string b = catalog_.loadingMovieDefault();
    if (b.empty() || !movieFactory_ || std::getenv("WFC_NO_VIDEO")) return;
    const std::string name = localizedMovie(b);
    if ((video_ && videoName_ == name) || (parkedUnderlay_ && parkedName_ == name)) return;
    std::unique_ptr<platform::IMoviePlayer> p(movieFactory_());
    if (!p || !p->open(Catalog::defaultExtractedRoot() + "/movies/" + name + ".mkv")) return;
    p->advance(0.0);   // the decoder's first frame (its warm-up) also happens here
    parkedUnderlay_ = std::move(p);
    parkedName_ = name;
    FlowTrace::emit("movie.prewarm", {{"movie", name}});
}

void FrontendRuntime::releaseVideo() {
    // Only the travel underlay is parked (the boot startup movie never plays again and must not replace it).
    if (video_ && videoLoops_ && videoName_ == localizedMovie(catalog_.loadingMovieDefault())) {
        parkedUnderlay_ = std::move(video_);
        parkedName_ = videoName_;
    }
    video_.reset();
}

bool FrontendRuntime::openVideo(const std::string& name, bool loop) {
    stopMovieAudio();
    releaseVideo();
    videoName_ = name;
    videoLoops_ = loop;
    std::string path = Catalog::defaultExtractedRoot() + "/movies/" + name + ".mkv";
    std::unique_ptr<platform::IMoviePlayer> p;
    const bool reused = loop && parkedUnderlay_ && parkedName_ == name;
    if (reused) {   // from its first frame again; decoded now so the last loading screen's final frame is not shown
        p = std::move(parkedUnderlay_);
        parkedName_.clear();
        p->restart();
        p->advance(0.0);
    } else {
        p.reset(movieFactory_ && !std::getenv("WFC_NO_VIDEO") ? movieFactory_() : nullptr);
        if (!p || !p->open(path)) {
            FlowTrace::emit("movie.unavailable", {{"movie", name}, {"file", path}, {"decoder", FlowTrace::boolean(p != nullptr)}});
            return false;
        }
    }
    FlowTrace::emit("movie.open", {{"movie", name}, {"seconds", std::to_string(p->duration())}, {"loop", FlowTrace::boolean(loop)},
                                   {"reused", FlowTrace::boolean(reused)}});
    // SeqAct_MoviePlayer movies carry their audio; the loading underlays have none (AssetTools video_audio probe).
    videoPath_ = path;
    movieAudioWanted_ = !loop && audio_;
    video_ = std::move(p);
    videoFramed_ = false;
    ++videoGen_;
    return true;
}

void FrontendRuntime::updateMoviePlayer(float dt, const platform::InputFrame& in) {
    // SeqAct_MoviePlayer (intro chain): the Bink movies, extracted by AssetTools as H.264/FLAC .mkv, decoded by the
    // platform movie player. Stopped fires at the end of the movie. The movie's sound is its own Bink audio tracks,
    // decoded and played by Systems (IFrontendAudio::startMovieAudio / stopMovieAudio, Systems M07 MovieAudioPlayer),
    // started with the first video frame and stopped at its end / skip [integration M06: one decoder, Systems'].
    // The loading underlay: [LoadingMovie] InitialStartupFileName / DefaultFileName (Xe-TransGame.ini), looped under
    // LoadScreen_GFX while a loading screen is up [HIGH]. The extracted files carry region / language suffixes; the
    // rebuild picks <name>_NA_INT, then <name>_INT, then <name> [PARTIAL: region of the dump UNKNOWN, see GetRegionCode].
    const bool scripted = flow_.kismetMovie().empty() && !flow_.scriptMovie().empty();
    const std::string& m = scripted ? flow_.scriptMovie() : flow_.kismetMovie();
    std::string want = m;
    auto stopped = [&](const std::string& name) { if (scripted) flow_.scriptMovieStopped(); else flow_.movieStopped(name); };
    if (want.empty() && flow_.loading().active && !flow_.loading().binkMovie.empty()) {
        const std::string& b = flow_.loading().binkMovie;
        if (b != underlayFor_) {
            underlayFor_ = b;
            underlay_ = localizedMovie(b);
        }
        want = underlay_;
        if (flow_.loading().kind == "InitialStartup" && !bootDone_) bootUnderlay_ = underlay_;
    }
    if (!m.empty() || flow_.frontEndStarted()) bootDone_ = true;
    const bool bootHold = want.empty() && !bootDone_ && !bootUnderlay_.empty();
    if (bootHold) want = bootUnderlay_;   // the startup movie, full screen, until the logos start
    fullScreenMovie_ = !m.empty() || bootHold;
    if (want != videoName_) {
        if (want.empty()) { stopMovieAudio(); releaseVideo(); videoName_.clear(); }
        else if (!openVideo(want, m.empty()) && !m.empty()) { stopped(m); videoName_.clear(); }
    }
    if (!video_) return;
    // A newly opened movie starts at its first frame: the long frame that opened it (e.g. a blocking scene load) is
    // not counted as playback time (the Activision logo used to start 0.25 s in).
    // The loading underlay / boot hold (no Kismet movie, no synced audio) advances at most one movie frame per update:
    // after a blocking frame (a scene load) the decoder would otherwise decode the whole backlog at once (7 frames, 48 ms on
    // the frame the title opens) instead of the picture pausing for the stall [PC ADAPTATION: the original Bink loading
    // movie decodes on its own thread]. Kismet movies keep real time (their audio is synced to it).
    float advanceBy = videoFramed_ ? dt : 0.0f;
    if (m.empty()) advanceBy = std::min(advanceBy, 1.0f / 30.0f);
    video_->advance(advanceBy);
    const uint8_t* px = nullptr;
    int vw = 0, vh = 0;
    uint64_t serial = 0;
    if (!videoFramed_ && video_->frame(px, vw, vh, serial)) {
        videoFramed_ = true;
        FlowTrace::emit("movie.firstFrame", {{"movie", videoName_}, {"w", std::to_string(vw)}, {"h", std::to_string(vh)},
                                             {"t", std::to_string(video_->position())}});
        if (movieAudioWanted_ && audio_) {   // audio starts with the picture
            movieAudioPlaying_ = audio_->startMovieAudio(videoPath_);
            // handle: 1 = a Systems movie stream is playing, -1 = the movie has no audio / not played
            FlowTrace::emit("movie.audioStart", {{"movie", videoName_}, {"handle", movieAudioPlaying_ ? "1" : "-1"},
                                                 {"owner", "systems"}});
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
        if (!m.empty()) stopped(done);
    }
}

void FrontendRuntime::updateScene(float dt) {
    // The live level under the menus (FrontendScene): its levels follow the current UI level; the menu movies'
    // fscommands and the intro's Stopped output start its matinees; the camera is evaluated every frame.
    LevelKind lv = flow_.loading().active ? LevelKind::None : flow_.level();
    std::string map = (lv == LevelKind::None || lv == LevelKind::Match) ? std::string() : flow_.levelMap();
    const auto& ev = flow_.kismetTriggers();
    if (map != sceneLevel_) {
        if (sceneRenderer_ && sceneDrawable_) sceneRenderer_->unload();
        scene_.leave();
        sceneDrawable_ = false;
        if (map.empty()) sceneSeen_ = ev.size();   // triggers raised from here on (during the travel) belong to the next level
        sceneLevel_ = map;
        if (!map.empty()) {
            scene_.enterLevel(map);
            auto loadScene = [&] { sceneDrawable_ = sceneRenderer_ && !scene_.levels().empty() && sceneRenderer_->load(scene_.levels()); };
            sceneLoading_ = true;   // frames presented during the load do not draw the half-loaded scene
            if (sceneLoadWrapper) sceneLoadWrapper(loadScene);
            else loadScene();
            sceneLoading_ = false;
            std::string lvls;
            for (const std::string& l : scene_.levels()) lvls += (lvls.empty() ? "" : "+") + l;
            FlowTrace::emit("scene.levels", {{"uiLevel", map}, {"levels", lvls}, {"drawn", FlowTrace::boolean(sceneDrawable_)},
                                             {"why", sceneDrawable_ ? "" : sceneRenderer_ ? "renderer: levels not exported / not drawable"
                                                                                           : "no scene renderer (Rendering handoff)"}});
        }
    }
    if (map.empty()) return;
    for (; sceneSeen_ < ev.size(); ++sceneSeen_) scene_.trigger(ev[sceneSeen_]);
    scene_.tick(dt);
    if ((sceneTraceTimer_ += dt) >= 2.0f) {
        sceneTraceTimer_ = 0.0f;
        SceneView v = scene_.view();
        std::string playing;
        for (const std::string& p : scene_.playing()) playing += (playing.empty() ? "" : ",") + p;
        if (v.valid)
            FlowTrace::emit("scene.view", {{"camera", v.camera}, {"matinee", v.matinee}, {"playing", playing},
                                           {"pos", FlowTrace::num(v.pos[0]) + "," + FlowTrace::num(v.pos[1]) + "," + FlowTrace::num(v.pos[2])},
                                           {"rot", FlowTrace::num(v.rot[0]) + "," + FlowTrace::num(v.rot[1]) + "," + FlowTrace::num(v.rot[2])},
                                           {"fov", FlowTrace::num(v.fov)}, {"drawn", FlowTrace::boolean(sceneDrawable_)}});
    }
}

void FrontendRuntime::update(const platform::InputFrame& input, float dt) {
    platform::InputFrame in = input;
    script_.applySynthetic(in);
    { core::prof::Scope prof("flow.tick"); flow_.tick(dt); }
    { core::prof::Scope prof("shims"); runNativeShims(); }
    { core::prof::Scope prof("movie.player"); updateMoviePlayer(dt, in); }
    // Full-screen movie mode (BeginMovieMode: UI event 12, the UI hidden) takes all input; after the movie the menus
    // see input again once the skip key is released, so the skip press does not also act on the menu [HIGH].
    bool fullScreenMovie = !flow_.kismetMovie().empty() || !flow_.scriptMovie().empty();
    if (fullScreenMovie) movieInputHold_ = true;
    else if (movieInputHold_ && in.uiDown == 0 && !in.mouseLeft) movieInputHold_ = false;
    platform::InputFrame none;
    none.mouseX = in.mouseX; none.mouseY = in.mouseY;
    if (presenter_) presenter_->update(flow_, movieInputHold_ ? none : in, dt);
    { core::prof::Scope prof("script"); script_.update(flow_, dt); }
    { core::prof::Scope prof("+audio.update"); updateAudio(dt); }
    updateProgression();
    { core::prof::Scope prof("+scene.update"); updateScene(dt); }
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
    updateProgression();
    // TnHUD: the HUD movie exists for the match; visible in UI states InGame / Spectating only (RE A8).
    bool inMatch = flow_.level() == LevelKind::Match && !flow_.loading().active;
    UIState st = flow_.ui().state();
    bool hudShown = inMatch && (st == UIState::InGame || st == UIState::Spectating);
    hud_.update(presenter_.get(), catalog_, inMatch, hudShown);
    // ShowScores (Back / Tab): TnHUD.SetShowScores(!bShowScores) toggles InGameStats_GFX with input focus; it is
    // force-closed when the HUD is hidden [RE OVERNIGHT A7 / playtest section 9, CONFIRMED].
    uint32_t pressed = in.uiDown & ~prevMatchUi_;
    prevMatchUi_ = in.uiDown;
    bool want = scoreboard_;
    if (hudShown && (pressed & (1u << (int)platform::UiKey::Select))) want = !scoreboard_;
    if (!hudShown) want = false;
    if (want != scoreboard_) {
        scoreboard_ = want;
        if (presenter_) presenter_->setScoreboard(want);
        FlowTrace::emit("hud.scoreboard", {{"open", FlowTrace::boolean(want)}});
    }
}

void FrontendRuntime::updateLoading(float dt) {
    platform::InputFrame none;
    updateMoviePlayer(dt, none);
    if (presenter_) presenter_->advanceLoading(dt);
}

void FrontendRuntime::draw(int w, int h) {
    // A full-screen movie hides the game presentation (BeginMovieMode): the scene is not drawn under it.
    bool prewarm = prewarmScene_;
    prewarmScene_ = false;
    if (sceneRenderer_ && sceneDrawable_ && !fullScreenMovie_ && (!sceneLoading_ || prewarm)) {
        for (const SceneChange& c : scene_.takeChanges()) {
            if (c.kind == SceneChange::Effect) sceneRenderer_->setEffectActive(c.actor, c.value);
            else sceneRenderer_->setActorHidden(c.actor, c.value);
        }
        SceneView v = scene_.view();
        {   // emblem state for gates: logged when a material parameter crosses its midpoint
            for (const auto& mp : v.materialParams) {
                bool on = mp.value >= 0.5;
                auto key = mp.actor + "." + mp.param;
                auto it = emblemOn_.find(key);
                if (it == emblemOn_.end() || it->second != on) {
                    emblemOn_[key] = on;
                    FlowTrace::emit("scene.emblem", {{"actor", mp.actor}, {"param", mp.param}, {"state", on ? "on" : "off"}});
                }
            }
        }
        if (v.valid) { core::prof::Scope prof("scene.draw"); sceneRenderer_->draw(v, w, h); }
    }
    static const bool sceneOnly = std::getenv("WFC_SCENE_ONLY") != nullptr;   // diagnostics: the 3D layer alone
    if (sceneOnly) return;
    if (!presenter_) return;
    const uint8_t* px = nullptr;
    int vw = 0, vh = 0;
    uint64_t serial = 0;
    bool over = fullScreenMovie_;   // SeqAct_MoviePlayer / Game.PlayMovie / the boot startup movie
    // The serial is unique across movies (the presenter re-uploads on change).
    if (video_ && video_->frame(px, vw, vh, serial)) presenter_->setVideoFrame(px, vw, vh, (videoGen_ << 40) | serial, over);
    else presenter_->setVideoFrame(nullptr, 0, 0, 0, false);
    presenter_->draw(flow_, w, h);
}

std::string FrontendRuntime::titleText() const {
    return "WFC Rebuild | " + flow_.stateSummary();
}

} // namespace frontend
