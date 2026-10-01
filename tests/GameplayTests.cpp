#include "game/Gameplay.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
static void steps(game::Player& p,game::Input in,const game::World& world,int count) {
    game::PlayerController c;
    for(int i=0;i<count;++i) { c.step(p,in,world,1.0f/120); in.jump=in.toggle=in.reset=false; }
}
int main() {
    try {
        game::World empty; empty.geometry.clear();
        game::Player robot; robot.position={0,0,10};
        game::Input move; move.forward=true;
        steps(robot,move,empty,120);
        require(robot.position.z<4.5f && robot.position.z>3.5f,"Robot movement speed");
        game::Player vehicle; vehicle.position={0,0,10}; vehicle.form=game::Form::Vehicle;
        steps(vehicle,move,empty,120);
        require(vehicle.position.z<robot.position.z-6,"Vehicle must move faster");
        game::Player jumper; game::Input jump; jump.jump=true; steps(jumper,jump,empty,1);
        require(jumper.position.y>0 && !jumper.grounded,"Jump must leave ground");
        steps(jumper,{},empty,180);
        require(jumper.grounded && std::abs(jumper.position.y)<0.001f,"Gravity must land on floor");
        game::World obstacle; obstacle.geometry={{{0,1,-3},{2,1,1},{1,1,1}}};
        game::Player wall; steps(wall,move,obstacle,120);
        require(wall.position.z>=-1.451f,"Wall penetration");
        game::Player landing; landing.position={0,4,-3}; landing.grounded=false;
        steps(landing,{},obstacle,120);
        require(landing.grounded && std::abs(landing.position.y-2)<0.001f,"Platform landing");
        game::Player under; under.form=game::Form::Vehicle;
        game::World ceiling; ceiling.geometry={{{0,1.8f,0},{3,0.2f,3},{1,1,1}}};
        game::Input transform; transform.toggle=true; steps(under,transform,ceiling,1);
        require(under.form==game::Form::Vehicle,"Unsafe transform clearance");
        steps(under,transform,empty,1);
        require(under.form==game::Form::Robot,"Transform toggle");
        game::Player diagonal; game::Input diag=move; diag.right=true; steps(diagonal,diag,empty,120);
        float travel=std::sqrt(diagonal.position.x*diagonal.position.x+diagonal.position.z*diagonal.position.z);
        require(std::abs(travel-(10-robot.position.z))<0.01f,"Diagonal speed normalization");
        game::PlayerController controller; controller.look(diagonal,0,100000,0.0025f);
        require(diagonal.pitch>=-0.751f,"Pitch clamp");
        game::Health hp; hp.damage(-20); require(hp.current==100,"Negative damage");
        hp.damage(200); require(!hp.alive() && hp.current==0,"Damage floor");
        game::Input reset; reset.reset=true; steps(diagonal,reset,empty,1);
        require(core::length(diagonal.position-empty.spawns.front().position)<0.001f,"Respawn");
        auto view=core::Mat4::lookAt({0,0,3},{0,0,0},{0,1,0});
        require(std::abs(view.m[14]+3)<0.001f,"Existing lookAt math");
        std::cout<<"PASS: movement, form speed, jump/gravity, wall collision, platform landing, transform clearance, diagonal normalization, look clamp, health, reset, camera math\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
