// Clean-room reconstruction — DEBUG-ONLY QA panel (NOT ORIGINAL; development convenience for fidelity testing).
// A separate native tool window, never part of the original menus: development builds only (CMake WFC_DEV_TOOLS, ON by
// default; build.ps1 -Shipping compiles it out). F10 opens / hides it; WFC_QA=1 also opens it at startup.
// Picks a map / mode / class (and, when Gameplay provides it, a weapon) and launches or restarts that scenario through
// the normal frontend flow, or returns to the title.
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace platform {

struct QaRequest {
    enum class Kind { None, Launch, Restart, Title, Respawn, NextStart, Noclip, God, Dummy, SwapCharacter,
                      BotOverlay, FreezeBots, KillBots, TeleportAim, ModeChanged } kind = Kind::None;
    int mapId = -1;
    std::string mode, character, weapon;
};

class QaPanel {
public:
    struct Option { std::string label; std::string value; };
    virtual ~QaPanel() = default;
    // Fills the lists (maps: value = MapId; modes: value = mode tag; characters: value = custom slot; weapons: value =
    // weapon id, empty list = no weapon override available).
    virtual void setOptions(const std::vector<Option>& maps, const std::vector<Option>& modes, const std::vector<Option>& characters,
                            const std::vector<Option>& weapons) = 0;
    virtual void show(bool visible) = 0;
    virtual bool visible() const = 0;
    virtual void setStatus(const std::string& text) = 0;
    virtual QaRequest poll() = 0;   // the button pressed since the last poll (Kind::None if none)
    virtual void setBots(const std::string& text) { (void)text; }   // the per-bot debug list (DEV TOOL)
    virtual void setMaps(const std::vector<Option>& maps) { (void)maps; }   // refill the map list (filtered by the mode)
};

// The platform's panel, or nullptr where none exists.
std::unique_ptr<QaPanel> createQaPanel();

} // namespace platform
