// Campaign player interacts (TnInteract*: buttons, hacking terminals, detpacks, emotes, energon, the Omega batteries' corrupt) and their
// TnSeqEvent_Interact events (Touch / Untouch / Begin Use / End Use, Instigator) [C, RE CAMPAIGN_CONTENT_DELTAS 2.2a]: a tap of Interact
// in the class's use range and cone (+-45 deg) in robot form -> Begin Use (the user invincible), End Use when the class's sequence
// completes (PROV lengths); single-use classes disable afterwards; SeqAct_Toggle on the actor 0 On / 1 Off / 2 Toggle; humans only (the
// AI uses them through SeqAct_TnAiUseInteract). PROV: no slide to the hotspot, no anims / camera / hacking movie, the Optimus / Megatron
// gates of Light / Dark Energon not applied. WFC_CPAUTOUSE=1 (test only): a human in range uses it without the button.
#include "game/World.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game {

namespace {
bool readJ3(const std::string& path, assets::Json& j) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return assets::Json::parse(ss.str(), j);
}
std::string tail3(const std::string& s) { const size_t k = s.find_last_of('.'); return k == std::string::npos ? s : s.substr(k + 1); }
// Per class [C, RE CAMPAIGN_CONTENT_DELTAS 2.2a]: TouchDistance (UU / 100), the use range of the condition (Button 300, Hacking 200 UU), single
// use (a DisableInteract action) and the Begin Use sequence's length (PROV seconds: the class anims / movie until their lengths land).
void classOf(CampaignInteract& I) {
    const std::string& c = I.cls;
    I.touch = 10.0f; I.useRange = 10.0f; I.hold = 2.0f; I.singleUse = false;
    if (c.find("ButtonReusable") != std::string::npos) { I.touch = 5.0f; I.useRange = 3.0f; I.hold = 1.5f; }
    else if (c.find("Button") != std::string::npos) { I.touch = 5.0f; I.useRange = 3.0f; I.hold = 1.5f; I.singleUse = true; }
    else if (c.find("HackingTerminal") != std::string::npos) { I.touch = 5.0f; I.useRange = 2.0f; I.hold = 6.0f; }
    else if (c.find("Detpack") != std::string::npos) { I.touch = 5.0f; I.useRange = 5.0f; I.hold = 1.5f; I.singleUse = true; }
    else if (c.find("CorruptOmegaBattery") != std::string::npos) { I.touch = 5.0f; I.useRange = 5.0f; I.hold = 2.0f; I.singleUse = true; I.enabled = false; }
    else if (c.find("DarkEnergon") != std::string::npos) { I.touch = 6.0f; I.useRange = 6.0f; I.hold = 3.0f; }
    else if (c.find("LightEnergon") != std::string::npos) { I.touch = 8.0f; I.useRange = 8.0f; I.hold = 3.0f; I.singleUse = true; }
    else if (c.find("Emote") != std::string::npos) { I.hold = 2.5f; }
}
} // namespace

void World::cpLoadInteracts() {
    cp_.interacts.clear();
    assets::Json j;
    if (!readJ3(cpChapter_.dir + "actors.json", j)) return;
    const assets::Json& as = j["actors"];
    for (size_t i = 0; i < as.size(); ++i) {
        const std::string cls = as[i]["class"].asString();
        if (cls.rfind("TnInteract", 0) != 0) continue;
        const assets::Json& L = as[i]["location_gltf"];
        if (L.size() < 3) continue;
        CampaignInteract I;
        I.name = as[i]["actor"].asString();
        I.cls = cls;
        I.pos = core::Vec3{L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
        classOf(I);
        cp_.interacts.push_back(I);
    }
    int bound = 0;
    for (int op = 0; op < (int)cp_.vm.opCount(); ++op) {
        const KismetVM::Op& o = cp_.vm.op(op);
        if (o.cls != "TnSeqEvent_Interact") continue;
        const std::string org = tail3(o.authored["Originator"].asString());
        for (CampaignInteract& I : cp_.interacts) if (I.name == org) { I.events.push_back(op); ++bound; }
    }
    LOG_INFO("campaign: %zu interacts, %d interact events bound", cp_.interacts.size(), bound);
}

void World::cpInteractFire(CampaignInteract& I, const char* out, int user) {
    for (int op : I.events) {
        for (int v : cp_.vm.varLink(op, "Instigator")) if (KismetVM::Var* var = cp_.vm.var(v)) var->obj = "pawn:" + std::to_string(user);
        cp_.vm.fireEvent(op, out);
    }
    LOG_INFO("campaign: interact %s (%s): %s by p%d", I.name.c_str(), I.cls.c_str(), out, user);
}

// SeqAct_Toggle whose targets include interacts: Turn On (0) / Turn Off (1) / Toggle (2).
bool World::cpInteractActivate(KismetVM& vm, int op, int input) {
    const KismetVM::Op& o = vm.op(op);
    if (o.cls != "SeqAct_Toggle") return false;
    bool any = false;
    for (int v : vm.varLink(op, "Target")) {
        const KismetVM::Var* var = vm.var(v);
        if (var && var->named >= 0) var = vm.var(var->named);
        if (!var) continue;
        const std::string nm = tail3(var->obj);
        for (CampaignInteract& I : cp_.interacts)
            if (I.name == nm) { I.enabled = input == 0 ? true : input == 1 ? false : !I.enabled; any = true; }
    }
    if (!any) return false;
    if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);
    return true;
}

