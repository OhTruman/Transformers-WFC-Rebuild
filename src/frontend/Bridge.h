// Clean-room reconstruction — value returned by an engine binding to ActionScript (ExternalInterface.call result).
// UnrealScript binding functions return bool / int / float / string (or nothing).
#pragma once
#include <string>
#include <vector>

namespace frontend {

struct BridgeValue {
    enum class Kind { Void, Bool, Number, String, Array } kind = Kind::Void;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<BridgeValue> items;   // Array (collection reads)

    BridgeValue() = default;
    BridgeValue(bool v) : kind(Kind::Bool), b(v) {}
    BridgeValue(int v) : kind(Kind::Number), n(v) {}
    BridgeValue(double v) : kind(Kind::Number), n(v) {}
    BridgeValue(const char* v) : kind(Kind::String), s(v) {}
    BridgeValue(const std::string& v) : kind(Kind::String), s(v) {}

    bool truthy() const { return kind == Kind::Bool ? b : kind == Kind::Number ? n != 0 : kind == Kind::String ? !s.empty() : false; }
    std::string str() const;
};

} // namespace frontend
