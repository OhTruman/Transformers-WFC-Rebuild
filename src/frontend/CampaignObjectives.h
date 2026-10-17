// Campaign objectives (docs/handoff/CAMPAIGN_HUD_PLAN.md E1; RE CAMPAIGN_HUD_OBJECTIVES_RE.md section 2).
//
// The original: the persistent level's TnWorldInfo._ObjectiveBank is an ordered list of keys, each localized with
// Localize("Objectives", key, "TransGame"); SeqAct_TnSetCurrentObjective(Description = key) makes one current (the bank
// index replicates); an empty description means no objective; an unknown text is a logged error. The client treats the
// previous one as complete, so every bank entry before the current index counts as done (LastCompletedObjective =
// Current - 1). Hud_GFX shows a new objective through _global.AddObjective (the HUD side is HudController::setObjective);
// _global.CompleteObjective is not defined in the movie, so completion has no visual. The pause menu reads
// <CurrentGame:ActiveObjectives> (the current text).
//
// Campaign only (GameFlow::campaign()); versus never sets a bank. Until AssetTools' campaign map export carries the bank
// (plan question C1) only tests set one.
#pragma once
#include <string>
#include <vector>

#include "frontend/Catalog.h"
#include "frontend/FlowTrace.h"

namespace frontend {

class CampaignObjectives {
public:
    void setBank(std::vector<std::string> keys) { bank_ = std::move(keys); current_ = -1; text_.clear(); }
    void clear() { bank_.clear(); current_ = -1; text_.clear(); }
    const std::vector<std::string>& bank() const { return bank_; }
    int current() const { return current_; }
    const std::string& activeText() const { return text_; }

    // SeqAct_TnSetCurrentObjective. "" clears the objective; a key outside the bank is refused (false), as the original
    // logs it and keeps the current one.
    bool setCurrent(const std::string& key, const Catalog& cat) {
        if (key.empty()) {
            current_ = -1;
            text_.clear();
            FlowTrace::emit("campaign.objective", {{"index", "-1"}, {"text", ""}});
            return true;
        }
        for (size_t i = 0; i < bank_.size(); ++i) {
            if (bank_[i] != key) continue;
            current_ = (int)i;
            text_ = cat.localize("TransGame", "Objectives", key);
            FlowTrace::emit("campaign.objective", {{"index", std::to_string(i)}, {"key", key}, {"text", text_}});
            return true;
        }
        FlowTrace::emit("campaign.objectiveError", {{"key", key}, {"why", "not in the objective bank"}});
        return false;
    }

    // Every bank entry before the current one (the original's completed list), localized.
    std::vector<std::string> completedTexts(const Catalog& cat) const {
        std::vector<std::string> out;
        for (int i = 0; i < current_ && i < (int)bank_.size(); ++i) out.push_back(cat.localize("TransGame", "Objectives", bank_[(size_t)i]));
        return out;
    }

private:
    std::vector<std::string> bank_;
    int current_ = -1;
    std::string text_;
};

} // namespace frontend