void World::cpTickInteracts(float dt) {
    static const bool autoUse = [] { const char* e = std::getenv("WFC_CPAUTOUSE"); return e && std::atoi(e) != 0; }();
    cp_.interactPrompt.clear();
    if (match_.state() != Match::State::InProgress) return;
    if (cp_.interactHeldPrev.size() < match_.players().size()) cp_.interactHeldPrev.resize(match_.players().size(), 0);
    // the uses in progress: End Use when the class's Begin Use sequence completes; a dead / gone user is force-ended
    for (CampaignInteract& I : cp_.interacts) {
        if (I.user < 0) continue;
        const bool gone = (size_t)I.user >= match_.players().size() || !match_.players()[(size_t)I.user].alive;
        I.t += dt;
        if (I.t >= I.hold || gone || !I.enabled) {
            cpInteractFire(I, "End Use", I.user);
            I.user = -1;
            if (I.singleUse) I.enabled = false;   // DisableInteract
        }
    }
    for (size_t q = 0; q < match_.players().size(); ++q) {
        const MatchPlayer& mp = match_.players()[q];
        bool held = false;
        if ((int)q == localPlayer_) held = player_.controller().interactHeld();
        else if (q < seatInteract_.size()) held = seatInteract_[q] != 0;
        const bool pressed = (held && !cp_.interactHeldPrev[q]) || autoUse;
        cp_.interactHeldPrev[q] = held ? 1 : 0;
        if (mp.kind == ParticipantKind::Bot || !mp.alive || cp_.cinematicMode) continue;
        const Character* c = participantPawn((int)q);
        if (!c) continue;
        bool busy = false;
        for (const CampaignInteract& I : cp_.interacts) if (I.user == (int)q) busy = true;
        const core::Vec3 p = c->position();
        const float fx = -std::sin(c->yaw()), fz = -std::cos(c->yaw());
        CampaignInteract* best = nullptr; float bd = 1e30f;
        for (CampaignInteract& I : cp_.interacts) {
            const float dx = I.pos.x - p.x, dz = I.pos.z - p.z, d = std::sqrt(dx * dx + dz * dz);
            const bool touch = I.enabled && d <= I.touch && std::fabs(I.pos.y - p.y) <= std::max(2.5f, I.touch * 0.5f);
            // Touch / Untouch: the touch cylinder (one toucher tracked: the first human in it)
            if (touch && I.toucher < 0) { I.toucher = (int)q; cpInteractFire(I, "Touch", (int)q); }
            else if (!touch && I.toucher == (int)q) { I.toucher = -1; cpInteractFire(I, "Untouch", (int)q); }
            // the use test: in range, the use cone (+-45 deg toward it), robot form, not in use
            if (!touch || busy || I.user >= 0 || c->moveForm() != Form::Robot || d > I.useRange) continue;
            if (d > 0.3f && (dx * fx + dz * fz) / d < 0.7071f) continue;
            if (d < bd) { bd = d; best = &I; }
        }
        if (!best) continue;
        if ((int)q == localPlayer_) cp_.interactPrompt = best->cls;   // SetInteractState(1): the prompt
        if (!pressed) continue;
        best->user = (int)q; best->t = 0.0f;   // a tap: AdjustingUser (PROV: no slide) -> Interacting
        cpInteractFire(*best, "Begin Use", (int)q);
    }
}

// The user of an interact is invincible while using it (MakeUserInvincible, default true).
bool World::cpInteractUserInvincible(int player) const {
    for (const CampaignInteract& I : cp_.interacts) if (I.user == player) return true;
    return false;
}

} // namespace game
