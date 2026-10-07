#include "frontend/Hud.h"

#include <algorithm>
#include "frontend/Catalog.h"
#include "frontend/FlowTrace.h"
#include "frontend/FrontendRuntime.h"

#include <cmath>

namespace frontend {

namespace {
// TnMessageTextColors [RE A2, CONFIRMED].
const char* kLocal = "#FFFFFF";
const char* kTeammate = "#50B5D5";
const char* kEnemy = "#F03C3C";
const char* kNeutral = "#FF9333";

std::string font(const char* color, const std::string& text) {
    if (text.empty()) return "";
    return std::string("<font color='") + color + "'>" + text + "</font>";
}

std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : std::string(1, c);
    return o;
}
} // namespace

std::string HudController::abilityIconId(const std::string& id) {
    if (id.empty() || id == "None") return "None";
    if (id.rfind("TnAbility", 0) == 0 || id.rfind("Team", 0) == 0) return id;
    return "TnAbility" + id;
}

int HudController::grenadeTypeFor(const std::string& id) {
    if (id.empty()) return 0;
    auto has = [&](const char* k) { return id.find(k) != std::string::npos; };
    if (has("Flash")) return 2;
    if (has("Mine")) return 3;
    if (has("Heal")) return 4;
    return 1;
}

void HudController::reset() {
    sentValid_ = false;
    wasOpen_ = wasVisible_ = wasSpectating_ = false;
    kills_.clear();
    announcements_.clear();
    rewards_.clear();
    events_.clear();
    progressLabel_.clear();
}

std::string HudController::killMessage(const Catalog& cat, const HudKill& k, int localTeam) {
    // TnDeathMessage.GetColoredString: the damage type's DeathString (Suicide for suicides), `k = killer, `o = victim;
    // names coloured local / teammate / enemy, the remaining words neutral; bracket tokens stay for the movie's image
    // substitutions. Unknown damage type -> the base [TnDamageType] template.
    std::string section = k.damageType.empty() ? "TnDamageType" : k.damageType;
    std::string tmpl = cat.localize("TransGame", section, k.suicide ? "Suicide" : "DeathString");
    if (tmpl.empty()) tmpl = cat.localize("TransGame", "TnDamageType", k.suicide ? "Suicide" : "DeathString");
    auto colorOf = [&](bool local, int team) {
        if (local) return kLocal;
        return (team >= 0 && team == localTeam) ? kTeammate : kEnemy;
    };
    std::string out, words;
    auto flushWords = [&]() { out += font(kNeutral, escape(words)); words.clear(); };
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '`' && i + 1 < tmpl.size() && (tmpl[i + 1] == 'k' || tmpl[i + 1] == 'o')) {
            flushWords();
            bool killer = tmpl[i + 1] == 'k';
            out += killer ? font(colorOf(k.killerLocal, k.killerTeam), escape(k.killer))
                          : font(colorOf(k.victimLocal, k.victimTeam), escape(k.victim));
            ++i;
            continue;
        }
        if (tmpl[i] == '[') {   // image token: outside the font runs
            flushWords();
            size_t e = tmpl.find(']', i);
            if (e == std::string::npos) e = tmpl.size() - 1;
            out += tmpl.substr(i, e - i + 1);
            i = e;
            continue;
        }
        words += tmpl[i];
    }
    flushWords();
    return out;
}

