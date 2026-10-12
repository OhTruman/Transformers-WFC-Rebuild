// Clean-room reconstruction — optional FSR 3.1 temporal upscaling over D3D12 (PC ADAPTATION, M5 B; OFF by default).
#pragma once
typedef unsigned int GLuint;

namespace render {

// GL names of the textures shared with D3D12 (render size: colour RGBA16F, depth R32F, velocity RG16F, reactive R8;
// display size: output RGBA16F). The renderer writes the inputs, calls fsr3Dispatch, then samples output.
struct Fsr3Targets { GLuint color = 0, depth = 0, velocity = 0, reactive = 0, output = 0; };

struct Fsr3Params {
    float jitterX = 0.0f, jitterY = 0.0f;       // the projection jitter in render pixels, in the shared textures' space
    float mvScaleX = 1.0f, mvScaleY = 1.0f;     // velocity (texture units) -> render pixels, in that space
    float frameTimeMs = 16.7f;
    bool reset = false;                         // camera cut: drop the history
    float nearM = 0.1f, farM = 20000.0f, fovY = 1.0f;
    float sharpness = 0.0f;                     // 0 = no RCAS
};

// (Re)creates the D3D12 device / FFX context / shared targets on first use or a size change. False: FSR 3 unavailable
// (no loader DLL, no D3D12, interop failure) - the caller keeps its other path.
bool fsr3Prepare(int inW, int inH, int outW, int outH, Fsr3Targets* t);
// GL inputs written: GL -> D3D12 upscale -> GL (GPU waits on both sides). False: the dispatch failed this frame.
bool fsr3Dispatch(const Fsr3Params& p);

}  // namespace render
