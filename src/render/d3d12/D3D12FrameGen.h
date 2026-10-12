// Clean-room reconstruction — optional FSR 3 frame generation through the D3D12 presenter (PC ADAPTATION, M5 C; OFF by
// default). The presenter's swapchain is AMD's frame-generation swapchain; per real frame the renderer hands it the
// HUD-less image (after post and the HUD screen effects, before Canvas / GFx UI), depth and motion vectors, and FSR 3
// interpolates a frame between consecutive real ones (UI extracted from the HUD-less difference and composited on both).
#pragma once
typedef unsigned int GLuint;

namespace render {

// GL names of this frame's shared inputs (rows top-down, D3D's orientation): HUD-less colour (display size, RGBA8),
// depth (render size, R32F) and motion vectors (render size, RG16F: prevUV - curUV with +y down).
struct FgInputs { GLuint hudless = 0, depth = 0, velocity = 0; };

struct FgParams {
    int renderW = 0, renderH = 0;
    float jitterX = 0.0f, jitterY = 0.0f;       // render pixels, +y down (0 without temporal upscaling)
    float frameTimeMs = 16.7f;
    bool reset = false;                         // camera cut
    float nearM = 0.1f, farM = 20000.0f, fovY = 1.0f;
    float camPos[3] = {}, camUp[3] = {0, 1, 0}, camRight[3] = {1, 0, 0}, camFwd[3] = {0, 0, -1};
};

bool framegenActive();                                       // the presenter runs the FG swapchain
// Before writing this frame's inputs: the textures of the ring slot the next present uses (its previous D3D12 reads
// are complete). False: no FG inputs this frame (FG then shows real frames only).
bool framegenInputs(int renderW, int renderH, FgInputs* out);
void framegenParams(const FgParams& p);                      // this frame's camera / timing (after framegenInputs)
// The renderer's frame start (steady_clock, ns since epoch): the presenter reports frame start -> Present returned (D3D12
// present / FG; a CPU-side latency proxy, display scan-out not included).
void presentFrameStart(long long steadyNs);

}  // namespace render
