// [Systems] Which controller drives which local seat - the platform window's pad routing as a pure function (testable without
// controllers). Seat 1 = the keyboard / mouse + ONE pad; seat 2 (split screen) = one other pad, joined by its Start press.
#pragma once

namespace platform {

constexpr int kMaxPads = 4;

struct PadPoll {   // one poll of every XInput slot
    bool connected[kMaxPads] = {};
    bool startDown[kMaxPads] = {};
    bool startEdge[kMaxPads] = {};   // Start went down this poll
    int lastActive = -1;             // the last slot with any button / trigger activity (-1: none yet)
};

struct SeatPads {
    int seat1 = 0, seat2 = -1;   // pad slots (-1: none - seat 1 then plays on the keyboard / mouse only)
};

// Outside split screen seat 1 reads the pad in use (any slot: Steam Input / DS4Windows / some adapters put a lone controller on
// slot 1+). With split screen: while seat 1 has no pad plugged in, the pad in use becomes seat 1's - the player's only controller
// is never a second player; a Start press on ANOTHER pad joins seat 2, unless seat 1's pad shows Start in the same poll (a mirrored
// virtual device of the same controller). Returns true when seat 2 joined this poll.
inline bool routeSeatPads(bool split, const PadPoll& p, SeatPads& s) {
    auto plugged = [&p](int i) { return i >= 0 && i < kMaxPads && p.connected[i]; };
    if (!split) {
        s.seat1 = p.lastActive >= 0 ? p.lastActive : 0;
        return false;
    }
    if (!plugged(s.seat1) && s.seat2 < 0 && plugged(p.lastActive)) s.seat1 = p.lastActive;
    if (s.seat2 >= 0 || !plugged(s.seat1)) return false;   // seat 2 taken / seat 1 first
    for (int i = 0; i < kMaxPads; ++i) {
        if (!p.startEdge[i] || i == s.seat1) continue;
        if (p.startDown[s.seat1]) continue;   // mirrors seat 1's own Start: the same controller twice
        s.seat2 = i;
        return true;
    }
    return false;
}

}  // namespace platform
