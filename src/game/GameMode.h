// Clean-room reconstruction — game mode/rules (scaffold).
// Owns match state, spawn selection, win conditions. Placeholder logic for now.
#pragma once
#include "game/Team.h"

namespace game {

class World;

class GameMode {
public:
    enum class Phase { Warmup, Playing, PostMatch };

    virtual ~GameMode() = default;
    virtual const char* name() const { return "FreeForAll (placeholder)"; }

    virtual void begin(World& world) { (void)world; phase_ = Phase::Playing; }
    virtual void tick(World& world, float dt) { (void)world; timeInPhase_ += dt; }

    Phase phase() const { return phase_; }

protected:
    Phase phase_ = Phase::Warmup;
    float timeInPhase_ = 0.0f;
};

} // namespace game
