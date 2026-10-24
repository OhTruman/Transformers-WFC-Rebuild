"""Per-primitive cull distances for the renderer (300 fps pass, RE CAMPAIGN_CULLING_LOD_RE.md section 2): the original draws a primitive
only while the view origin is within its PrimitiveComponent.CachedCullDistance of its bounds (0 = never culled). The value is read as
shipped from authored.db (never recomputed from the CullDistanceVolumes) and written as a sidecar next to the map's render data, so the
asset root (ExtractedAssets: read only) is untouched.

  build_cull_distance.py --world <world.glb> --prefix <level package prefix> --out <render dir> [--db authored.db]

  MP / DLC map:  --world <asset root>/Maps/<MAP>/world.glb --prefix <MAP>_ --out <render root>/<MAP>
  campaign unit: --world <unit>/world.glb --prefix <unit>_m --out <render root of the unit>

Writes <out>/cull_distance.json:
  nodes: world.glb node name -> distance (UE units), static meshes. A collection-actor node carries its component path; a plain actor
         node is resolved by its short name in the prefix's level packages (several matches: the one whose StaticMesh is the node's).
  fx:    emitter actor short name -> distance (its ParticleSystemComponent), for the map FX path.
Only distances > 0 are listed.
"""
import argparse, json, os, sqlite3, struct

GEN = "tools/render/build_cull_distance.py"


def glb_nodes(path):
    b = open(path, "rb").read()
    n = struct.unpack("<I", b[12:16])[0]
    return json.loads(b[20:20 + n]).get("nodes", [])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--world", required=True)
    ap.add_argument("--prefix", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--db", default="F:/Transformers Rebuild/AssetTools/manifests/authored.db")
    a = ap.parse_args()
    c = sqlite3.connect("file:%s?mode=ro" % a.db, uri=True)
    like = a.prefix.replace("_", "\\_") + "%"

    def props(opath):
        r = c.execute("select props from objects where opath = ?", (opath,)).fetchone()
        return json.loads(r[0]) if r and r[0] else {}

    def dist(p):
        v = p.get("CachedCullDistance")
        return float(v) if isinstance(v, (int, float)) and v > 0 else 0.0

    nodes, fx = {}, {}
    stats = {"nodes": 0, "resolved": 0, "ambiguous": 0, "unresolved": 0}
    for n in glb_nodes(a.world):
        e = n.get("extras") or {}
        if not e.get("mesh"):
            continue
        stats["nodes"] += 1
        comp = e.get("component")
        if not comp:   # a plain actor: its StaticMeshComponent through the level package(s)
            rows = c.execute("select opath, props from objects where opath like ? escape '\\' and opath like ?",
                             (like, "%.TheWorld.PersistentLevel." + e.get("actor", "?"))).fetchall()
            cands = []
            for op, pr in rows:
                d = json.loads(pr or "{}")
                ref = d.get("StaticMeshComponent") or d.get("CollisionComponent")
                if isinstance(ref, dict): ref = ref.get("ref")
                if ref: cands.append(ref)
            if len(cands) > 1:
                same = [r for r in cands if (props(r).get("StaticMesh") or {}) in (e["mesh"], {"ref": e["mesh"]})]
                if len(same) == 1: cands = same
                else: stats["ambiguous"] += 1; cands = cands[:1]
            comp = cands[0] if cands else None
        if not comp:
            stats["unresolved"] += 1
            continue
        stats["resolved"] += 1
        d = dist(props(comp))
        if d > 0:
            nodes[n.get("name")] = d
    for op, pr in c.execute("select opath, props from objects where opath like ? escape '\\' and class = 'ParticleSystemComponent'", (like,)):
        d = dist(json.loads(pr or "{}"))
        parts = op.split(".")
        if d > 0 and len(parts) >= 2 and parts[-2].startswith("Emitter"):
            fx[parts[-2]] = d
    os.makedirs(a.out, exist_ok=True)
    out = {"generated_by": GEN, "world": os.path.abspath(a.world), "prefix": a.prefix, "units": "UE units (0 / absent = never culled)",
           "rule": "draw while distance(view origin, bounds) <= distance (RE CAMPAIGN_CULLING_LOD_RE.md section 2)",
           "stats": dict(stats, culled_nodes=len(nodes), culled_fx=len(fx)), "nodes": nodes, "fx": fx}
    json.dump(out, open(os.path.join(a.out, "cull_distance.json"), "w", encoding="utf-8"), indent=0)
    print("cull_distance:", a.prefix, out["stats"])


if __name__ == "__main__":
    main()
