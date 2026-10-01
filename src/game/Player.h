// Clean-room reconstruction — local player: owns a controller + its pawn.
#pragma once
#include "game/PlayerController.h"
#include "game/Character.h"
#include "game/Team.h"
#include "render/Renderer.h"

namespace game {

class Player {
public:
    Player() { controller_.possess(&pawn_); }

    Character& pawn() { return pawn_; }
    const Character& pawn() const { return pawn_; }
    PlayerController& controller() { return controller_; }
    const PlayerController& controller() const { return controller_; }
    Team team() const { return team_; }
    void setTeam(Team t) { team_ = t; }

    void draw(render::IRenderer& r) const { pawn_.draw(r); }

private:
    Character pawn_;
    PlayerController controller_;
    Team team_ = Team::Autobot;
};

} // namespace game
