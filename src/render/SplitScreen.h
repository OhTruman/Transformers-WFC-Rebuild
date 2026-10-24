// Clean-room reconstruction — split screen views (PC EXTENSION: the original has one local player and no split screen,
// docs/handoff/SPLITSCREEN_RE.md; layouts and title-safe numbers from the stock Engine.Default__GameViewportClient CDO).
//
// Frame protocol with two local players (one view = today's frame, unchanged):
//   r.setSplitLayout(SplitLayout::Horizontal);            // once, when the second seat joins (Single when it leaves)
//   r.beginFrame(cams[0], W, H);                           // as today (view 0's camera)
//   for (int v = 0; v < r.viewCount(); ++v) {
//       r.beginView(v, cams[v]);                           // every draw call until the next beginView goes to view v
//       world.draw(r, seat v); markers for seat v;         // per-view submissions (culling / LOD / sprites face this camera)
//   }
//   r.endFrame();                                          // post-process + RT per view, composited into the window
//   HUD: one movie per seat, drawn into splitViewPixels(layout, v, W, H) (title-safe: titleSafe(rect))
// Each view keeps its own temporal history (TAA, RT denoisers, exposure, motion), post-process volume (the volume of that
// view's point) and matinee fade / colour scale. Audio is not the renderer's (one listener per seat, Systems).
#pragma once
#include <algorithm>
#include <cmath>

namespace render {

// stock UE3 ESplitScreenType for one / two players (eSST_NONE, eSST_2P_HORIZONTAL - the stock 2P default -, eSST_2P_VERTICAL)
enum class SplitLayout { Single = 0, Horizontal = 1, Vertical = 2 };

// a view in normalized window space, origin top-left (the CDO's SplitscreenInfo origin / size)
struct ViewRect {
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
};

struct ViewPixels {
    int x = 0, y = 0, w = 0, h = 0;   // window pixels, origin top-left
};

inline int splitViewCount(SplitLayout l) { return l == SplitLayout::Single ? 1 : 2; }

// CDO: Horizontal P1 (0, 0, 1, 0.5) top, P2 (0, 0.5, 1, 0.5) bottom; Vertical P1 (0, 0, 0.5, 1) left, P2 (0.5, 0, 0.5, 1)
inline ViewRect splitViewRect(SplitLayout l, int view) {
    ViewRect r;
    if (l == SplitLayout::Horizontal) { r.h = 0.5f; r.y = view == 1 ? 0.5f : 0.0f; }
    else if (l == SplitLayout::Vertical) { r.w = 0.5f; r.x = view == 1 ? 0.5f : 0.0f; }
    return r;
}

// pixel rect of a view: edges rounded once so the two views tile the window exactly (no gap / overlap at odd sizes)
inline ViewPixels splitViewPixels(SplitLayout l, int view, int windowW, int windowH) {
    const ViewRect r = splitViewRect(l, view);
    ViewPixels p;
    p.x = (int)std::lround(r.x * (float)windowW);
    p.y = (int)std::lround(r.y * (float)windowH);
    p.w = (int)std::lround((r.x + r.w) * (float)windowW) - p.x;
    p.h = (int)std::lround((r.y + r.h) * (float)windowH) - p.y;
    return p;
}

// the view's camera aspect (the camera keeps its horizontal FOV - UE3 convention - so a 32:9 top / bottom view is wide and
// short and an 8:9 left / right view is narrow and tall, as stock UE3 splits)
inline float splitViewAspect(SplitLayout l, int view, int windowW, int windowH) {
    const ViewPixels p = splitViewPixels(l, view, windowW, windowH);
    return p.h > 0 ? (float)p.w / (float)p.h : 1.0f;
}

// title-safe area inside a view (CDO TitleSafeZone: MaxPercent 0.9, RecommendedPercent 0.8 - applied per view, stock)
constexpr float kTitleSafeMax = 0.9f;
constexpr float kTitleSafeRecommended = 0.8f;
inline ViewPixels titleSafe(const ViewPixels& v, float percent = kTitleSafeMax) {
    const float pc = std::min(std::max(percent, 0.0f), 1.0f);
    ViewPixels s;
    s.w = (int)std::lround((float)v.w * pc);
    s.h = (int)std::lround((float)v.h * pc);
    s.x = v.x + (v.w - s.w) / 2;
    s.y = v.y + (v.h - s.h) / 2;
    return s;
}

} // namespace render
