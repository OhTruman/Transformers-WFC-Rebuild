#include "game/FrontendAudio.h"

#include <cstring>

namespace game {

std::string FrontendAudio::uiCueFor(const char* name) const {
    if (!name || !*name) return {};
    for (const char* bank : {"BL_HUD_INTERFACE.", "BL_LVL_HUD_INTERFACE."}) {
        std::string c = std::string(bank) + name;
        if (cues_.hasCue(c.c_str())) return c;
    }
    return {};
}

int FrontendAudio::playUiSound(const char* name) {
    const std::string c = uiCueFor(name);
    if (c.empty()) return -1;
    SoundCues::Emitter e; e.owner = SoundCues::kUI;              // the PlayerController's 2D component
    return cues_.play(c.c_str(), e, 0.0f);
}

bool FrontendAudio::stopUiSound(const char* name, float fadeSeconds) {
    const std::string c = uiCueFor(name);
    if (c.empty()) return false;
    int id = cues_.oldestInstance(c.c_str());
    if (id < 0) return false;
    cues_.stop(id, fadeSeconds);                                 // FadeOut(FadeTime, 0.0)
    return true;
}

// AssetTools authored.db SeqAct_PlayMusic of the UI levels [CONF authored]; unset fields = MusicTrack defaults.
bool FrontendAudio::frontendTrack(const char* uiLevel, MusicTrack& out) {
    struct Row { const char* level; const char* cue; float fadeIn, fadeOut; };
    static const Row kRows[] = {
        {"UI_FrontEnd_m", "BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01", 0.25f, 1.0f},
        {"UI_Lobby_m", "BL_LVL_HUD_INTERFACE.MP_LOBBY_MX", 0.0f, 1.0f},
        {"UI_CampaignLobby_m", "BL_LVL_HUD_INTERFACE.MP_LOBBY_MX", 0.0f, 1.0f},
        {"UI_PartyLobby_m", "BL_LVL_HUD_INTERFACE.MP_PARTY_LOBBY_MX", 0.0f, 0.0f},
    };
    for (const Row& r : kRows)
        if (uiLevel && std::strcmp(uiLevel, r.level) == 0) {
            out = MusicTrack{};
            out.cue = r.cue; out.fadeIn = r.fadeIn; out.fadeOut = r.fadeOut;
            return true;
        }
    return false;
}

} // namespace game
