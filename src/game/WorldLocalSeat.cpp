// Split screen, Gameplay part 1 (docs/handoff/GAMEPLAY_SPLITSCREEN_PLAN.md): a second local human seat on the remote-seat path.
// Seat 2 is a participant of kind Remote whose RemoteCommand is built locally every step from its own PlayerController (wire seat:
// it never writes the pawn directly), fed by pad 2's InputFrame; the seat path (applyRemoteNow / applySeat / tickRemoteSeats) then
// simulates it as it simulates an online peer. WFC_SEAT2BOT=1 (test only): seat 2 joins at the match start and a scripted input
// (forward + a slow turn, fire bursts, a jump every 5 s) drives it.
#include "game/World.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>

namespace game {

int World::addLocalSeat(int pad, const CharacterSelection& sel, bool selected, int matchSeat) {
    if (seat2_ >= 0 || !matchActive_) return seat2_;
    if (matchSeat >= 0) {   // online: the launch made the match seat a Remote participant; this peer feeds it from pad 2
        if ((size_t)matchSeat >= match_.players().size() || match_.players()[(size_t)matchSeat].kind != ParticipantKind::Remote) {
            LOG_WARN("local seat 2: match seat %d is not a Remote participant - not bound", matchSeat);
            return -1;
        }
        (void)sel; (void)selected;   // the launch record carries its selection (every peer applies it)
        seat2_ = matchSeat;
        seat2Pad_ = pad;
        seat2Ctl_ = PlayerController{};
        seat2Ctl_.setWireSeat(true);
        seat2In_ = platform::InputFrame{};
        LOG_INFO("local seat 2: pad %d bound to match player p%d (online: its commands go through the lockstep)", pad, seat2_);
        return seat2_;
    }
    RemoteSeat r;
    r.name = "Player 2";
    const int me = localPlayer_;
    r.team = me >= 0 && (size_t)me < match_.players().size() ? match_.players()[(size_t)me].team : 0;   // the first player's side
    r.member = "local:pad" + std::to_string(pad);
    r.selection = sel; r.selected = selected;
    // Seat 2 takes a bot slot on its team (campaign: the last squad AI, as co-op; versus / Escalation: the team's last bot) through
    // the late-join handover (Bot -> Remote at the next tick); with no bot on the team it joins as an extra participant.
    // Campaign: it stays in cp_.squad, so it respawns beside its buddy and counts as a squad member.
    int slot = -1;
    if (cp_.active) {
        for (size_t k = cp_.squad.size(); k-- > 0;)
            if (match_.players()[(size_t)cp_.squad[k]].kind == ParticipantKind::Bot) { slot = cp_.squad[k]; break; }
    } else for (size_t i = match_.players().size(); i-- > 0;)
        if ((int)i != me && match_.players()[i].kind == ParticipantKind::Bot && match_.players()[i].team == r.team) { slot = (int)i; break; }
    if (slot >= 0) {
        SlotSwap q; q.matchPlayer = slot; q.toBot = false; q.member = r.member;
        if (!applySlotSwap(q)) slot = -1;
        else {
            match_.playerMutable(slot).name = r.name;
            if (selected && !cp_.active) match_.selectCharacter(slot, sel);   // campaign keeps the slot's character (co-op chapters)
        }
    }
    seat2_ = slot >= 0 ? slot : addRemoteParticipant(r, true);
    seat2Pad_ = pad;
    seat2Ctl_ = PlayerController{};
    seat2Ctl_.setWireSeat(true);
    seat2In_ = platform::InputFrame{};
    LOG_INFO("local seat 2: pad %d joined as p%d (team %d, %s)", pad, seat2_, r.team, slot >= 0 ? "took a bot slot" : "an extra participant");
    return seat2_;
}

void World::removeLocalSeat() {
    if (seat2_ < 0) return;
    SlotSwap s; s.matchPlayer = seat2_; s.toBot = true;   // leave-to-bot, as an online seat that leaves
    applySlotSwap(s);
    LOG_INFO("local seat 2: p%d left (a bot takes the slot)", seat2_);
    seat2_ = -1; seat2Pad_ = -1;
}

void World::setSeat2Input(const platform::InputFrame& in) { seat2In_ = in; }

// Per frame (handleInput): seat 2's controller takes its input (camera, intent, buttons) for its pawn.
void World::seat2HandleInput(float dt) {
    if (seat2_ < 0) return;
    static const bool bot = [] { const char* e = std::getenv("WFC_SEAT2BOT"); return e && std::atoi(e) != 0; }();
    if (bot) {   // the scripted test input
        platform::InputFrame f;
        const double t = (double)simTick_ / 60.0;
        f.down[(int)platform::Button::Forward] = true;
        f.mouseDX = 2.0f;                                       // a slow turn
        f.down[(int)platform::Button::Fire] = std::fmod(t, 4.0) < 1.5;
        if (std::fmod(t, 5.0) < 1.0 / 60.0) { f.pressed[(int)platform::Button::Jump] = true; f.down[(int)platform::Button::Jump] = true; }
        seat2In_ = f;
    }
    Character* pc = participantPawnMutable(seat2_);
    seat2Ctl_.possess(pc);
    seat2Ctl_.setWorlds(collision_.valid() ? &collision_ : nullptr, weaponCollision_.valid() ? &weaponCollision_ : nullptr);
    if (pc) seat2Ctl_.handleInput(cp_.active && cp_.cinematicMode ? platform::InputFrame{} : seat2In_, dt);
}

// Per step (tickPrefix, before the queued remote commands apply): seat 2's command for the step about to run.
RemoteCommand World::encodeSeat2Command() {
    Character* pc = seat2_ >= 0 ? participantPawnMutable(seat2_) : nullptr;
    if (!pc) { RemoteCommand c; c.tick = (uint32_t)(simTick_ + 1); return c; }   // no pawn (yet): an empty command, as encodeLocalCommand's dead path
    const bool dead = !match_.players()[(size_t)seat2_].alive;
    return encodeSeatCommand(seat2Ctl_, pc, seat2_, dead);
}

void World::seat2QueueCommand() {
    if (seat2_ < 0) return;
    if (onlineSeats_) return;   // online: the driver submits encodeSeat2Command() and every peer applies it from the lockstep
    Character* pc = participantPawnMutable(seat2_);
    if (!pc) return;
    const bool dead = !match_.players()[(size_t)seat2_].alive;
    applyRemoteCommand(seat2_, encodeSeatCommand(seat2Ctl_, pc, seat2_, dead));
    static const bool bot = std::getenv("WFC_SEAT2BOT") != nullptr;
    if (bot && simTick_ % 600 == 0) {
        const core::Vec3 p = pc->actorLocation();
        const MatchPlayer& mp = match_.players()[(size_t)seat2_];
        const PresentedFrame::Seat* hs = presented_.seats.size() > 1 ? &presented_.seats[1] : nullptr;
        LOG_INFO("SEAT2 tick %lld p%d at %.1f %.1f %.1f yaw %.2f alive %d kills %d deaths %d | hud %s hp %.0f/%.0f clip %d/%d markers %zu taken %zu caused %zu",
                 (long long)simTick_, seat2_, p.x, p.y, p.z, seat2Ctl_.camYaw(), (int)mp.alive, mp.kills, mp.deaths, hs ? hs->weaponId.c_str() : "-",
                 hs ? hs->health : 0.0f, hs ? hs->healthMax : 0.0f, hs ? hs->clip : 0, hs ? hs->mag : 0, hs ? hs->markers.size() : (size_t)0,
                 hs ? hs->damageTaken.size() : (size_t)0, hs ? hs->damageCaused.size() : (size_t)0);
    }
}

// Split screen step 2: the per-seat HUD state (PresentedFrame::seats). seats[0] mirrors the top-level local fields; seats[1] is
// seat 2's own pawn / controller / damage queues. Seat 2 is on seat 1's team, so the objective and pawn markers (relations by team)
// are seat 1's, with the enemy name label re-checked for line of sight from seat 2's eye.
void World::fillSeats(PresentedFrame& p) {
    std::vector<PresentedFrame::DamageTaken> taken2; std::vector<PresentedFrame::DamageCaused> caused2;   // seat 2's unconsumed entries
    if (p.seats.size() > 1) { taken2.swap(p.seats[1].damageTaken); caused2.swap(p.seats[1].damageCaused); }
    p.seats.clear();
    PresentedFrame::Seat s0;
    s0.player = localPlayer_; s0.dead = localPlayerDead();
    s0.health = p.localHealth; s0.healthMax = p.localHealthMax; s0.segmentTops = p.localSegmentTops;
    s0.mag = p.localMag; s0.reserveMax = p.localReserveMax; s0.clip = p.hud.clipAmmo; s0.reserve = p.hud.reserveAmmo;
    s0.weaponId = p.localWeaponId; s0.chassis = p.localChassis;
    s0.aim = p.aim; s0.hud2 = p.hud2; s0.markers = p.markers;
    s0.damageTaken = p.damageTaken; s0.damageCaused = p.damageCaused;
    p.seats.push_back(std::move(s0));
    if (seat2_ < 0 || (size_t)seat2_ >= p.players.size()) return;
    PresentedFrame::Seat s;
    s.player = seat2_;
    const MatchPlayer& mp = p.players[(size_t)seat2_];
    s.dead = !mp.alive;
    s.chassis = mp.chassis;
    Character* pcm = participantPawnMutable(seat2_);
    seat2Ctl_.possess(pcm);   // the current pawn (respawns; no input frame yet)
    const Character* pc = pcm;
    if (pc) {
        s.health = pc->health().current; s.healthMax = pc->health().max;
        for (int i = 0; i < pc->health().segmentCount; ++i) s.segmentTops.push_back(pc->health().segmentTop(i));
        const Weapon* hw = &pc->weapon();
        if (pc->moveForm() == Form::Vehicle && pc->vehicleWeapon()) hw = pc->vehicleWeapon();   // as hudState: the held weapon
        s.mag = hw->magSize; s.reserveMax = hw->reserveMax; s.clip = hw->ammo; s.reserve = hw->reserve;
        s.weaponId = pc->weapon().def ? pc->weapon().def->id : "";
        s.aim = seat2Ctl_.hudAimState();
        PresentedFrame::HudObservers& o = s.hud2;
        o.attackingTeam = p.hud2.attackingTeam;
        for (const Character::AbilitySlot& a : pc->abilities_) o.abilities.push_back({a.id, a.cooldown, a.cooldownTime, a.pendingCooldown, a.implemented});
        if (const Weapon* gb = grenadeBag(*pc)) { o.grenade.ammo = gb->ammo; o.grenade.type = gb->def ? gb->def->id : ""; }
        for (const Projectile& pr : projectiles_) o.grenade.activeCount += pr.instigator == seat2_ && pr.grenade;
        o.weapon.jammed = pc->jammedRemain_ > 0.0f; o.weapon.spread = pc->effectiveSpread();
        if (pc->cloakRemain_ > 0.0f) o.buffs.push_back({"TnBuffCloak", pc->cloakRemain_});
        if (pc->warcryRemain_ > 0.0f) o.buffs.push_back({"TnBuffWarcry", pc->warcryRemain_});
    }
    const core::Vec3 eye = pc ? seat2Ctl_.cameraPos() : core::Vec3{0, 0, 0};
    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);
    for (PresentedFrame::Marker m : p.markers) {
        if (m.player == seat2_) continue;   // never its own pawn tag
        float th;
        if (pc && m.relation == 1 && !m.label.empty() && m.key.compare(0, 5, "pawn:") == 0 && line && line->segmentHit(eye, m.base, th)) m.label.clear();
        s.markers.push_back(std::move(m));
    }
    if (localPlayer_ >= 0 && (size_t)localPlayer_ < p.present.size() && p.present[(size_t)localPlayer_]) {   // seat 1 is an ally tag for seat 2
        for (const PresentedFrame::Marker& m : p.markers) if (m.player == localPlayer_) goto tagged;
        {
            PresentedFrame::Marker m;
            m.key = "pawn:" + std::to_string(localPlayer_); m.type = "TnObjectiveMarkerTypeTransformerVersus";
            m.setup = "AllyMarkerSetup"; m.relation = 0; m.player = localPlayer_; m.owner = 100 + localPlayer_;
            const Character& c = player_.pawn();
            m.base = c.actorLocation(); m.labelZ = c.cylinderHalfHeight(c.moveForm()) * 0.5f;
            m.health = c.health().max > 0.0f ? c.health().current / c.health().max : 0.0f;
            m.label = p.players[(size_t)localPlayer_].name;
            s.markers.push_back(std::move(m));
        }
        tagged:;
    }
    taken2.insert(taken2.end(), seat2Taken_.begin(), seat2Taken_.end()); seat2Taken_.clear();
    caused2.insert(caused2.end(), seat2Caused_.begin(), seat2Caused_.end()); seat2Caused_.clear();
    if (taken2.size() > 1024) taken2.erase(taken2.begin(), taken2.end() - 1024L);
    if (caused2.size() > 1024) caused2.erase(caused2.begin(), caused2.end() - 1024L);
    s.damageTaken.swap(taken2); s.damageCaused.swap(caused2);
    p.seats.push_back(std::move(s));
}

