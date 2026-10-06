// Clean-room reconstruction — XP / stat award producer (see Progression.h).
#include "game/Progression.h"
#include "game/Match.h"
#include <algorithm>
#include <cstring>

namespace game {

namespace {
#include "game/ProgressionTable.inc"

const XpEventRow* xpRow(const std::string& id) {
    for (const XpEventRow& r : kXpEvents) if (id == r.id) return &r;
    return nullptr;
}
// "TransGame.TnBuffCloak" / "TnBuffCloak" -> "TnBuffCloak"
std::string shortCls(const std::string& s) { const size_t d = s.rfind('.'); return d == std::string::npos ? s : s.substr(d + 1); }
bool hasBuff(const ParticipantSnapshot& p, const char* cls, int* instigator = nullptr) {
    if (!cls) return false;
    for (const ParticipantSnapshot::Buff& b : p.buffs) if (shortCls(b.cls) == cls) { if (instigator) *instigator = b.instigator; return true; }
    return false;
}
const char* vftName(int vehicleType) {   // ETnVehicleFormType of the victim's current form (VFT_Invalid = robot)
    switch (vehicleType) { case 0: return "VFT_Car"; case 1: return "VFT_Truck"; case 2: return "VFT_Tank"; case 3: return "VFT_Plane"; default: return ""; }
}
}

int AwardProducer::xpAmount(const std::string& id, bool teamGame) {
    const XpEventRow* r = xpRow(id);
    if (!r) return 0;
    return (int)(kGlobalXpMultiplier * (float)((r->dmXp >= 0 && !teamGame) ? r->dmXp : r->xp));
}

int AwardProducer::challengeStatId(const char* challenge) {
    for (const ChallengeStatRow& r : kChallengeStats) if (std::strcmp(r.challenge, challenge) == 0) return r.statId;
    return -1;
}

void AwardProducer::reset() {
    // Serials are never reused, so lastSerial_ stays; per-match detectors restart.
    nextTxn_ = 1; firstKill_ = firstDeath_ = false;
    recentKills_.clear(); dominate_.clear(); lastKiller_.clear(); totals_.clear();
}

void AwardProducer::xp(int player, int txn, const std::string& id, bool teamGame, const std::string& extra) {
    if (player < 0) return;
    const XpEventRow* r = xpRow(id);
    if (!r) return;
    XpAward a;
    a.player = player; a.transactionId = txn; a.xp = xpAmount(id, teamGame);
    a.announcement = r->announcement; a.description = r->description; a.extra = extra; a.eventId = id;
    totals_[player] += a.xp;
    if (xp_.size() >= 4096) xp_.erase(xp_.begin(), xp_.begin() + 2048);   // nobody draining (direct boot): keep the newest
    xp_.push_back(a);
}

void AwardProducer::stat(int player, int statId, long amount, int updateType) {
    if (player < 0 || statId <= 0) return;
    if (stats_.size() >= 8192) stats_.erase(stats_.begin(), stats_.begin() + 4096);
    stats_.push_back({player, statId, amount, updateType});
}

void AwardProducer::consume(const Match& m) {
    const bool team = m.settings().teamGame;
    for (const GameplayEvent& e : m.gameplayEvents()) {
        if (e.serial <= lastSerial_) continue;
        lastSerial_ = e.serial;
        switch (e.type) {
            case GameplayEventType::MatchStart: reset(); break;
            case GameplayEventType::Kill: onKill(m, e); break;
            case GameplayEventType::Assist: {
                // AddAssist(damage / HealthMax): > 0.5 Assist, > 0.25 WeakAssist [CONF RE]; the basic assist challenge stat.
                const int txn = nextTxn_++;
                if (e.assistFraction > 0.5f) xp(e.instigator, txn, "Assist", team);
                else if (e.assistFraction > 0.25f) xp(e.instigator, txn, "WeakAssist", team);
                if (e.assistFraction > 0.25f) stat(e.instigator, challengeStatId("CHALLENGE_BASIC_ASSIST"));
                break;
            }
            case GameplayEventType::KillstreakEarned: break;   // folded into the kill's transaction (onKill looks ahead)
            case GameplayEventType::Objective: {
                const int txn = nextTxn_++;
                const std::string& o = e.objective;
                if (o == "FlagCapture") xp(e.instigator, txn, "FlagCapture", team);
                else if (o == "FlagReturn") xp(e.instigator, txn, "FlagReturn", team);
                else if (o == "BombPlant") xp(e.instigator, txn, "BombPlant", team);
                else if (o == "BombDetonate") xp(e.instigator, txn, "BombDetonate", team);
                else if (o == "BombDefuse") xp(e.instigator, txn, "BombDefuse", team);
                else if (o == "NodeCapture") xp(e.instigator, txn, "DominationCapture", team);
                else if (o == "ZoneHold") {
                    // At zone exit / deactivation, by the stay's points / 10: 0-9 .. >= 50 -> ZoneHold0..5 [CONF RE].
                    const int tier = std::min(5, std::max(0, e.value / 10));
                    if (e.value > 0) xp(e.instigator, txn, "ZoneHold" + std::to_string(tier), team);
                }
                break;
            }
            case GameplayEventType::MatchEnd: {
                // TnVersusGame.CheckEndGame: GameWin XP to every player on the winning PRI's team, GameLose to the rest; versus
                // (team) games only - DM gives none [CONF RE quirk C].
                if (!team) break;
                for (size_t p = 0; p < m.players().size(); ++p) {
                    const int txn = nextTxn_++;
                    if (e.xpWinTeam >= 0 && m.players()[p].team == e.xpWinTeam) xp((int)p, txn, "GameWin", team);
                    else xp((int)p, txn, "GameLose", team);
                }
                break;
            }
            default: break;
        }
    }
}

void AwardProducer::onKill(const Match& m, const GameplayEvent& e) {
    const bool team = m.settings().teamGame;
    const int k = e.instigator, v = e.victim;
    if (k < 0 || v < 0 || k == v) return;
    const ParticipantSnapshot& K = e.instigatorState;
    const ParticipantSnapshot& V = e.victimState;
    const std::string dmg = shortCls(e.damageType);
    const int txn = nextTxn_++;
    // TnKillAwardManager.AwardKill: the base Kill event first, then each rule in order (one transaction).
    xp(k, txn, "Kill", team);
    stat(k, challengeStatId("CHALLENGE_BASIC_KILLS"));
    if (K.vehicleForm) stat(k, challengeStatId("CHALLENGE_BASIC_KILLS_VEHICLE"));
    // Killstreak (exactly 3 / 5 / 7) with the earned reward in extra data; Prime streak stats.
    const int streak = K.killStreak + 1;
    if (streak == 3 || streak == 5 || streak == 7) {
        std::string reward;
        for (const GameplayEvent& s : m.gameplayEvents())
            if (s.type == GameplayEventType::KillstreakEarned && s.instigator == k && s.serial > e.serial && s.serial <= e.serial + 3) reward = s.text;
        xp(k, txn, "KillStreak" + std::to_string(streak), team, reward.empty() ? std::string() : "Killstreak," + reward);
        std::string spec = K.specialty; std::transform(spec.begin(), spec.end(), spec.begin(), ::toupper);
        stat(k, challengeStatId(("CHALLENGE_PRIME_KILLSTREAK_" + spec + std::to_string(streak) + "_SINGLEMATCH").c_str()));
        stat(k, challengeStatId(("CHALLENGE_PRIME_KILLSTREAK_" + spec + std::to_string(streak) + "_TOTAL").c_str()));
    }
    stat(k, challengeStatId("CHALLENGE_PRIME_KILLSTREAK_HIGHEST"), streak, 1);
    // KillerBuffCheck / KilledDefensiveBuffCheck / KilledOffensiveBuffCheck (assist XP to the debuffer when someone else kills).
    for (const BuffCheckRow& r : kKillerBuffChecks) if (hasBuff(K, r.buff)) { xp(k, txn, r.xpEvent, team); stat(k, r.statId); }
    for (const BuffCheckRow& r : kKilledDefensiveBuffChecks) if (hasBuff(V, r.buff)) { xp(k, txn, r.xpEvent, team); stat(k, r.statId); }
    for (const BuffCheckRow& r : kKilledOffensiveBuffChecks) {
        int by = -1;
        if (!hasBuff(V, r.buff, &by)) continue;
        // The debuffer must be known: the rebuild does not track every debuff's instigator yet [PARTIAL] - unknown = no award.
        if (by == k) { xp(k, txn, r.xpEvent, team); stat(k, r.statId); }
        else if (by >= 0 && r.assistXpEvent) xp(by, nextTxn_++, r.assistXpEvent, team);
    }
    // DamageTypeCheck (whirlwind / shockwave / ram ...).
    for (const DamageCheckRow& r : kDamageTypeChecks) if (dmg == r.damageType) { xp(k, txn, r.xpEvent, team); stat(k, r.statId); }
    // Sentry / mine / roller-mine kills [PROV: in addition to the base Kill].
    if (dmg.find("Sentry") != std::string::npos) xp(k, txn, "SentryKill", team);
    else if (dmg.find("KamikazeMine") != std::string::npos) xp(k, txn, "MineKill", team);
    else if (dmg.find("RollerMine") != std::string::npos) xp(k, txn, "RollerMineKill", team);
    if (K.downed) { xp(k, txn, "KillerDowned", team); stat(k, kKillerDownedStatId); }
    // First kill / first death of the match (once each).
    if (!firstKill_) { firstKill_ = true; xp(k, txn, "FirstKill", team); stat(k, challengeStatId("CHALLENGE_BASIC_FIRST_KILL")); }
    if (!firstDeath_) { firstDeath_ = true; xp(v, nextTxn_++, "FirstDeath", team); stat(v, challengeStatId("CHALLENGE_BASIC_FIRST_DEATH")); }
    if (K.alive && K.health > 0.0f && K.health < 50.0f) xp(k, txn, "KillerLowHealth", team);
    if (e.backstab) { xp(k, txn, "KillBackstab", team); stat(k, challengeStatId("CHALLENGE_SCOUT_BACKSTAB")); }
    if (e.headshot) xp(k, txn, "Headshot", team);
    if (e.distanceUU > kLongRangeUU) xp(k, txn, "LongRange", team);
    // Objective-carrier kills: flag (CTF rules) / bomb (EXT rules); zone (KOTH) / node (DOM) kills.
    const std::string& mode = m.settings().modeTag;
    if (V.carrying == 0 && mode == "CTF") { xp(k, txn, "FlagCarrierKill", team); stat(k, kFlagCarrierStatId); }
    if (V.carrying == 1 && mode == "EXT") xp(k, txn, "BombCarrierKill", team);
    if (V.inActiveZone && mode == "KOTH") xp(k, txn, "ZoneKill", team);
    if (V.inEnemyNode && mode == "DOM") xp(k, txn, "DominationPointKill", team);
    // MultiKill (kills <= 3.0 s apart; exact level 2 / 3 / 4).
    {
        std::vector<float>& rk = recentKills_[k];
        if (!rk.empty() && e.time - rk.back() > 3.0f) rk.clear();
        rk.push_back(e.time);
        const int n = (int)rk.size();
        if (n >= 2 && n <= 4) xp(k, txn, "MultiKill" + std::to_string(n), team);
    }
    // Payback / domination: consecutive kills of the same victim (2 / 3 / 4); killing whoever last killed you; breaking free
    // from an enemy that dominated you.
    {
        const int dom = ++dominate_[{k, v}];
        const int theirs = dominate_[{v, k}];
        dominate_[{v, k}] = 0;
        if (dom >= 2 && dom <= 4) xp(k, txn, "KillDomination" + std::to_string(dom), team);
        auto lk = lastKiller_.find(k);
        if (theirs >= 2) xp(k, txn, "KillPaybackDomination", team);
        else if (lk != lastKiller_.end() && lk->second == v) xp(k, txn, "KillPayback", team);
        lastKiller_[v] = k;
    }
    if (V.killStreak > 2) xp(k, txn, "EndKillStreak", team);
    // AllMelee: a melee kill credits the killer's class melee award + stat.
    if (e.melee && dmg.find("Rammed") == std::string::npos && dmg.find("Whirlwind") == std::string::npos) {
        static const char* cls[4][3] = {{"Scout", "KillScoutMelee", "WEAPON_MELEE_SCOUT_KILLS"}, {"Scientist", "KillScientistMelee", "WEAPON_MELEE_SCIENTIST_KILLS"},
                                        {"Soldier", "KillSoldierMelee", "WEAPON_MELEE_SOLDIER_KILLS"}, {"Leader", "KillLeaderMelee", "WEAPON_MELEE_LEADER_KILLS"}};
        for (auto& c : cls) if (K.specialty == c[0]) { xp(k, txn, c[1], team); stat(k, challengeStatId(c[2])); }
    }
    if (V.level > K.level && K.level > 0) stat(k, challengeStatId("CHALLENGE_BASIC_KILL_HIGHERLEVEL"));
    if (K.spawnTime >= 0.0f) stat(k, challengeStatId("CHALLENGE_BASIC_LIFETIME"), (long)(e.time - K.spawnTime), 1);   // [PROV: seconds alive, match max]
    stat(k, challengeStatId(K.fineAim ? "CHALLENGE_BASIC_KILL_FINEAIM_ON" : "CHALLENGE_BASIC_KILL_FINEAIM_OFF"));
    if (e.killAfterDeath) xp(k, txn, "KillAfterDeath", team);
    if (e.melee && V.hovering) xp(k, txn, "KillHoverMelee", team);
    // TnKillAwardRuleSpecialtySpecific: per-class weapon / form / buff kill stats.
    for (const SpecialtyCheckRow& r : kSpecialtyChecks) {
        if (K.specialty != r.specialty) continue;
        if (r.damageType && dmg != r.damageType) continue;
        if (r.headshot && !e.headshot) continue;
        if (r.killerBuff && !hasBuff(K, r.killerBuff)) continue;
        if (r.killedBuff && !hasBuff(V, r.killedBuff)) continue;
        if (r.killerInVehicle && !K.vehicleForm) continue;
        if (r.killedForm[0] && std::strcmp(vftName(V.vehicleForm ? V.vehicleType : -1), r.killedForm) != 0) continue;
        if (r.killedSpecialty[0] && V.specialty != r.killedSpecialty) continue;
        stat(k, r.statId);
    }
}

} // namespace game
