#include "ui/UiGL.h"
#include "render/gl/GLExt.h"

namespace ui {

void beginScreenFrame(int width, int height) {
    if (glx::BindFramebuffer) glx::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

} // namespace ui
