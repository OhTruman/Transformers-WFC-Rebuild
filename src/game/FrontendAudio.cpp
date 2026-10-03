#include "game/FrontendAudio.h"
#include "game/AmbientAudio.h"

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

// The level manifest's SeqAct_PlayMusic (tools/systems/gen_level_audio.py from authored.db) [CONF authored].
bool FrontendAudio::frontendTrack(const char* level, MusicTrack& out) {
    return level && AmbientAudio::levelMusicTrack(level, out);
}

} // namespace game
