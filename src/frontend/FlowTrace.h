// Clean-room reconstruction — machine-readable frontend/game-flow trace for validation (Experimental lane).
// Every flow transition is logged as "FLOW ..." in wfc.log and, when WFC_FLOWLOG=<path> is set, appended to
// that file as one JSON object per line: {"t":seconds,"seq":n,"ev":"<event>", ...fields}.
// Event names are stable; see docs/FRONTEND.md "Validation hooks".
#pragma once
#include <initializer_list>
#include <string>
#include <utility>

namespace frontend {

class FlowTrace {
public:
    using Field = std::pair<const char*, std::string>;
    static void emit(const char* ev, std::initializer_list<Field> fields);
    static void setClock(double seconds);
    static std::string num(double v);
    static std::string boolean(bool v) { return v ? "true" : "false"; }
};

} // namespace frontend
