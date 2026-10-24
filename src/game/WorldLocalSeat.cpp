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

int World::addLocalSeat(int pad, const CharacterSelection& sel, bool selected) {
    if (seat2_ >= 0 || !matchActive_) return seat2_;
    RemoteSeat r;
    r.name = "Player 2";
    const int me = localPlayer_;
    r.team = me >= 0 && (size_t)me < match_.players().size() ? match_.players()[(size_t)me].team : 0;   // the first player's side
    r.member = "local:pad" + std::to_string(pad);
    r.selection = sel; r.selected = selected;
    seat2_ = addRemoteParticipant(r, true);
    seat2Pad_ = pad;
    seat2Ctl_ = PlayerController{};
    seat2Ctl_.setWireSeat(true);
    seat2In_ = platform::InputFrame{};
    LOG_INFO("local seat 2: pad %d joined as p%d (team %d)", pad, seat2_, r.team);
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
void World::seat2QueueCommand() {
    if (seat2_ < 0) return;
    Character* pc = participantPawnMutable(seat2_);
    if (!pc) return;
    const bool dead = !match_.players()[(size_t)seat2_].alive;
    applyRemoteCommand(seat2_, encodeSeatCommand(seat2Ctl_, pc, seat2_, dead));
    static const bool bot = std::getenv("WFC_SEAT2BOT") != nullptr;
    if (bot && simTick_ % 600 == 0) {
        const core::Vec3 p = pc->actorLocation();
        const MatchPlayer& mp = match_.players()[(size_t)seat2_];
        LOG_INFO("SEAT2 tick %lld p%d at %.1f %.1f %.1f yaw %.2f alive %d kills %d deaths %d", (long long)simTick_, seat2_, p.x, p.y, p.z,
                 seat2Ctl_.camYaw(), (int)mp.alive, mp.kills, mp.deaths);
    }
}

int World::localSeatCount() const { return seat2_ >= 0 ? 2 : 1; }
int World::localSeatPlayer(int seat) const { return seat == 1 ? seat2_ : localPlayer_; }
void World::seatCamera(int seat, render::Camera& cam) const {
    if (seat == 1 && seat2_ >= 0) seat2Ctl_.updateCamera(cam);
    else player_.controller().updateCamera(cam);
}

} // namespace game
