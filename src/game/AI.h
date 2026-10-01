// Clean-room reconstruction — AI controller (scaffold).
// Currently a no-op brain; real behaviour trees / navigation to come.
#pragma once

namespace game {

class World;
class Character;

class AIController {
public:
    explicit AIController(Character* pawn) : pawn_(pawn) {}
    // Produces movement/aim intent for its pawn. Placeholder: idle.
    void think(World& world, float dt) { (void)world; (void)dt; }

private:
    Character* pawn_ = nullptr;
};

} // namespace game
