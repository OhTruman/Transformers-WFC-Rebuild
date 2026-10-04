// Clean-room reconstruction — TnObjectiveMarkerTypeSprite.Draw (Canvas markers). See HudMarkers.h.
//
// Rules (RE MILESTONE05 GAMEPLAY §5, BLOCKERS §H; authored setups via build_hud.py):
//   * placement: project MarkerBase + (0, 0, MarkerZOffsetWS); label anchor + LabelZOffsetWS        [CONFIRMED]
//   * off-screen: only with DrawWhenOffscreen; direction from the canvas centre (flipped behind the camera) clamped
//     to the safe frame (SafeFramePaddingPercent); ArrowAngle = acos(dir.y) * sign(dir.x) + 180      [CONFIRMED]
//   * focus: within DistanceFromCenterFocusThreshold x canvas width of the centre, or within AutoFocusRange (UU);
//     held FocusHysterisis seconds after it ends                                                       [CONFIRMED]
//   * sizes: fractions of the canvas width                                                            [CONFIRMED]
//   * material params: FocusParam (Over) 1/0, OnScreenParam 1/0, RotationParam off-screen             [CONFIRMED]
//   * label: MarkerFont, setup LabelColor, LabelOffset (canvas fractions) from the marker; drawn when focused /
//     unfocused / off-screen per the setup flags; unfocused label alpha fades with the remaining hold   [HIGH]
//   * label centring on its anchor                                                                     [PROV]
#include "render/HudMarkers.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include "assets/Json.h"

