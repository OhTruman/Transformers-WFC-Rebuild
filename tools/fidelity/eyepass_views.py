# Authored eye-pass views (Integration 2026-10-16): per user map, 5 fixed cameras chosen from the map data so each sheet covers
# the cases the user named: a street under buildings, a reflective floor / wall, a dark interior, a vehicle lane, the busiest
# firefight spot. Writes tools/fidelity/views/<map id>.txt in the product's WFC_SHOTLIST format ("<name> x,y,z,tx,ty,tz",
# glTF metres; text after the camera is ignored by the parser and carries the reason). Each view is listed 3 times
# (<name>_w1, <name>_w2, <name>) so the camera is held 24 frames before the kept capture: Realistic's temporal denoiser
# restarts at a camera cut, and an 8-frame hold would grade unconverged noise, not the lighting.
#   python eyepass_views.py [map ids]       (default 508 510 507 504 509)
# Method (no GPU): candidate cameras ring the gameplay points (KOTH zones, domination points, flag / bomb points, team spawns,
# pickups) at 6-14 m, 2-4 m above the point, in 12 azimuths. A candidate must see its target (clear ray in collision.glb), stand
# in open space (no hit within 0.6 m in 10 directions) over a floor (collision_pawn.glb, within 25 m below), and is scored per
# case from rays cast at its target:
#   covered street  open sky straight up from the target is NOT required; tall walls on both sides (side rays at 30 deg
#                   elevation hit 8-40 m up) and a long clear run along the view (>= 25 m) - a street between buildings
#   reflective      the floor under the target (bsp.glb) has an environment-mapped material (materials_pbr env_slot) or a
#                   spec / metal / glass / wet name, and the camera looks down across it at 15-35 deg
#   dark interior   a ceiling straight up within 12 m, walls within 20 m on 7+ of 16 sides, and the darkest quarter of the
#                   light sum (lighting.json: brightness x (1 - d / radius)^2) among the candidate targets
#   vehicle lane    the longest straight clear run at 1 m over a flat floor (height change < 1.5 m over the run), viewed
#                   along it
#   firefight       the KOTH / domination point closest to the centroid of both teams' spawns, from the most open side
# The best candidate per case is kept; views of the same case may not repeat a target already used by another case.
import json, math, os, struct, sys
import numpy as np

MAPS = {"508": "MP_IAC_Streets", "510": "MP_UND_Gorge", "507": "MP_ORB_Debris", "504": "MP_IAC_Rust", "509": "MP_KON_Molten"}
SRC = "F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps"
RENDER = "F:/Transformers Rebuild/Rebuild/work/render"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "views")

def glb(path, want_mat=False):
    b = open(path, "rb").read(); n = struct.unpack("<I", b[12:16])[0]; g = json.loads(b[20:20 + n])
    off = 20 + n; bin_ = b[off + 8: off + 8 + struct.unpack("<I", b[off:off + 4])[0]]
    def acc(i):
        a = g["accessors"][i]; v = g["bufferViews"][a["bufferView"]]; o = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        ct = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]]; k = {"SCALAR": 1, "VEC3": 3, "VEC2": 2, "VEC4": 4}[a["type"]]
        st = v.get("byteStride", 0); isz = np.dtype(ct).itemsize * k
        if st and st != isz:
            raw = np.frombuffer(bin_, np.uint8, count=st * (a["count"] - 1) + isz, offset=o)
            return np.stack([np.frombuffer(raw[j * st: j * st + isz].tobytes(), ct) for j in range(a["count"])])
        return np.frombuffer(bin_, ct, count=a["count"] * k, offset=o).reshape(-1, k) if k > 1 else np.frombuffer(bin_, ct, count=a["count"], offset=o)
    def trs(nd):
        if "matrix" in nd: return np.array(nd["matrix"], np.float64).reshape(4, 4).T
        m = np.eye(4); t = nd.get("translation", [0, 0, 0]); r = nd.get("rotation", [0, 0, 0, 1]); s = nd.get("scale", [1, 1, 1])
        x, y, z, w = r
        R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
        m[:3, :3] = R * np.array(s); m[:3, 3] = t; return m
    tris, mats = [], []
    def walk(i, P):
        nd = g["nodes"][i]; M = P @ trs(nd)
        if "mesh" in nd:
            for pr in g["meshes"][nd["mesh"]]["primitives"]:
                if pr.get("mode", 4) != 4: continue
                p = acc(pr["attributes"]["POSITION"]).astype(np.float64); p = p @ M[:3, :3].T + M[:3, 3]
                ix = acc(pr["indices"]).astype(np.int64).reshape(-1, 3) if "indices" in pr else np.arange(len(p)).reshape(-1, 3)
                tris.append(p[ix]); mats.extend([g["materials"][pr["material"]].get("name", "") if want_mat and "material" in pr else ""] * len(ix))
        for c in nd.get("children", []): walk(c, M)
    for sc in g.get("scenes", [{"nodes": list(range(len(g["nodes"])))}]):
        for i in sc["nodes"]: walk(i, np.eye(4))
    return (np.concatenate(tris) if tris else np.zeros((0, 3, 3))), np.array(mats)

