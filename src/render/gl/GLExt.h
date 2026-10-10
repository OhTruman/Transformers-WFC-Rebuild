// Clean-room reconstruction — GL 2.0..3.3 entry points loaded at runtime (no extension library).
// Only the renderer includes this; platform specifics stay in GLExt.cpp.
#pragma once
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>
#include <cstddef>

#ifndef APIENTRY
#define APIENTRY
#endif

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_RGBA16F 0x881A
#ifndef GL_RGB32F
#define GL_RGB32F 0x8815
#endif
#define GL_HALF_FLOAT 0x140B
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508
#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864F
#endif

namespace glx {

#ifdef MemoryBarrier            // winnt.h's MemoryBarrier macro (_mm_mfence) would rename glMemoryBarrier below
#undef MemoryBarrier
#endif
#define WFC_GL_FUNCS(X) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(GLboolean, IsProgram, (GLuint)) \
    X(GLboolean, IsBuffer, (GLuint)) \
    X(GLboolean, IsFramebuffer, (GLuint)) \
    X(GLboolean, IsVertexArray, (GLuint)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar*)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform2fv, (GLint, GLsizei, const GLfloat*)) \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Uniform1iv, (GLint, GLsizei, const GLint*)) \
    X(void, Uniform3fv, (GLint, GLsizei, const GLfloat*)) \
    X(void, Uniform4fv, (GLint, GLsizei, const GLfloat*))     X(void, Uniform4uiv, (GLint, GLsizei, const GLuint*)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, GenFramebuffers, (GLsizei, GLuint*)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint*)) \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void, VertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, StencilOpSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))     X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, DeleteProgram, (GLuint))

// Optional entry points (diagnostics: KHR_debug / GL 4.3 debug output, GL 4.5 / ARB_robustness reset status).
// Missing ones stay null and never fail load().
typedef void(APIENTRY* GLDEBUGPROCWFC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                       const GLchar* message, const void* userParam);
#define WFC_GL_OPT_FUNCS(X) \
    X(void, DebugMessageCallback, (GLDEBUGPROCWFC, const void*)) \
    X(void, DebugMessageControl, (GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean)) \
    X(GLenum, GetGraphicsResetStatus, (void)) \
    X(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void*, GLsizei)) \
    X(void, MultiDrawElementsIndirect, (GLenum, GLenum, const void*, GLsizei, GLsizei)) \
    X(void, VertexAttribDivisor, (GLuint, GLuint)) \
    X(void, TexStorage3D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei)) \
    X(void, TexBuffer, (GLenum, GLenum, GLuint)) \
    X(void, DispatchCompute, (GLuint, GLuint, GLuint)) \
    X(void, MemoryBarrier, (GLbitfield)) \
    X(void, BindBufferBase, (GLenum, GLuint, GLuint)) \
    X(void, BindImageTexture, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum)) \
    X(void, Uniform1ui, (GLint, GLuint)) \
    X(void, Uniform2i, (GLint, GLint, GLint)) \
    X(void, GetBufferSubData, (GLenum, GLintptr, GLsizeiptr, void*)) \
    X(void, BufferStorage, (GLenum, GLsizeiptr, const void*, GLbitfield)) \
    X(void*, MapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield)) \
    X(void, CopyImageSubData, (GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei)) \
    X(void, TextureView, (GLuint, GLenum, GLuint, GLenum, GLuint, GLuint, GLuint, GLuint)) \
    X(void, CompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*)) \
    X(void, GenQueries, (GLsizei, GLuint*)) \
    X(void, DeleteQueries, (GLsizei, const GLuint*)) \
    X(void, BeginQuery, (GLenum, GLuint)) \
    X(void, EndQuery, (GLenum)) \
    X(void, GetQueryObjectiv, (GLuint, GLenum, GLint*)) \
    X(void, GetQueryObjectui64v, (GLuint, GLenum, unsigned long long*))     X(void, QueryCounter, (GLuint, GLenum))

#define WFC_GL_DECL(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; extern PFN_##name name;
WFC_GL_FUNCS(WFC_GL_DECL)
WFC_GL_OPT_FUNCS(WFC_GL_DECL)
#undef WFC_GL_DECL

// Load all entry points (requires a current context). Returns false if any is missing.
bool load();
// Redundant uniform elimination (installed by load): forget the current program at frame boundaries (other
// components may bind programs through their own loaders); counters since the last call.
void uniformCacheForgetCurrent();
void uniformCacheStats(unsigned long long& sent, unsigned long long& skipped);
// The value program `prog` holds at `loc` as last set through the cache: 1 = copied (`words` 32-bit words),
// 0 = never set since link (GL default 0), -1 = unknown (cache off / size mismatch).
int uniformCacheGet(GLuint prog, GLint loc, void* out, unsigned words);
bool uniformCacheActive();

