#include "frontend/Url.h"

#include <cctype>
#include <cstdlib>

namespace frontend {

namespace {
bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
} // namespace

Url Url::parse(const std::string& text) {
    Url u;
    size_t q = text.find('?');
    u.map_ = text.substr(0, q);
    while (q != std::string::npos) {
        size_t next = text.find('?', q + 1);
        std::string part = text.substr(q + 1, next == std::string::npos ? std::string::npos : next - q - 1);
        size_t eq = part.find('=');
        if (eq == std::string::npos) { u.opts_.push_back({part, ""}); u.hasValue_.push_back(false); }
        else { u.opts_.push_back({part.substr(0, eq), part.substr(eq + 1)}); u.hasValue_.push_back(true); }
        q = next;
    }
    return u;
}

std::string Url::toString() const {
    std::string s = map_;
    for (size_t i = 0; i < opts_.size(); ++i) {
        s += "?" + opts_[i].first;
        if (hasValue_[i]) s += "=" + opts_[i].second;
    }
    return s;
}

void Url::setOption(const std::string& key, const std::string& value) {
    for (size_t i = 0; i < opts_.size(); ++i)
        if (iequals(opts_[i].first, key)) { opts_[i].second = value; hasValue_[i] = true; return; }
    opts_.push_back({key, value});
    hasValue_.push_back(true);
}

void Url::addFlag(const std::string& flag) {
    if (hasOption(flag)) return;
    opts_.push_back({flag, ""});
    hasValue_.push_back(false);
}

bool Url::hasOption(const std::string& key) const {
    for (const auto& o : opts_) if (iequals(o.first, key)) return true;
    return false;
}

std::string Url::option(const std::string& key, const std::string& def) const {
    for (const auto& o : opts_) if (iequals(o.first, key)) return o.second;
    return def;
}

int Url::intOption(const std::string& key, int def) const {
    for (const auto& o : opts_) if (iequals(o.first, key)) return std::atoi(o.second.c_str());
    return def;
}

} // namespace frontend
