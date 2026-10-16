# Campaign phase backlog (after package 5)

Items deferred to the campaign phase (user roadmap 2026-10-15: campaign + textures + RT, then cross-play MP, then the final
optimisation pass). Owner in brackets; add new rows at the end.

| item | owner | why deferred | notes |
|---|---|---|---|
| Death-type picker (TnDeathPicker): choose the TnDeathType per death from the damage type's DamageDeathType, a headshot and the victim's motion (Hovering / Airborne / momentum / booster), and drive the blueprint entry's death animation, gibs, decals, effects and TnDeathModifierTriggerLight together | Gameplay | Integration 2026-10-15: package 5 ships the Systems approximation of the lights only (SYSTEMS_DEATH_LIGHTS.md) | Data: TR_Deaths_p Shared{Robot,Car,Tank,Jet,Vehicle}Deaths blueprints (ROBODEF / VehicleForm DeathBlueprint), TnDamageType.DamageDeathType / DeathType; replace World::spawnDeathLight's rule with the picked entry's modifiers |
