// Clean-room reconstruction — TnSegmentedHealth initialised from a TnSegmentedHealthBlueprint.
// Default: TR_Health_p.SharedHealth [CONF RE MILESTONE05_GAMEPLAY_UNKNOWNS §6] — segments [175, 125, 125, 125] ->
// HealthMax 550; Overshield 550 (Health may reach HealthMax + Overshield). In versus play TnSpecialty.Apply
// re-initialises it from TR_Health_p.Health_<Class> (ApplySpecialtyBuffs, Default__TnGame true) [CONF script].
// AdjustDamage: while overshield health > 0 the damage comes off Health directly (the overshield part first);
// otherwise normal segmented damage. HealDamage dispatches on the heal type (SHT_*).
// Regeneration [CONF RE MILESTONE05_PLAYTEST_RE §9]: 20 HP/s after 2.0 s without damage, up to the top of the current
// segment; the robot blueprint's HealthRegenParameters apply in both forms (the truck's 12 / 7 s are unread).
#pragma once
#include <vector>

namespace game {

struct Health {
    static constexpr int kMaxSegments = 8;
    float segments[kMaxSegments] = {175.0f, 125.0f, 125.0f, 125.0f};
    int segmentCount = 4;
    float overshieldMax = 550.0f;

    float current = 550.0f;
    float max = 550.0f;            // HealthMax = sum of segments
    float sinceDamage = 1e9f;      // seconds since the last damage taken
    static constexpr float kRegenDelay = 2.0f, kRegenRate = 20.0f;

    // TnSegmentedHealth.Initialize(Blueprint): segments + overshield, Health = HealthMax.
    void initialize(const std::vector<float>& seg, float overshield) {
        segmentCount = 0; max = 0.0f;
        for (float s : seg) { if (segmentCount >= kMaxSegments) break; segments[segmentCount++] = s; max += s; }
        if (segmentCount == 0) { segments[0] = 550.0f; segmentCount = 1; max = 550.0f; }
        overshieldMax = overshield;
        reset();
    }
    bool isDead() const { return current <= 0.0f; }
    float overshield() const { return current > max ? current - max : 0.0f; }
    float normalizedOverShield() const { return overshieldMax > 0.0f ? overshield() / overshieldMax : 0.0f; }
    // Index of the segment the current health lies in (0 = bottom); segmentCount = overshield.
    int activeSegment() const {
        if (current > max) return segmentCount;
        float top = 0.0f;
        for (int i = 0; i < segmentCount; ++i) { top += segments[i]; if (current <= top) return i; }
        return segmentCount - 1;
    }
    float segmentTop(int i) const { float top = 0.0f; for (int k = 0; k <= i && k < segmentCount; ++k) top += segments[k]; return top; }
    // Returns the health actually removed.
    float applyDamage(float amount) {
        if (amount <= 0.0f || current <= 0.0f) return 0.0f;
        float before = current;
        sinceDamage = 0.0f;
        current -= amount;
        if (current < 0.0f) current = 0.0f;
        return before - current;
    }
    enum class HealType { AddHealthToAll = 0, AddHealthToSegment = 1, AddSegment = 2, AddAllSegments = 3, AddOverShield = 4 };
    void heal(HealType t, float amount) {
        switch (t) {
            case HealType::AddHealthToAll: current = current + amount > max ? (current > max ? current : max) : current + amount; break;
            case HealType::AddHealthToSegment: { float top = segmentTop(activeSegment()); if (current < top) current = current + amount > top ? top : current + amount; break; }
            case HealType::AddSegment: { int s = activeSegment(); if (s < segmentCount) current = segmentTop(s < segmentCount - 1 && current >= segmentTop(s) ? s + 1 : s); break; }
            case HealType::AddAllSegments: if (current < max) current = max; break;       // Health = max(Health, HealthMax)
            case HealType::AddOverShield: current = max + overshieldMax; break;
        }
    }
    void tickRegen(float dt, float rateScale = 1.0f) {
        sinceDamage += dt;
        if (current <= 0.0f || current >= max || sinceDamage < kRegenDelay) return;
        float top = segmentTop(activeSegment());
        const float rate = kRegenRate * rateScale;
        if (current < top) current = current + rate * dt > top ? top : current + rate * dt;
    }
    void reset() { current = max; sinceDamage = 1e9f; }
};

} // namespace game
