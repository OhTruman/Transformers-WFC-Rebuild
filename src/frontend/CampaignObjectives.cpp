#include "frontend/CampaignObjectives.h"

#include <fstream>
#include <sstream>

#include "assets/Json.h"
#include "frontend/OpenPath.h"

namespace frontend {

namespace {
std::string trimmed(std::string s) {   // one A4 bank key carries a trailing space in the data (AssetTools)
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    return s;
}
}

std::string CampaignObjectives::manifestFor(const std::string& manifestRoot, const std::string& map) {
    return manifestRoot + "/next_map/" + map + "_objectives.json";
}

bool CampaignObjectives::loadManifest(const std::string& path) {
    std::ifstream f(openPath(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    assets::Json j;
    if (!assets::Json::parse(ss.str(), j)) return false;
    // bank[] in index order = TnWorldInfo._ObjectiveBank
    std::vector<std::pair<int, std::string>> keyed;
    const assets::Json& bank = j["bank"];
    for (size_t i = 0; i < bank.size(); ++i) keyed.push_back({bank[i]["index"].asInt((int)i), trimmed(bank[i]["key"].asString())});
    std::sort(keyed.begin(), keyed.end());
    std::vector<std::string> keys;
    for (const auto& k : keyed) keys.push_back(k.second);
    setBank(std::move(keys));
    // setters[]: each SeqAct_TnSetCurrentObjective by its Kismet object path -> its key ("" = clears the objective)
    setters_.clear();
    const assets::Json& setters = j["setters"];
    for (size_t i = 0; i < setters.size(); ++i) {
        const assets::Json& s = setters[i];
        const std::string action = s["action"].asString();
        if (action.empty()) continue;
        setters_[action] = s["clears_objective"].asBool() ? std::string() : trimmed(s["key"].asString());
    }
    FlowTrace::emit("campaign.objectiveBank", {{"file", path}, {"bank", std::to_string(bank_.size())}, {"setters", std::to_string(setters_.size())}});
    return true;
}

bool CampaignObjectives::fireSetter(const std::string& action, const Catalog& cat) {
    auto it = setters_.find(action);
    if (it == setters_.end()) {
        FlowTrace::emit("campaign.objectiveError", {{"setter", action}, {"why", "unknown SeqAct_TnSetCurrentObjective"}});
        return false;
    }
    return setCurrent(it->second, cat);   // an out-of-bank key (D1's D1_ORB_OBJ08) is refused there, as the original
}

} // namespace frontend
