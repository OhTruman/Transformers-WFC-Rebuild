// Clean-room reconstruction — the UE3 data stores the WFC movies read through HmDataStoreActionScriptBinding
// ("DataStores.ReadValue('<CurrentGame:SelectedMapID>')", "<TnMenuItems:Maps>" collections, change callbacks).
// Values come from the lobby / match state in GameFlow and the manifest catalog; nothing is invented: a markup
// with no rebuild source is reported (flow event datastore.unhandled) and reads as empty.
// Sources: RE MILESTONE05 sections 2-6, notes/data/frontend_gfx_callbacks.json (markups per movie).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "frontend/Bridge.h"

namespace frontend {

class GameFlow;
class Catalog;

class DataStores {
public:
    DataStores(GameFlow& flow, const Catalog& cat) : flow_(flow), cat_(cat) {}

    // Dispatch of "DataStores.<Fn>" bridge calls.
    BridgeValue call(const std::string& fn, const std::vector<std::string>& args, const std::string& movie = "");
    void forgetMovie(const std::string& movie);   // movie closed: drop its callbacks
    // Current value of a markup as the binding returns it ("" when unknown); `known` tells whether it has a source.
    std::string read(const std::string& markup, bool* known = nullptr);

    // RegisterValueChangedCallback: (markup, "path.fn") pairs; poll() reports the ones whose value changed.
    struct Change { std::string movie, markup, callback, value; };
    std::vector<Change> poll();

private:
    struct Collection { std::vector<std::string> columns; std::vector<std::vector<std::string>> rows; std::vector<bool> enabled; };
    bool collection(const std::string& markup, Collection& out);
    std::string playerName() const;

    GameFlow& flow_;
    const Catalog& cat_;
    struct Reg { std::string movie, markup, callback, last; };
    std::vector<Reg> regs_;
    std::map<std::string, std::string> written_;   // WriteValue of settings without a rebuild consumer (kept for read-back)
};

} // namespace frontend
