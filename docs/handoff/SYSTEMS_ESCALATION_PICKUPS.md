# Escalation weapon pickups (BrokenHope 17 / Remnant 16): finding - not consumed, not played (Systems, 2026-10-17)

Both preconditions for the CAMPAIGN_BACKLOG item 1 regen are **no** on integration af4d0731+, so the regen was not run
(it would also write into ExtractedAssets / AssetTools, which are read only to the lanes; it belongs to AssetTools).
(1) The runtime does not consume weapon pickup factories in any map: `World::loadPickupFactories` (src/game/World.cpp)
instances only TnAmmoCratePickupFactory / TnHealthPickupFactory / TnOverShieldPickupFactory rows of gameplay.json "pickups"
and skips every other class (`else continue`); TnWeaponPickupFactory rows added to MP_ESC_*'s gameplay.json would be read and
dropped (the only weapon-factory logic is the objective TnGameObjectiveWeaponPickupFactory flag / bomb path in MapState.cpp).
(2) Escalation does not play: the match layer has the six versus modes only (`gameModeName`: DM, TDM, CTF, KOTH, EXT, DOM;
no SV / wave rules - the Escalation wave GRI properties are read by the HUD data store but nothing sets them), and the
SPAWNFILL harness already reports MP_ESC_* versus layouts as N/A (one side, 4 authored starts). So the 33 factories have no
consumer and no mode to appear in; the regen only becomes useful together with an Escalation (SV) mode and a weapon-pickup
actor (spawn, pickup, respawn rules of TnWeaponPickupFactory) - a package-8+ item, not package 7.