void HudController::update(IMoviePresenter* p, const Catalog& cat, bool open, bool visible) {
    if (!p) return;
    if (open != wasOpen_) {
        p->setHud(open, open && visible);
        wasOpen_ = open;
        wasVisible_ = open && visible;
        sentValid_ = false;
        FlowTrace::emit("hud.open", {{"open", FlowTrace::boolean(open)}});
        if (!open) { kills_.clear(); announcements_.clear(); rewards_.clear(); events_.clear(); return; }
    }
    if (!open) return;
    if ((open && visible) != wasVisible_) {
        p->setHud(true, visible);
        wasVisible_ = visible;
        FlowTrace::emit("hud.visible", {{"visible", FlowTrace::boolean(visible)}});
    }
    auto call = [&](const char* fn, std::vector<BridgeValue> args) { p->hudCall(std::string("_global.") + fn, args); };
    if (attacking_) frame_.attackingTeamStatus = attacking_;
    const HudFrame& f = frame_;
    if (f.valid) {
        if (!sentValid_) {
            // A fresh movie: crosshair and the form's widgets. The crosshair type is chosen by the movie itself from
            // NotifyCurrentWeaponChanged's class name (RE 934ecde), so no SetWeaponCrosshair here.
            call("ShowCrosshair", {true});
        }
        if (!sentValid_ || f.fullSegments != sent_.fullSegments || std::fabs(f.currentSegment - sent_.currentSegment) > 1e-3 ||
            f.totalSegments != sent_.totalSegments)
            call("NotifySegmentedHealthChanged", {f.fullSegments, f.currentSegment, f.totalSegments});
        if (!sentValid_ || std::fabs(f.overshield - sent_.overshield) > 1e-3) call("NotifyOverShieldChanged", {f.overshield});
        if (!sentValid_ || f.weapon != sent_.weapon) {
            std::string cls = f.weapon.rfind("Tn", 0) == 0 || f.weapon.empty() ? f.weapon : "TnWeapon" + f.weapon;
            call("NotifyCurrentWeaponChanged", {cls});
        }
        if (!sentValid_ || f.aimType != sent_.aimType) call("NotifyFineAimChanged", {f.aimType});
        if (!sentValid_ || f.clip != sent_.clip || f.clipCapacity != sent_.clipCapacity)
            call("NotifyWeaponClipAmmoChanged", {f.clip, f.clipCapacity});
        if (!sentValid_ || f.reserve != sent_.reserve || f.reserveCapacity != sent_.reserveCapacity)
            call("NotifyWeaponReserveAmmoChanged", {f.reserve, f.reserveCapacity});
        if (!sentValid_ || f.vehicleForm != sent_.vehicleForm) call("NotifyCurrentFormChanged", {f.vehicleForm ? 1 : 0});
        // The observers that have a source (see HudFrame): on change, or all again for a fresh movie.
        auto changed = [&](const auto& now, const auto& was) { return now && (!sentValid_ || !was || *now != *was); };
        if (changed(f.progress, sent_.progress) || (f.progress && f.progressObserver != sent_.progressObserver)) {
            // A bar that ends without an observer (the glue sends 0) keeps the last label, so the text fades out with
            // the bar (Hud_GFX sets description_txt on every call, the 0 call included).
            if (f.progressObserver) {
                progressLabel_ = cat.localize("TransGame", *f.progressObserver, "label");
                for (size_t at; (at = progressLabel_.find("`p")) != std::string::npos;) progressLabel_.replace(at, 2, f.progressName);
            }
            call("NotifyProgressBarChanged", {progressLabel_, *f.progress});
        }
        if (changed(f.attackingTeamStatus, sent_.attackingTeamStatus)) call("NotifyOnAttackingTeamChanged", {*f.attackingTeamStatus});
        if (changed(f.killstreakId, sent_.killstreakId)) call("NotifyKillstreakChanged", {*f.killstreakId});
        static const char* kAbilityChanged[3] = {"NotifyAbilityType0Changed", "NotifyAbilityType1Changed", "NotifyAbilityType2Changed"};
        static const char* kAbilityCooldown[3] = {"NotifyAbilityType0UpdateCooldown", "NotifyAbilityType1UpdateCooldown", "NotifyAbilityType2UpdateCooldown"};
        for (int i = 0; i < 3; ++i) {
            const auto& a = f.abilities[(size_t)i];
            const auto& w = sent_.abilities[(size_t)i];
            if (!a) continue;
            if (!sentValid_ || !w || a->id != w->id) call(kAbilityChanged[i], {abilityIconId(a->id)});
            if (!sentValid_ || !w || a->cooldown != w->cooldown || a->fraction != w->fraction) call(kAbilityCooldown[i], {a->cooldown, a->fraction});
        }
        if (changed(f.grenadeAmmo, sent_.grenadeAmmo) || changed(f.grenadeType, sent_.grenadeType))
            call("NotifyGrenadeAmmoChanged", {f.grenadeAmmo.value_or(0), f.grenadeType.value_or(0)});
        if (changed(f.activeGrenades, sent_.activeGrenades)) call("NotifyActiveGrenadeCount", {*f.activeGrenades});
        if (changed(f.playerYaw, sent_.playerYaw)) call("NotifyPlayerRotationChanged", {*f.playerYaw});
        if (changed(f.lockOnState, sent_.lockOnState)) call("NotifyLockOnStateChanged", {*f.lockOnState});
        if (changed(f.targetName, sent_.targetName)) call("NotifyTargetNameChanged", {*f.targetName});
        if (changed(f.targetType, sent_.targetType)) call("NotifyTargetTypeChanged", {*f.targetType});
        if (changed(f.targetHealth, sent_.targetHealth)) call("NotifyTargetHealthChanged", {f.targetType.value_or(0), *f.targetHealth});
        if (changed(f.weaponJammed, sent_.weaponJammed)) call("NotifyWeaponJammedChanged", {*f.weaponJammed});
        if (changed(f.weaponSpread, sent_.weaponSpread)) call("NotifyWeaponSpreadChanged", {*f.weaponSpread});
        if (changed(f.weaponMessage, sent_.weaponMessage)) call("NotifyWeaponMessageChanged", {*f.weaponMessage});
        if (changed(f.downedHealth, sent_.downedHealth)) call("NotifyNormalizedDownedHealthChanged", {*f.downedHealth});
        if (changed(f.hudScrambled, sent_.hudScrambled)) call("NotifyHudScrambledChanged", {*f.hudScrambled});
        if (changed(f.scoringMultiplier, sent_.scoringMultiplier)) call("NotifyScoringMultiplierChanged", {*f.scoringMultiplier});
        if (changed(f.increaseDamage, sent_.increaseDamage)) call("NotifyDamageIncrease", {*f.increaseDamage});
        if (f.contextualPrompts) {
            static const std::vector<std::string> kNone;
            const auto& was = sentValid_ && sent_.contextualPrompts ? *sent_.contextualPrompts : kNone;
            auto in = [](const std::vector<std::string>& v, const std::string& t) { return std::find(v.begin(), v.end(), t) != v.end(); };
            for (const auto& t : was) if (!in(*f.contextualPrompts, t)) call("NotifyContextualCommand", {0, 1, t});
            for (const auto& t : *f.contextualPrompts) if (!in(was, t)) call("NotifyContextualCommand", {0, 0, t});
        }
        if (f.cantTransformCount && sentValid_ && sent_.cantTransformCount)
            for (int i = *sent_.cantTransformCount; i < *f.cantTransformCount; ++i) call("NotifyCantTransform", {});
        if (!sentValid_ || f.spectating != sent_.spectating) {
            call("NotifySpectating", {f.spectating});
            call("GameMessageSpectatorMode", {f.spectating});   // the kill feed moves up 160 px while spectating
        }
        sent_ = f;
        sentValid_ = true;
    }
    for (const Event& e : events_) call(e.fn, e.args);
    events_.clear();
    for (const PendingKill& k : kills_) {
        std::string html = killMessage(cat, k.k, k.localTeam);
        call("GameMessage", {html});
        FlowTrace::emit("hud.killFeed", {{"html", html}});
    }
    kills_.clear();
    for (const std::string& a : announcements_) { call("GameAnnouncement", {a}); FlowTrace::emit("hud.announcement", {{"text", a}}); }
    announcements_.clear();
    for (const std::string& r : rewards_) { call("RewardAnnouncement", {r}); FlowTrace::emit("hud.reward", {{"text", r}}); }
    rewards_.clear();
}

} // namespace frontend
