// Clean-room reconstruction — ability with cooldown (scaffold).
#pragma once

namespace game {

struct Ability {
    const char* name = "PlaceholderAbility";
    float cooldownTime = 5.0f;
    float remaining = 0.0f;

    void tick(float dt) { if (remaining > 0) remaining -= dt; }
    bool ready() const { return remaining <= 0.0f; }
    bool tryActivate() { if (!ready()) return false; remaining = cooldownTime; return true; }
};

} // namespace game
