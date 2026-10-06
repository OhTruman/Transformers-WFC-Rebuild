"""Milestone 07 expectations, derived only from AssetTools manifests and ExtractedAssets (never from the build under test).

    python gen_expectations.py [out.json]

maps        every registry map: lobby MapId, display name, modes, cooked / pipeline / runtime-data readiness, KillZ (m),
            start counts per class, objective actors per mode, launchable flag
characters  every MP chassis: faction, default class, vehicle form, availability, exported robot / vehicle / character.json
class_presets the four default classes per faction: chassis, iconic names, weapons, vehicle weapon, abilities
modes       every game mode: tag, friendly name, team / FFA, default goal, maps offering it, objective actor classes
"""
import json, os, sys

AT = r"F:\Transformers Rebuild\AssetTools\manifests"
VS = r"F:\Transformers Rebuild\ExtractedAssets\VerticalSlice"
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "expectations.m07.json")

def load(p):
    with open(p, encoding="utf-8") as f:
        return json.load(f)

# mode -> objective / spawn actor classes in spawnpoints.json (authored)
MODE_ACTORS = {"CTF": ["TnFlagCapturePoint", "TnGameObjectivePickupFactoryFlag"], "KOTH": ["TnKingOfTheHillZone"],
               "DOM": ["TnDominationPoint"], "EXT": ["TnBombPlantPoint", "TnGameObjectivePickupFactoryBomb"],
               "TDM": [], "DM": [], "SV": ["TnSurvivalSpawnPoint"]}
MODE_STARTS = {"DM": "TnFreeForAllPlayerStart", "SV": "TnTeamPlayerStart"}

readiness = load(os.path.join(AT, "next_map", "readiness.json"))["maps"]
pipeline = load(os.path.join(AT, "maps", "pipeline_summary.json"))
catalog = load(os.path.join(AT, "mp_map_catalog.json"))
cat_maps = catalog["maps"]
cat_items = cat_maps.items() if isinstance(cat_maps, dict) else [(m.get("runtime") or m.get("map") or m.get("package"), m) for m in cat_maps]

def runtime_dir(base):
    b = base.replace("_BASE_m", "").replace("_Base_m", "").replace("_base_m", "")
    return b

maps = []
for key, c in cat_items:
    c = c if isinstance(c, dict) else {}
    base = c.get("MapFilename") or c.get("map") or c.get("package") or key
    rt = runtime_dir(str(base))
    md = os.path.join(VS, "Maps", rt)
    has_rt = os.path.isdir(md) and all(os.path.exists(os.path.join(md, f)) for f in ("world.glb", "collision_pawn.glb", "gameplay.json", "spawnpoints.json", "physics.json"))
    rd = next((v for k, v in readiness.items() if k.lower().startswith(rt.lower())), {})
    pl = pipeline["maps"].get(rt, {})
    fn = c.get("FriendlyName"); fn = fn.get("INT") if isinstance(fn, dict) else fn
    entry = {"runtime": rt, "base": base, "name": fn or rd.get("name"), "ini_order": c.get("ini_order"),
             "mapId": int(c.get("MapId")) if c.get("MapId") else None, "modes": c.get("CompatibleGameTypes") or rd.get("modes") or pl.get("modes") or [],
             "cooked": bool(c.get("cooked", pl.get("cooked_in_dump", False))), "pipeline": pl.get("pipeline"), "structure_audit": pl.get("structure_audit"),
             "runtime_data": has_rt}
    if has_rt:
        phys = load(os.path.join(md, "physics.json")).get("world", {})
        kz = [v.get("KillZ") for k, v in phys.items() if k.lower().endswith("_base_m") and isinstance(v, dict) and v.get("KillZ") is not None]
        entry["killz_m"] = (kz[0] * 0.01) if kz else -2621.43
        pts = load(os.path.join(md, "spawnpoints.json"))
        pts = pts["points"] if isinstance(pts, dict) else pts
        counts = {}
        for p in pts:
            counts[p.get("class")] = counts.get(p.get("class"), 0) + 1
        entry["actor_counts"] = {k: v for k, v in counts.items() if k and k.startswith("Tn")}
        entry["mode_objectives_present"] = {m: all(counts.get(a, 0) > 0 for a in MODE_ACTORS.get(m, [])) for m in entry["modes"]}
        gp = load(os.path.join(md, "gameplay.json"))
        entry["player_starts"] = len([s for s in gp.get("player_starts", []) if len(s.get("location_gltf") or []) >= 3])
        entry["initial_spawn_clusters"] = [c2["actor"] for c2 in gp.get("spawn_clusters", []) if c2.get("authored", {}).get("InitialSpawn")]
    entry["launchable"] = bool(entry["runtime_data"] and entry["cooked"])
    entry["versus_launchable"] = entry["launchable"] and any(m in ("TDM", "DM") for m in entry["modes"])
    maps.append(entry)

