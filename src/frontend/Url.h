// Clean-room reconstruction — UE3 travel URL ("Map?Key=Value?Flag").
// WFC moves between frontend, party lobby, game lobby and match by URL (TnGame.ClientTravelToMap /
// ServerTravelToMap); the match reads its settings back from the URL (GameInfo.InitGame GetIntOption etc.).
// Source: RE-Workspace notes/MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md sections 1.5, 2.3, 3.1 [CONFIRMED script].
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace frontend {

class Url {
public:
    Url() = default;
    explicit Url(const std::string& map) : map_(map) {}

    static Url parse(const std::string& text);
    std::string toString() const;

    const std::string& map() const { return map_; }
    void setMap(const std::string& m) { map_ = m; }

    // "?Key=Value" (replaces an existing key, case-insensitive like UE3 HasOption/GetOption).
    void setOption(const std::string& key, const std::string& value);
    // "?Flag" without a value (e.g. "?listen").
    void addFlag(const std::string& flag);
    bool hasOption(const std::string& key) const;
    std::string option(const std::string& key, const std::string& def = "") const;
    // UE3 GameInfo.GetIntOption: atoi of the value, `def` when absent.
    int intOption(const std::string& key, int def) const;

    const std::vector<std::pair<std::string, std::string>>& options() const { return opts_; }

private:
    std::string map_;
    std::vector<std::pair<std::string, std::string>> opts_;   // flags have value "" and no '='
    std::vector<bool> hasValue_;
};

} // namespace frontend
