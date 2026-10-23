# Gameplay PROV list (campaign + Escalation), 2026-10-23

PROV = a provisional stand-in where the original behaviour isn't reconstructed yet. Each item lists what the player sees, what
replacing it needs, and my effort estimate (S < 1 h, M ~ 2-4 h, L = a subsystem). The items still in progress (the Omega boss AI, hit
reactions, the horde rules) are not listed. Ordered by impact within each group.

## High impact (visible in the first minutes, or blocks progress)

| # | PROV | What the player sees | Needs | Effort |
|---|---|---|---|---|
| H1 | **Campaign AI uses the Classic versus brain** (factory enemies + the squad); no GOAP action / goal sets | enemies and buddies fight like versus bots: no cover tactics, no class-specific actions (snipers, jets, bosses) | the GOAP port (campaign AICD action sets, RE has the sets) | L |
| H2 | **Escalation horde = Classic brain + an awareness stand-in** (the threat sensor) | the horde charges straight in; no CQC LOS breaks, no Brute HunkerDown | the same GOAP port (or the small rules batch) | L / S |
| H3 | **ShootAtActor / UseTurret / interact ops = the generic Out** | scripted moments ("buddy mans the turret", "AI shoots the door") fire their outputs at once without the action | per-op behaviour on the AI | M |
| H4 | **Pawn factories pool ≤ 8 alive per body, 96 in all** | a heavy wave larger than 8 of one body spawns fewer | on-demand participant creation | M |
| H5 | **Destructible triggers by class name** (TnCorruption = any damage, notification = any sibling change, TractorBeam = never) | a few scripted destructibles change state at the wrong moment or never (the Omega batteries' grab) | RE's Tn* trigger semantics | S-M |

## Medium impact (noticeable, not blocking)

| # | PROV | What the player sees | Needs | Effort |
|---|---|---|---|---|
| M1 | Squad aux spawn spots = ±5 m beside the checkpoint | buddies appear at fixed offsets, rarely inside a prop | AssetTools' AuxilliarySpawnPoints export | S |
| M2 | Squad respawn = the nav cell 6 m in front of the human | a respawn may land behind cover the original avoids | the original's "closest unoccupied nav point in front" scoring | S |
| M3 | Squad formation = 3 m beside / behind the leader | buddies hug the player more tightly than the original Move-To-Buddy | the squad goal set (part of H1) | S |
| M4 | Stranded squad member = teleported back to its buddy (PC adaptation) | a rare visible pop | none (keep) or better nav coverage | - |
| M5 | Kismet objective radii Close / Medium / Long = 5 / 12 / 25 m | buddies stop a little early or late at scripted points | RE's RANGE_ values | S |
| M6 | Pawns ride movers by a footprint test (no UE Base) | rare slip on a fast lift edge | a real base attachment | M |
| M7 | Mover-attached nav cells move only when the mover rests | bots can't path while a lift moves (they ride it) | per-step cell updates | S |
| M8 | Streaming: collision of every unit preloaded; load = a hitch at Started, no latent wait | streamed-out geometry still collides; a ~0.3 s hitch per unit load | per-unit collision sets + async load | M |
| M9 | Campaign characters' weapons / stats = the MP iconic presets | the trio's loadouts are the MP ones, not campaign PlayerData | the campaign PlayerData export (AssetTools has the weapons) | M |
| M10 | Escalation pickup weapons: clip / ammo from PlayerData, damage / spread still MP | Escalation weapon damage matches versus, not the original's campaign tuning | PlayerData values in WeaponDef | S |
| M11 | Escalation loot pickups: a 1.5 m ring, 60 s life, no mesh; the ability resource isn't spent by abilities | loot is invisible; picking it up does nothing yet | the pickup mesh (Rendering) + the ability cost model | M |
| M12 | Jet booster hit test = every hit while hovering; Brute backpack = a direction gate | weak points are easier / harder to hit than the attached actors | per-bone / attached-actor hit tests | M |
| M13 | Jet hover trigger = every ~8-9 s with a target | the hover cadence differs from HoverFromTactical's | the GOAP action timing (H1) | S |
| M14 | The ForceIntoForm safe-location step isn't modelled | a forced transform may start in a tight spot | MoveToSafeTransformationLocation | S |

## Low impact (polish / rare)

| # | PROV | Notes |
|---|---|---|
| L1 | `?Character=` URL pick stands in for the in-map CharacterSelect when no menu pick arrives | the Frontend's real pick replaces it |
| L2 | Restart From Last Checkpoint relaunches in place without reloading the map | world state comes back through the Kismet restart |
| L3 | Destructible hit box = the AABB of the rotated mesh box | generous hits on rotated props |
| L4 | Escalation: off-nav spawn points use the stand-in (the versus nav default); the original cell nav is opt-in (WFC_ESCNAV=1) | waiting on AssetTools' versus_only coverage |
| L5 | Escalation spawn facing = toward the nearest player (no authored point rotation) | |
| L6 | Escalation backpack: explodes at 3 s; the Extinguish anim's earlier end isn't used | |
| L7 | Loot extras: headshot +5 missing (no headshot state) | |
| L8 | Matinee: no Toggle / Visibility / material-param / slomo tracks; mover rotation not applied; pawn Move tracks not applied | visible only in some cutscenes |
| L9 | Skeletal matinee actors: DrawScale3D, pitch / roll not applied | |
| L10 | Bodies: a missing faction variant borrows the other faction's export | none left after AssetTools' 57 / 57 |

**My recommendation for the final build:** fix H3, H5, M1, M5, M10 and M11 (all S-M with data in hand); keep H1 / H2 as PROV unless
the GOAP port is scheduled (L); the rest can stay.
