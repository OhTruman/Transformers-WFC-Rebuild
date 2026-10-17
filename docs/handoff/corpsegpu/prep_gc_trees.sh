#!/bin/sh
# Prepare the two GPU-corpse check trees from the integration head: int_gcb = head + the corpse-stage hook (with the look-back camera),
# int_gcn = head + edit_corpse_gpu.js + the hook. Usage: sh prep_gc_trees.sh <head sha>   (run from work/)
set -e
H=$1
for t in gcb gcn; do
  rm -rf int_$t && mkdir -p int_$t
  git -C .. archive --format=tar $H > a_$t.tar && tar -xf a_$t.tar -C int_$t && rm a_$t.tar
done
(cd int_gcn && node ../edit_corpse_gpu.js)
for t in gcb gcn; do
  "/f/Transformers Rebuild/AssetTools/bin/py/python.exe" -B pass29/stage_hooks2.py int_$t
  node -e "
const fs=require('fs');const f='int_$t/src/game/World.cpp';let s=fs.readFileSync(f,'utf8');
const a='player_.controller().setSpectatorView(spot - f * camD + core::Vec3{0, 4.0f, 0}, yaw0, -0.3f * 9.0f / camD, 0.0f);';
if(s.split(a).length!==2) throw new Error('hook anchor $t');
s=s.replace(a,'if (camD >= 0.0f) player_.controller().setSpectatorView(spot - f * camD + core::Vec3{0, 4.0f, 0}, yaw0, -0.3f * 9.0f / camD, 0.0f);\n                else player_.controller().setSpectatorView(spot - f * camD + core::Vec3{0, 4.0f, 0}, yaw0 + 3.14159265f, 0.3f * 9.0f / camD, 0.0f);   // ahead, looking back');
fs.writeFileSync(f,s);"
done
echo "trees ready for $H"
