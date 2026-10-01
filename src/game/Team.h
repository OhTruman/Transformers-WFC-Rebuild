// Clean-room reconstruction — teams (scaffold).
#pragma once

namespace game {

enum class Team { None, Autobot, Decepticon };

inline const char* teamName(Team t) {
    switch (t) {
        case Team::Autobot: return "Autobot";
        case Team::Decepticon: return "Decepticon";
        default: return "None";
    }
}

} // namespace game
