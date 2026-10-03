// Clean-room reconstruction — TnSegmentedHealth (TR_Health_p.SharedHealth) [CONF RE MILESTONE05_GAMEPLAY_UNKNOWNS §6]:
// segments [175, 125, 125, 125] -> HealthMax 550; Overshield 550 (Health may reach HealthMax + 550).
// AdjustDamage: while overshield health > 0 the damage comes off Health directly (the overshield part first);
// otherwise normal segmented damage. HealDamage dispatches on the heal type (SHT_*).
// Segment regeneration rules were not read [UNKNOWN]: no regeneration is applied.
#pragma once

namespace game {

struct Health {
    static constexpr int kSegmentCount = 4;
    static constexpr float kSegments[kSegmentCount] = {175.0f, 125.0f, 125.0f, 125.0f};
    static constexpr float kOvershield = 550.0f;

    float current = 550.0f;
    float max = 550.0f;            // HealthMax = sum of segments

    bool isDead() const { return current <= 0.0f; }
    float overshield() const { return current > max ? current - max : 0.0f; }
    float normalizedOverShield() const { return overshield() / kOvershield; }
    // Index of the segment the current health lies in (0 = bottom); kSegmentCount = overshield.
    int activeSegment() const {
        if (current > max) return kSegmentCount;
        float top = 0.0f;
        for (int i = 0; i < kSegmentCount; ++i) { top += kSegments[i]; if (current <= top) return i; }
        return kSegmentCount - 1;
    }
    float segmentTop(int i) const { float top = 0.0f; for (int k = 0; k <= i && k < kSegmentCount; ++k) top += kSegments[k]; return top; }
    // Returns the health actually removed.
    float applyDamage(float amount) {
        if (amount <= 0.0f || current <= 0.0f) return 0.0f;
        float before = current;
        current -= amount;
        if (current < 0.0f) current = 0.0f;
        return before - current;
    }
    enum class HealType { AddHealthToAll = 0, AddHealthToSegment = 1, AddSegment = 2, AddAllSegments = 3, AddOverShield = 4 };
    void heal(HealType t, float amount) {
        switch (t) {
            case HealType::AddHealthToAll: current = current + amount > max ? (current > max ? current : max) : current + amount; break;
            case HealType::AddHealthToSegment: { float top = segmentTop(activeSegment()); if (current < top) current = current + amount > top ? top : current + amount; break; }
            case HealType::AddSegment: { int s = activeSegment(); if (s < kSegmentCount) current = segmentTop(s < kSegmentCount - 1 && current >= segmentTop(s) ? s + 1 : s); break; }
            case HealType::AddAllSegments: if (current < max) current = max; break;       // Health = max(Health, HealthMax)
            case HealType::AddOverShield: current = max + kOvershield; break;
        }
    }
    void reset() { current = max; }
};

} // namespace game
