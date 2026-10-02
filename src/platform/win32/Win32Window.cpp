// Clean-room reconstruction — Win32 window + OpenGL context + input polling.
// This is the ONLY gameplay-facing platform code that touches Win32.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>
#include <xinput.h>

#include "platform/Window.h"
#include "core/Log.h"

namespace platform {
namespace {

// Map a neutral Button to a Win32 virtual key.
int vkFor(Button b) {
    switch (b) {
        case Button::Forward:      return 'W';
        case Button::Back:         return 'S';
        case Button::Left:         return 'A';
        case Button::Right:        return 'D';
        case Button::Jump:         return VK_SPACE;
        case Button::Transform:    return 'F';
        case Button::Fire:         return VK_LBUTTON;
        case Button::Sprint:       return 0;        // no sprint in WFC (no binding)
        case Button::Reload:       return 'R';
        case Button::CameraToggle: return 'C';
        case Button::Debug:        return 'B';
        case Button::Quit:         return VK_ESCAPE;
        case Button::FineAim:      return VK_RBUTTON;
        case Button::Dash:         return VK_SHIFT; // [CONF] Shift = "Ability0 | VehicleSpecialMove";
                                                    // PlayerInCarForm.StartVehicleSpecialMove -> set_DashingInput
        default:                   return 0;
    }
}

class Win32Window final : public IWindow {
public:
    bool create(int w, int h, const char* title) {
        width_ = w; height_ = h;
        hinst_ = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Win32Window::wndProcThunk;
        wc.hInstance = hinst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"WFCRebuildWindow";
        if (!RegisterClassExW(&wc)) { LOG_ERROR("RegisterClassExW failed"); return false; }

        RECT r = {0, 0, w, h};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

        wchar_t wtitle[256];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, wtitle, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, hinst_, this);
        if (!hwnd_) { LOG_ERROR("CreateWindowExW failed"); return false; }

        hdc_ = GetDC(hwnd_);
        if (!initGL()) return false;

        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
        return true;
    }

    ~Win32Window() override {
        if (hglrc_) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(hglrc_); }
        if (hdc_ && hwnd_) ReleaseDC(hwnd_, hdc_);
        if (hwnd_) DestroyWindow(hwnd_);
    }

    bool pump(InputFrame& input) override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) wantClose_ = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (wantClose_) return false;

        // Poll keyboard/mouse buttons via async state (kept in platform layer).
        for (int i = 0; i < (int)Button::Count; ++i) {
            int vk = vkFor((Button)i);
            bool nowDown = vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (!focused_) nowDown = false;
            input.pressed[i] = nowDown && !input.down[i];
            input.down[i] = nowDown;
        }

        // Mouse-look via cursor recentering while captured + focused.
        input.mouseDX = input.mouseDY = 0.0f;
        if (mouseCaptured_ && focused_) {
            POINT p;
            GetCursorPos(&p);
            POINT center = centerScreen();
            input.mouseDX = float(p.x - center.x);
            input.mouseDY = float(p.y - center.y);
            SetCursorPos(center.x, center.y);
        }

        pollGamepad(input);
        return true;
    }

    void present() override { SwapBuffers(hdc_); }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool focused() const override { return focused_; }

    void setTitle(const char* title) override {
        wchar_t w[512];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, w, 512);
        SetWindowTextW(hwnd_, w);
    }

    void setMouseCaptured(bool captured) override {
        if (captured == mouseCaptured_) return;
        mouseCaptured_ = captured;
        ShowCursor(!captured);
        if (captured) { POINT c = centerScreen(); SetCursorPos(c.x, c.y); }
    }

private:
    bool initGL() {
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;
        int pf = ChoosePixelFormat(hdc_, &pfd);
        if (!pf || !SetPixelFormat(hdc_, pf, &pfd)) { LOG_ERROR("SetPixelFormat failed"); return false; }
        hglrc_ = wglCreateContext(hdc_);
        if (!hglrc_ || !wglMakeCurrent(hdc_, hglrc_)) { LOG_ERROR("wglCreateContext failed"); return false; }
        LOG_INFO("OpenGL: %s", (const char*)glGetString(GL_VERSION));
        return true;
    }

    POINT centerScreen() const {
        RECT rc; GetClientRect(hwnd_, &rc);
        POINT c{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
        ClientToScreen(hwnd_, &c);
        return c;
    }

    void pollGamepad(InputFrame& input) {
        XINPUT_STATE st{};
        if (XInputGetState(0, &st) == ERROR_SUCCESS) {
            input.padConnected = true;
            auto axis = [](SHORT v, SHORT dz) -> float {
                if (v > dz) return float(v - dz) / float(32767 - dz);
                if (v < -dz) return float(v + dz) / float(32768 - dz);
                return 0.0f;
            };
            input.padLX = axis(st.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            input.padLY = axis(st.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            input.padRX = axis(st.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            input.padRY = axis(st.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            input.padLT = st.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? st.Gamepad.bLeftTrigger / 255.0f : 0.0f;
        } else {
            input.padConnected = false;
            input.padLX = input.padLY = input.padRX = input.padRY = 0.0f;
            input.padLT = 0.0f;
        }
    }

    static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        Win32Window* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<Win32Window*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self) return self->wndProc(hwnd, msg, wp, lp);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CLOSE: wantClose_ = true; return 0;
            case WM_DESTROY: PostQuitMessage(0); return 0;
            case WM_SIZE:
                width_ = LOWORD(lp); height_ = HIWORD(lp);
                if (width_ < 1) width_ = 1;
                if (height_ < 1) height_ = 1;
                return 0;
            case WM_SETFOCUS: focused_ = true; return 0;
            case WM_KILLFOCUS: focused_ = false; return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HINSTANCE hinst_ = nullptr;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
    int width_ = 0, height_ = 0;
    bool focused_ = true;
    bool wantClose_ = false;
    bool mouseCaptured_ = false;
};

} // namespace

IWindow* createWindow(int width, int height, const char* title) {
    auto* w = new Win32Window();
    if (!w->create(width, height, title)) { delete w; return nullptr; }
    return w;
}

} // namespace platform
#endif // _WIN32