// M43 stability diagnostics (always on): driver debug output (errors / undefined behaviour / high severity, rate
// limited, WFC_GLDEBUG=all for every message, WFC_GLDEBUG=sync for synchronous call stacks) and the context reset
// status. Counters are read by the renderer's diagnostics.
void installDebugOutput();
struct DebugCounts { unsigned errors = 0, undefined = 0, high = 0, medium = 0, other = 0; };
const DebugCounts& debugCounts();
// GL_NO_ERROR, or the reset status once the driver reports a lost context (logged once).
GLenum pollResetStatus();
// GPU frame time (GL_TIME_ELAPSED ring of 3, read two frames late: never stalls). frameBegin / frameEnd bracket the
// whole frame; a frame whose GPU time exceeds 250 ms is logged (Windows resets the driver on ~2 s of GPU work).
void gpuTimerBegin();
void gpuTimerEnd();
double lastGpuFrameMs();
long gpuFrameReads();
// Per-pass GPU timestamps (GL_TIMESTAMP) in the same 3-frame ring: mark k (1..5) at a pass boundary of the current
// frame; mark 0 is the frame start. lastGpuPassMs(k) = GPU time from the previous available mark to mark k of the
// frame lastGpuFrameMs belongs to (-1 if not marked that frame).
enum GpuPass { kPassWorld = 1, kPassCaller = 2, kPassMapFx = 3, kPassTranslucent = 4, kPassPost = 5, kPassCount = 6 };
void gpuMark(int k);
double lastGpuPassMs(int k);           // increments with each new lastGpuFrameMs value (3 frames after the measured frame)
double lastGpuFrameCpuMs();   // CPU time between the same markers (gpu ~ cpu: the GPU waited on submission)
// WFC_SLOWFRAME support (no stalls): the index of the frame being recorded (gpuTimerBegin .. gpuTimerEnd) and of the
// frame lastGpuFrameMs / lastGpuPassMs belong to; the CPU time from gpuTimerBegin to each pass mark of the current
// frame (ms, -1 = not marked); cumulative program binds (UseProgram calls through the uniform cache) and buffer
// upload bytes (BufferData / BufferSubData).
long gpuFrameIndex();
long lastGpuFrameIndex();
double cpuPassMark(int k);
unsigned long long programBinds();
unsigned long long bufferUploadBytes();

// Redundant texture-bind elimination (300+ fps lobbies: bindCommon re-binds every material slot + the shadow-mask
// unit per draw). glBindTexture / glDeleteTextures in every file that includes this header go through a per-unit
// cache of the 2D / cube / 2D-array bindings, and ActiveTexture is wrapped at load: a bind of the texture a unit
// already holds is skipped (cannot change any image). The cache is invalidated at load, at the renderer's frame
// boundaries (other GL users draw between frames) and after glPopAttrib; deleting a texture resets the units that
// held it to 0 (GL semantics). WFC_NOTEXCACHE=1 = A/B (every bind issued).
void cachedBindTexture(GLenum target, GLuint texture);
void cachedDeleteTextures(GLsizei n, const GLuint* textures);
void textureCacheInvalidate();
void textureCacheStats(unsigned long long& issued, unsigned long long& skipped);
// WFC_TEXTRACE=1 (leak hunting): glGenTextures in every file including this header records its file:line;
// textureTraceDump logs the live textures per creation site and which sites grew since the previous dump.
void tracedGenTextures(GLsizei n, GLuint* textures, const char* file, int line);
void textureTraceDump(const char* tag);

// a uniform location for a string-literal name, cached per program until relink / delete (see GLExt.cpp)
GLint cachedUniformLocation(GLuint program, const char* literalName);
void uploadProfDump(long frames);                  // WFC_UPLOADPROF: top buffer-upload callers since the last dump
bool locCheckOn();                                 // WFC_LOCCHECK: cached uniform locations compared with the driver
void locCheckCount(bool same);
unsigned long long firstProgramBinds();             // programs bound for the first time since their link (stall trace)
unsigned long long bufferAllocs();                  // BufferData calls (new storage) and their bytes
unsigned long long bufferAllocBytes();
} // namespace glx

#ifndef WFC_NO_TEXCACHE_MACROS
#define glBindTexture glx::cachedBindTexture
#define glDeleteTextures glx::cachedDeleteTextures
#define glGenTextures(n, t) glx::tracedGenTextures((n), (t), __FILE__, __LINE__)
#endif
