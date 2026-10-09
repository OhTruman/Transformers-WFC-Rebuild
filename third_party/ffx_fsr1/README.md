AMD FidelityFX Super Resolution 1.0 (FSR 1): ffx_a.h and ffx_fsr1.h, unmodified, from
https://github.com/GPUOpen-Effects/FidelityFX-FSR (ffx-fsr/), MIT licence (LICENSE.txt).
Used for the optional spatial upscaler (EASU) + sharpening (RCAS): the shader side is embedded by
tools/render/embed_ffx.py into src/render/gl/FfxFsr1Source.inc; the CPU side (constants) is compiled from the
headers in src/render/gl/WfcFsr.cpp.