// Damage hooks for seat 2's hit-direction indicator and hit markers (World::applyDamage, alongside the local queues).
void World::seat2NoteDamage(int victim, int instigator, float applied, bool killed) {
    if (seat2_ < 0 || applied <= 0.0f || instigator == victim) return;
    if (victim == seat2_) {
        const Character* pc = participantPawn(seat2_);
        const core::Vec3 from = match_.playerLocation(instigator);
        const core::Vec3 d = from - (pc ? pc->position() : from);
        seat2Taken_.push_back({std::remainder(std::atan2(-d.x, -d.z) - seat2Ctl_.camYaw(), 6.2831853f), applied, instigator, from});
        if (seat2Taken_.size() > 1024) seat2Taken_.erase(seat2Taken_.begin());
    }
    if (instigator == seat2_) {
        seat2Caused_.push_back({victim, applied, killed});
        if (seat2Caused_.size() > 1024) seat2Caused_.erase(seat2Caused_.begin());
    }
}

int World::localSeatCount() const { return seat2_ >= 0 ? 2 : 1; }
int World::localSeatPlayer(int seat) const { return seat == 1 ? seat2_ : localPlayer_; }
void World::seatCamera(int seat, render::Camera& cam) const {
    if (seat == 1 && seat2_ >= 0 && seat2Ctl_.pawn()) seat2Ctl_.updateCamera(cam);
    else player_.controller().updateCamera(cam);
}

} // namespace game