class Mesh:
    def __init__(self, T, mats=None):
        self.v0 = T[:, 0]; self.e1 = T[:, 1] - T[:, 0]; self.e2 = T[:, 2] - T[:, 0]; self.mats = mats
        self.lo = T.min(1); self.hi = T.max(1); self.T = T
    def sub(self, c, r):
        """the triangles within r of point c (a local copy: rays near one target test a few thousand triangles, not the map)"""
        k = np.all((self.hi >= np.asarray(c) - r) & (self.lo <= np.asarray(c) + r), 1)
        return Mesh(self.T[k], self.mats[k] if self.mats is not None and len(self.mats) else None)
    def ray(self, o, d, tmax=1e9):
        """nearest hit distance (inf if none) and the triangle index"""
        o = np.asarray(o, float); d = np.asarray(d, float); d = d / np.linalg.norm(d); e = o + d * min(tmax, 2000)
        lo, hi = np.minimum(o, e) - 0.01, np.maximum(o, e) + 0.01
        sel = np.nonzero(np.all((self.hi >= lo) & (self.lo <= hi), 1))[0]
        if not len(sel): return math.inf, -1
        v0, e1, e2 = self.v0[sel], self.e1[sel], self.e2[sel]
        p = np.cross(d, e2); det = (e1 * p).sum(1); ok = np.abs(det) > 1e-9; inv = np.where(ok, 1 / np.where(ok, det, 1), 0)
        s = o - v0; u = (s * p).sum(1) * inv; q = np.cross(s, e1); v = (q * d).sum(1) * inv; t = (e2 * q).sum(1) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4) & (t < tmax)
        if not hit.any(): return math.inf, -1
        k = np.argmin(np.where(hit, t, np.inf)); return float(t[k]), int(sel[k])

def unit(az, el=0.0): return np.array([math.sin(az) * math.cos(el), math.sin(el), math.cos(az) * math.cos(el)])