# ---- characters
mpc = load(os.path.join(AT, "mp_content", "mp_characters.json"))
roster = load(os.path.join(AT, "mp_content", "roster_package.json"))
avail = set(mpc["summary"].get("mp_available_by_default", []))
chars = []
for ch in mpc["characters"]:
    cid = ch["chassis_id"]; d = os.path.join(VS, "Characters", cid)
    chars.append({"chassis": cid, "iconic": ch.get("iconic_id"), "faction": ch.get("faction"), "default_class": ch.get("default_class"),
                  "vehicle_form": ch.get("vehicle_form"), "available_by_default": cid in avail,
                  "export": {f: os.path.exists(os.path.join(d, f)) for f in ("robot.glb", "vehicle.glb", "character.json")},
                  "display_custom": (ch.get("display_name") or {}).get("custom_body", {}).get("INT"),
                  "display_iconic": (ch.get("display_name") or {}).get("iconic", {}).get("INT")})
presets = {}
for cls, p in roster["default_four_classes (MP presets)"].items():
    presets[cls] = {"Autobot": p.get("Autobot"), "Decepticon": p.get("Decepticon"), "names": p.get("names"), "weapons": p.get("weapons"),
                    "vehicle_weapons": p.get("vehicle_weapons"), "melee": p.get("melee"), "abilities": p.get("abilities")}
weapon_exports = sorted(n for n in os.listdir(os.path.join(VS, "Weapons")) if os.path.isdir(os.path.join(VS, "Weapons", n)))

# ---- modes
fm = load(os.path.join(AT, "frontend_modes.json"))
gmi = fm["game_mode_info (TnDataProvider_GameModeInfo: Xe-TransGame.ini + TransGame.int)"]
maps_per_mode = fm.get("maps_per_mode", {})
modes = []
for cls, g in gmi.items():
    tag = g.get("SettingsConfigName")
    if not tag:
        continue
    modes.append({"tag": tag, "class": cls, "name": g.get("FriendlyName"), "team": tag not in ("DM",),
                  "objective_actors": MODE_ACTORS.get(tag, []), "start_class": MODE_STARTS.get(tag, "TnTeamPlayerStart"),
                  "maps": maps_per_mode.get(tag) if isinstance(maps_per_mode, dict) else None,
                  "maps_with_runtime": [m["runtime"] for m in maps if tag in m["modes"] and m["launchable"]],
                  "rules": (fm.get("online_game_settings_classes", {}).get("TnOnlineGameSettings" + tag, {}) or {}).get("Rules", [])})

# ---- mode-dependent visibility (BASE Kismet SeqCond_GameRuleActive -> SeqAct_ToggleHidden, gameplay.json): the number
# of those actors visible under each mode = initially visible, plus targets of an UnHide rule the mode carries
for m in maps:
    gp = os.path.join(VS, "Maps", m["runtime"] or "", "gameplay.json")
    if not m["runtime"] or not os.path.exists(gp):
        continue
    mdv = load(gp).get("mode_dependent_visibility", []) or []
    actors = {}
    for r in mdv:
        for t in r.get("targets", []):
            actors.setdefault(t["actor"], not t.get("initially_hidden", True))
    m["mode_actors_total"] = len(actors)
    m["mode_visible_expected"] = {}
    for md in modes:
        vis = dict(actors)
        for r in mdv:
            if r.get("rule") in md["rules"]:
                for t in r.get("targets", []):
                    vis[t["actor"]] = (r.get("action") == "UnHide")
        m["mode_visible_expected"][md["tag"]] = sum(vis.values())

# ---- title Matinee (UI_FrontEnd Kismet, frontend_flow.json): every SeqAct_Interp with its comment and looping flag
def interps(node, acc):
    if isinstance(node, dict):
        if node.get("class") == "SeqAct_Interp":
            a = node.get("authored", {}) or {}
            acc.append({"object": node.get("object"), "comment": a.get("ObjComment"), "looping": bool(a.get("bLooping"))})
        for v in node.values(): interps(v, acc)
    elif isinstance(node, list):
        for v in node: interps(v, acc)
    return acc
ff = load(os.path.join(AT, "frontend_flow.json"))
title_interps = interps(ff, [])

out = {"generated_by": "tools/fidelity/m07/gen_expectations.py", "sources": [AT, VS], "maps": maps, "characters": chars,
       "class_presets": presets, "weapon_exports": weapon_exports, "modes": modes, "title_matinees": title_interps,
       "rules": {"optimus_fallback": "a selected chassis other than Optimus resolving to Characters/Optimus/* is an explicit FAIL (OPTIMUS FALLBACK)",
                 "vehicle_form_by_class": "Scout=car, Scientist=jet, Soldier=tank, Leader=truck (CONFIRMED, mp_classes.json)"}}
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=1)
print("maps %d (launchable %d), characters %d (exported %d), modes %d -> %s" % (len(maps), sum(m["launchable"] for m in maps), len(chars),
      sum(all(c["export"].values()) for c in chars), len(modes), OUT))
