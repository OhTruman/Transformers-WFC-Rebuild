// Clean-room reconstruction — health/damage (scaffold).
#pragma once

namespace game {

struct Health {
    float current = 100.0f;
    float max = 100.0f;

    bool isDead() const { return current <= 0.0f; }
    void applyDamage(float amount) { current -= amount; if (current < 0) current = 0; }
    void heal(float amount) { current += amount; if (current > max) current = max; }
    void reset() { current = max; }
};

} // namespace game
