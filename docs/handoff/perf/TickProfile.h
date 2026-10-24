#pragma once
// WFC_TICKPROF / WFC_TICKLOG (diagnostics): the step profiler's slots, usable from every World translation unit (World.cpp owns the
// accumulators). WFC_TICKLOG=<csv> writes one row per sim step: prefix / remainder / total ms, the counters and every slot's ms.
namespace game {

bool tickProfOn();
void tickProfAdd(int slot, double ms);
double tickProfNowMs();
void tickCount(int which);   // WFC_TICKLOG counters: 0 LOS rays, 1 path searches, 2 bot thinks, 3 spawn tries

// Slots beyond the World.cpp phases (30+): the Escalation director, the campaign runtime, hazards, the presented-frame markers.
enum TickSlot { kTsSurvival = 30, kTsSvKismet = 31, kTsSvMech = 32, kTsCpVm = 33, kTsCpMatinee = 34, kTsCpTouches = 35,
                kTsCpDestructibles = 36, kTsCpInteracts = 37, kTsCpSentriesMines = 38, kTsCpFactories = 39, kTsHazards = 40,
                kTsFillMarkers = 41, kTsPrefix = 42 };

struct TickScope {
    int s; double t0;
    explicit TickScope(int slot) : s(slot), t0(tickProfOn() ? tickProfNowMs() : 0.0) {}
    ~TickScope() { if (tickProfOn()) tickProfAdd(s, tickProfNowMs() - t0); }
};

} // namespace game
