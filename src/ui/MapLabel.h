// PC ADAPTATION (user playtest 2026-10-13: "The map name needs to be centered with the map preview / frame. Right now the
// map name is offset toward the left ... The blue overlay / frame around the map is overlapping the text").
// GameLobby_GFX's selected-map panel: labelAnchor_mc (19, 149, depth 12) holds the map labels (mc_mapLabelMultiplayer:
// mapName_txt, left autoSize, bounds -2..198 with the 2 px gutter, so the text starts at the field's _x), each sliding
// in from -20 / out to +20; frame_mc (depth 16, 434 wide) draws the frame and an additive colour box over the whole
// panel. Here every label's mapName_txt is centred on frame_mc (the slides stay) and labelAnchor_mc moves above frame_mc,
// so the frame never draws over the name. Used by GfxPresenter every frame and by wfc_gfxdump's map-label check.
#pragma once
#include <cmath>
#include <string>

#include "ui/gfx/Display.h"

namespace ui {

// The field x that centres text of width textW on `centre` (both in the label anchor's units).
inline double mapLabelCentredX(double centre, double textW) { return centre - textW * 0.5; }

// The clip holding both labelAnchor_mc and frame_mc (searched below c).
inline gfx::MovieClip* findMapLabelAnchor(gfx::MovieClip* c, int depth = 0) {
    if (!c || depth > 8) return nullptr;
    bool frame = false;
    gfx::MovieClip* anchor = nullptr;
    for (auto& ch : c->children) {
        if (!ch.second || ch.second->kind != gfx::DisplayObject::Kind::Clip || ch.second->removed) continue;
        if (ch.second->name == "frame_mc") frame = true;
        else if (ch.second->name == "labelAnchor_mc") anchor = static_cast<gfx::MovieClip*>(ch.second.get());
    }
    if (frame && anchor) return anchor;
    for (auto& ch : c->children)
        if (ch.second && ch.second->kind == gfx::DisplayObject::Kind::Clip && !ch.second->removed)
            if (gfx::MovieClip* f = findMapLabelAnchor(static_cast<gfx::MovieClip*>(ch.second.get()), depth + 1)) return f;
    return nullptr;
}

struct MapLabelResult {
    bool found = false;
    int raisedTo = -1;              // labelAnchor_mc's new AS depth when it was moved above frame_mc this call
    double frameX = 0, frameW = 0;  // frame_mc in the panel's units
    double anchorX = 0;
};

// Centres every map label on frame_mc and keeps labelAnchor_mc above it. Idempotent (cheap to run every frame).
inline MapLabelResult centreMapLabels(gfx::Player& p) {
    MapLabelResult r;
    gfx::MovieClip* anchor = findMapLabelAnchor(p.root());
    if (!anchor || !anchor->script || !anchor->parent || !anchor->parent->script) return r;
    gfx::avm1::VM& vm = p.vm();
    using V = gfx::avm1::Value;
    const V frame = vm.get(anchor->parent->script, "frame_mc");
    gfx::DisplayObject* frameD = p.resolveTarget("frame_mc", anchor->parent);
    if (!frame.isObject() || !frameD) return r;
    r.found = true;
    if (frameD->depth > anchor->depth) {
        const V next = vm.callMethod(V(anchor->parent->script), "getNextHighestDepth", {});
        vm.callMethod(V(anchor->script), "swapDepths", {next});
        r.raisedTo = (int)vm.toNumber(next);
    }
    r.frameW = vm.toNumber(vm.get(frame.o, "_width"));
    r.frameX = vm.toNumber(vm.get(frame.o, "_x"));
    r.anchorX = vm.toNumber(vm.get(anchor->script, "_x"));
    if (!(r.frameW > 1.0)) return r;
    const double centre = r.frameX + r.frameW * 0.5 - r.anchorX;
    for (auto& ch : anchor->children) {
        if (!ch.second || ch.second->kind != gfx::DisplayObject::Kind::Clip || !ch.second->script) continue;
        const V t = vm.get(ch.second->script, "mapName_txt");
        if (!t.isObject()) continue;
        const double x = mapLabelCentredX(centre, vm.toNumber(vm.get(t.o, "textWidth")));
        if (std::fabs(vm.toNumber(vm.get(t.o, "_x")) - x) > 0.01) vm.set(t.o, "_x", V(x));
    }
    return r;
}

} // namespace ui