namespace render {

namespace {
std::string readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
void color(const assets::Json& c, uint8_t out[4]) {
    if (!c.isObject()) return;
    out[0] = (uint8_t)c["R"].asFloat(255); out[1] = (uint8_t)c["G"].asFloat(255);
    out[2] = (uint8_t)c["B"].asFloat(255); out[3] = (uint8_t)c["A"].asFloat(255);
}
}

bool HudMarkers::load(const std::string& root) {
    assets::Json J;
    std::string t = readAll(root + "/_ui/hud_markers.json");
    if (t.empty() || !assets::Json::parse(t, J)) return false;
    for (const auto& kv : J.obj) {
        const assets::Json& e = kv.second;
        Type ty;
        ty.focusThreshold = e["DistanceFromCenterFocusThreshold"].asFloat(0.0625f);
        ty.focusHysteresis = e["FocusHysterisis"].asFloat(0.2f);
        ty.autoFocusRange = e["AutoFocusRange"].asFloat(0.0f);
        ty.safeFrame = e["SafeFramePaddingPercent"].asFloat(0.08f);
        ty.healthBarW = e["HealthBarSize"]["X"].asFloat(0.06f);
        ty.healthBarH = e["HealthBarSize"]["Y"].asFloat(0.015f);
        ty.healthBarOffY = e["HealthBarOffsetFromLabel"]["Y"].asFloat(0.02f);
        std::string f = e["LabelFont"].asString();
        if (!f.empty()) ty.font = f.substr(f.rfind('.') + 1);
        for (const auto& sv : e.obj) {
            const assets::Json& s = sv.second;
            if (!s.isObject() || !s.has("Mat")) continue;
            Setup st;
            st.mat = s["Mat"].asString();
            st.healthMat = s["HealthBarMat"].asString();
            if (s.has("FocusParam")) st.focusParam = s["FocusParam"].asString();
            if (s.has("OnScreenParam")) st.onScreenParam = s["OnScreenParam"].asString();
            if (s.has("RotationParam")) st.rotationParam = s["RotationParam"].asString();
            st.zOffset = s["MarkerZOffsetWS"].asFloat(0.0f);
            st.labelZOffset = s["LabelZOffsetWS"].asFloat(0.0f);
            st.focusedSize = s["FocusedMarkerSize"].asFloat(0.05f);
            st.unfocusedSize = s["UnfocusedMarkerSize"].asFloat(st.focusedSize);
            st.offscreenSize = s["OffscreenMarkerSize"].asFloat(0.0625f);
            st.labelOffX = s["LabelOffset"]["X"].asFloat(0.0f);
            st.labelOffY = s["LabelOffset"]["Y"].asFloat(0.0f);
            st.labelFocused = s["DrawLabelWhenFocused"].asBool(true);
            st.labelUnfocused = s["DrawLabelWhenUnfocused"].asBool(false);
            st.labelOffscreen = s["DrawLabelWhenOffscreen"].asBool(false);
            st.drawOffscreen = s["DrawWhenOffscreen"].asBool(false);
            color(s["LabelColor"], st.labelColor);
            ty.setups[sv.first] = st;
        }
        types_[kv.first] = ty;
    }
    return !types_.empty();
}

void HudMarkers::draw(IRenderer& r, const Camera& cam, int W, int H, const std::vector<MarkerRequest>& markers, float dt) {
    if (W <= 0 || H <= 0) return;
    const core::Mat4 vp = cam.proj() * cam.view();
    const core::Vec3 fwd = core::forwardFromYawPitch(cam.yaw, cam.pitch);
    auto project = [&](const core::Vec3& p, float& sx, float& sy, bool& front) {
        const float* m = vp.m;
        float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        front = w > 0.01f;
        float iw = 1.0f / (std::fabs(w) > 1e-5f ? w : 1e-5f);
        sx = (x * iw * 0.5f + 0.5f) * (float)W;
        sy = (0.5f - y * iw * 0.5f) * (float)H;
    };
    for (auto& kv : focusHold_) kv.second -= dt;
    for (const MarkerRequest& mk : markers) {
        auto ti = types_.find(mk.type);
        if (ti == types_.end()) continue;
        const Type& ty = ti->second;
        auto si = ty.setups.find(mk.setup);
        if (si == ty.setups.end()) continue;
        const Setup& st = si->second;
        core::Vec3 base = mk.base + core::Vec3{0, st.zOffset * 0.01f, 0};
        float sx, sy; bool front;
        project(base, sx, sy, front);
        const bool onScreen = front && sx >= 0 && sx <= W && sy >= 0 && sy <= H;
        // focus
        float dc = std::hypot(sx - W * 0.5f, sy - H * 0.5f);
        float distUU = core::length(base - cam.pos) * 100.0f;
        bool focusNow = onScreen && (dc <= ty.focusThreshold * (float)W || (ty.autoFocusRange > 0 && distUU <= ty.autoFocusRange));
        float& hold = focusHold_[mk.key];
        if (focusNow) hold = ty.focusHysteresis;
        const bool focused = focusNow || hold > 0.0f;
        std::vector<std::pair<std::string, std::array<float, 4>>> params = mk.params;
        if (onScreen) {
            float size = (focused ? st.focusedSize : st.unfocusedSize) * (float)W;
            if (size > 0.0f) {
                IRenderer::MaterialTile t;
                t.material = st.mat; t.w = t.h = size; t.x = sx - size * 0.5f; t.y = sy - size * 0.5f;
                params.push_back({st.focusParam, {focused ? 1.0f : 0.0f, 0, 0, 0}});
                params.push_back({st.onScreenParam, {1, 0, 0, 0}});
                t.params = params;
                r.drawMaterialTile(t);
            }
        } else if (st.drawOffscreen && st.offscreenSize > 0.0f) {
            float dx = sx - W * 0.5f, dy = sy - H * 0.5f;
            if (!front) { dx = -dx; dy = -dy; }
            float len = std::hypot(dx, dy);
            if (len < 1e-3f) { dx = 0; dy = 1; len = 1; }
            dx /= len; dy /= len;
            const float pad = ty.safeFrame, hx = W * (0.5f - pad), hy = H * (0.5f - pad);
            float k = std::min(hx / std::max(std::fabs(dx), 1e-4f), hy / std::max(std::fabs(dy), 1e-4f));
            float size = st.offscreenSize * (float)W;
            IRenderer::MaterialTile t;
            t.material = st.mat; t.w = t.h = size;
            t.x = W * 0.5f + dx * k - size * 0.5f; t.y = H * 0.5f + dy * k - size * 0.5f;
            float ang = std::acos(std::max(-1.0f, std::min(1.0f, dy))) * (dx < 0 ? -1.0f : 1.0f) * 57.29578f + 180.0f;
            params.push_back({st.focusParam, {0, 0, 0, 0}});
            params.push_back({st.onScreenParam, {0, 0, 0, 0}});
            params.push_back({st.rotationParam, {ang, 0, 0, 0}});
            t.params = params;
            r.drawMaterialTile(t);
        }
        // label
        bool drawLabel = onScreen ? (focused ? st.labelFocused : st.labelUnfocused) : (st.drawOffscreen && st.labelOffscreen);
        float labelAlpha = 1.0f;
        if (onScreen && !focusNow && focused && !st.labelUnfocused && ty.focusHysteresis > 0.0f)
            labelAlpha = std::max(0.0f, hold / ty.focusHysteresis);   // fades over the hold
        if (drawLabel && !mk.label.empty() && onScreen) {
            float lz = mk.labelZ >= 0.0f ? mk.labelZ : st.labelZOffset * 0.01f;
            float lx, ly; bool lf;
            project(mk.base + core::Vec3{0, lz, 0}, lx, ly, lf);
            lx += st.labelOffX * (float)W; ly += st.labelOffY * (float)W;
            float tw = 0, th = 0;
            r.canvasTextSize(ty.font, mk.label, tw, th);
            uint8_t c[4] = {st.labelColor[0], st.labelColor[1], st.labelColor[2], (uint8_t)(st.labelColor[3] * labelAlpha)};
            r.drawCanvasText(ty.font, mk.label, lx - tw * 0.5f, ly - th * 0.5f, c);
            if (mk.drawHealthBar && !st.healthMat.empty()) {
                IRenderer::MaterialTile hb;
                hb.material = st.healthMat; hb.w = ty.healthBarW * W; hb.h = ty.healthBarH * W;
                hb.x = lx - hb.w * 0.5f; hb.y = ly + ty.healthBarOffY * W - hb.h * 0.5f;
                hb.params = {{"Health", {mk.health, 0, 0, 0}}};
                r.drawMaterialTile(hb);
            }
        }
        (void)fwd;
    }
}

} // namespace render