def views(mid):
    name = MAPS[mid]; src = os.path.join(SRC, name)
    col = Mesh(glb(os.path.join(src, "collision.glb"))[0]); pawn = Mesh(glb(os.path.join(src, "collision_pawn.glb"))[0])
    colW, pawnW = col, pawn   # whole-map meshes (the loop below works on local copies)
    wpn = Mesh(glb(os.path.join(src, "collision_weapon.glb"))[0])   # what bullets hit: the visible world without player-only blockers
    bt, bm = glb(os.path.join(RENDER, name, "bsp.glb"), True); bsp = Mesh(bt, bm); bspW = bsp
    pbr = json.load(open(os.path.join(RENDER, name, "materials_pbr.json"), encoding="utf-8"))
    def reflective_mat(m):
        e = pbr.get(m) or next((v for k, v in pbr.items() if k.endswith("." + m) or k.split(".")[-1] == m), None)
        return bool(e and e.get("env_slot") is not None) or any(w in m.lower() for w in ("spec", "metal", "glass", "wet", "chrome", "mirror", "shiny"))
    # lights: the render data's lighting.json (glTF metres, radius_m, brightness); "lit" = sum of brightness x (1 - d / r)^2
    lights = [(np.array(l["position"], float), float(l.get("radius_m") or 10), float(l.get("brightness") or 1))
              for l in json.load(open(os.path.join(RENDER, name, "lighting.json"), encoding="utf-8")).get("lights", []) if l.get("position")]
    def lit(p): return sum(br * (1 - np.linalg.norm(q - p) / r) ** 2 for q, r, br in lights if np.linalg.norm(q - p) < r)
    sp = json.load(open(os.path.join(src, "spawnpoints.json"), encoding="utf-8"))["points"]
    pts = [(p["class"], p["actor"], np.array(p["location_gltf"], float)) for p in sp if not p.get("volume") and "location_gltf" in p]
    pts += [(p["class"], p["actor"], np.array(p["location_gltf"], float)) for p in sp if p.get("volume") and p["class"] in ("TnKingOfTheHillZone",) and "location_gltf" in p]
    goal = [p for p in pts if p[0] in ("TnKingOfTheHillZone", "TnDominationPoint", "TnFlagCapturePoint", "TnBombPlantPoint")]
    spawns = [p for p in pts if p[0] == "TnTeamPlayerStart"]
    targets = goal + spawns[::4] + [p for p in pts if "Pickup" in p[0]]
    def ground(p, m=None):
        t, _ = (m or pawnW).ray(p + [0, 2.0, 0], [0, -1, 0], 30); return p + [0, 2.0 - t, 0] if t < 30 else None
    def open_at(c):
        loc = col.sub(c, 1.0)
        for az in np.linspace(0, 2 * math.pi, 8, endpoint=False):
            if loc.ray(c, unit(az), 0.6)[0] < 0.6: return False
        return loc.ray(c, [0, 1, 0], 0.6)[0] >= 0.6 and loc.ray(c, [0, -1, 0], 0.6)[0] >= 0.6
    cands = []
    for cls, actor, p in targets:
        g0 = ground(p)
        if g0 is None: continue
        tgt = g0 + [0, 1.5, 0]
        col, pawn, bsp = colW.sub(tgt, 130), pawnW.sub(tgt, 75), bspW.sub(tgt, 10)
        vis = wpn.sub(tgt, 70)   # visible geometry: weapon collision (collision.glb has invisible player blockers, bsp.glb lacks the static meshes)
        up = vis.ray(tgt, [0, 1, 0], 200)[0]
        walls = [vis.ray(tgt, unit(az), 60)[0] for az in np.linspace(0, 2 * math.pi, 16, endpoint=False)]
        light = lit(tgt)
        fl, ftri = bsp.ray(tgt, [0, -1, 0], 6); fmat = bsp.mats[ftri] if ftri >= 0 else ""
        for az in np.linspace(0, 2 * math.pi, 12, endpoint=False):
            for dist, dh in ((7, 2.0), (10, 3.0), (14, 4.0)):
                c = tgt + unit(az) * dist + [0, dh, 0]
                if not open_at(c): continue
                gc = pawn.ray(c, [0, -1, 0], 25)[0]   # (25 m: Debris platforms sit over void; a camera over a gap may still see its target)
                if gc >= 25: continue
                v = tgt - c; dv = np.linalg.norm(v)
                if col.ray(c, v, dv - 0.3)[0] < dv - 0.3: continue
                look = v / dv; az_l = math.atan2(look[0], look[2]); el_l = math.degrees(math.asin(-look[1]))
                run = col.ray(tgt + [0, 1, 0], [look[0], 0, look[2]], 120)[0]
                sides = [col.ray(tgt, unit(az_l + s * math.pi / 2, math.radians(30)), 80)[0] for s in (-1, 1)]
                side_h = [s * math.sin(math.radians(30)) for s in sides]
                far = tgt + np.array([look[0], 0, look[2]]) * min(run, 60); gf = ground(far + [0, 1, 0], pawn); flat = gf is not None and abs(gf[1] - g0[1]) < 1.5
                cands.append(dict(cls=cls, actor=actor, c=c, t=tgt, up=up, walls=walls, light=light, fmat=fmat, el=el_l, run=run, side_h=side_h, flat=flat, dist=dist, same_floor=abs((c[1] - gc) - g0[1]) <= 3.0,
                                  open=sum(w > 25 for w in walls)))
    col, pawn, bsp = colW, pawnW, bspW
    if not cands: return []
    cen = np.mean([p[2] for p in spawns], 0) if spawns else np.mean([c["t"] for c in cands], 0)
    def s_street(k): return (all(8 <= h <= 40 for h in k["side_h"]) and k["run"] >= 25) * (k["run"] + 10 * min(k["side_h"]))
    def s_refl(k): return (bool(k["fmat"]) and reflective_mat(k["fmat"]) and 15 <= k["el"] <= 35) * (40 - abs(k["el"] - 25))
    lit_all = sorted(k["light"] for k in cands); dark_max = lit_all[len(lit_all) // 4] if lit_all else 0   # the darkest quarter of the candidate targets
    def s_dark(k): return (100 - k["up"] - 10 * k["light"] / max(dark_max, 1e-3)) if (k["up"] <= 12 and sum(w < 20 for w in k["walls"]) >= 7 and k["light"] <= dark_max) else 0
    def s_lane(k): return (k["flat"] and k["run"] >= 30) * k["run"]
    def s_fight(k): return (k["cls"] in ("TnKingOfTheHillZone", "TnDominationPoint")) * (1000 - np.linalg.norm(k["t"] - cen) + 3 * k["open"])
    def s_dark2(k): return (200 - 10 * k["light"] - k["up"]) if k["up"] <= 30 else 0                       # fallback: darkest spot with a roof
    def s_fight2(k): return (k["cls"] in ("TnFlagCapturePoint", "TnBombPlantPoint", "TnTeamPlayerStart")) * (1000 - np.linalg.norm(k["t"] - cen) + 3 * k["open"])
    def s_refl2(k): return (k["run"] >= 8 and k["el"] <= 20) * (k["run"] + 1 - k["el"] / 10)                     # fallback: a glancing view over open floor
    cases = (("firefight", ((s_fight, "busiest fight spot: objective nearest the spawn centroid"), (s_fight2, "busiest fight spot (no KOTH / domination point): flag / bomb point or spawn nearest the spawn centroid"))),
             ("street", ((s_street, "street under buildings"),)),
             ("dark", ((s_dark, "dark interior (ceiling within 12 m, walled on 7+ of 16 sides within 20 m, darkest quarter of the light sum)"), (s_dark2, "darkest roofed spot (no walled dark interior on this map)"))),
             ("reflective", ((s_refl, "reflective floor"), (s_refl2, "glancing view over open floor (no env-mapped / spec floor material found under a target)"))),
             ("lane", ((s_lane, "vehicle lane (long flat clear run)"), (lambda k: min(k["run"], 120), "longest clear run (no flat 30 m lane on this map)"))))
    # views/reject.txt: "<map id> <target actor> <reason>" - targets whose camera renders wrong (inside unlisted geometry);
    # found by eye on the test-render pass, the next-best candidate is used instead
    rj = os.path.join(OUT, "reject.txt")
    rejected = {l.split()[1] for l in open(rj) if l.strip() and l.split()[0] == mid} if os.path.exists(rj) else set()
    out, used = [], set(rejected)
    for case, rules in cases:
        best = None
        for strict in (True, False):   # strict: the camera stands on its target's floor (+-3 m); loose: over a gap / another level
            for fn, why in rules:
                best = max((k for k in cands if k["actor"] not in used and (k["same_floor"] or not strict)), key=fn, default=None)
                if best is not None and fn(best) > 0: break
            if best is not None and fn(best) > 0: break
        else:
            out.append((case, None, f"{rules[0][1]}: no candidate on this map")); continue
        if not best["same_floor"]: why += " [camera not on the target's floor]"
        used.add(best["actor"]); out.append((case, best, f"{why}; target {best['actor']} ({best['cls']}); floor {best['fmat'] or '-'}; ceiling {best['up']:.0f} m; run {min(best['run'], 120):.0f} m; light sum {best['light']:.1f}; view down {best['el']:.0f} deg"))
    return out

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for mid in (sys.argv[1:] or ["508", "510", "507", "504", "509"]):
        vs = views(mid); lines = []
        for case, k, why in vs:
            print(f"{mid} {case}: {why}")
            if k is None: continue
            cam = "{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}".format(*k["c"], *k["t"])
            lines += [f"{case}_w1 {cam}", f"{case}_w2 {cam}", f"{case} {cam} {why}"]
        open(os.path.join(OUT, f"{mid}.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
