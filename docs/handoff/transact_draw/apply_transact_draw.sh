#!/bin/bash
# Rendering: the transforming doors / bridges draw, on top of Gameplay's transforming actors (TransformingActors.{h,cpp} + their
# edit_transforming_actors.js already applied). Run from a tree root: copies TransformingActorsDraw.{h,cpp} to src/game/ and adds
# the World member + the World::draw call. Anchors must match exactly once.
set -e
H="$(cd "$(dirname "$0")" && pwd)"
cp "$H/TransformingActorsDraw.h" "$H/TransformingActorsDraw.cpp" src/game/
one() { [ "$(grep -c "$1" "$2")" = 1 ] || { echo "anchor not unique / missing in $2: $1"; exit 1; }; }
one '^#include "game/TransformingActors.h"$' src/game/World.h
one "^    TransformingActors transforming_;" src/game/World.h
one '^    syncMapPresentation(r);$' src/game/World.cpp
sed -i 's|^#include "game/TransformingActors.h"$|&\n#include "game/TransformingActorsDraw.h"|' src/game/World.h
sed -i "s|^    TransformingActors transforming_;.*|&\n    TransformingActorsDraw transformingDraw_;   // (Rendering) their skeletal meshes posed at t, drawn each frame|" src/game/World.h
sed -i 's|^    syncMapPresentation(r);$|&\n    transformingDraw_.draw(transforming_, r);   // the map'"'"'s transforming doors / bridges (posed at t; nothing on maps without them)|' src/game/World.cpp
echo "transact draw applied"
