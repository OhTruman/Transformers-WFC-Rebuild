// Clean-room reconstruction — DEBUG-ONLY QA panel, Win32 (NOT ORIGINAL). A plain tool window with list boxes; its
// messages are dispatched by the game window's thread-wide message pump.
#include "platform/QaPanel.h"

#include <windows.h>

#include <cstdlib>

namespace platform {
namespace {

enum : int { kMaps = 101, kModes, kChars, kWeapons, kLaunch, kRestart, kTitle, kStatus };

class Win32QaPanel : public QaPanel {
public:
    Win32QaPanel() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &Win32QaPanel::proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"WfcQaPanel";
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, L"WFC QA (debug only - not original)",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 40, 40, 760, 360, nullptr, nullptr, wc.hInstance, this);
        auto label = [&](const wchar_t* t, int x) {
            CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE, x, 8, 170, 18, hwnd_, nullptr, wc.hInstance, nullptr);
        };
        auto list = [&](int id, int x) {
            return CreateWindowW(L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, x, 28, 175, 220, hwnd_,
                                 (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        };
        auto button = [&](const wchar_t* t, int id, int x) {
            CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 256, 140, 28, hwnd_, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        };
        label(L"Map", 8); label(L"Mode", 192); label(L"Class", 376); label(L"Weapon (Gameplay)", 560);
        lists_[0] = list(kMaps, 8); lists_[1] = list(kModes, 192); lists_[2] = list(kChars, 376); lists_[3] = list(kWeapons, 560);
        button(L"Launch", kLaunch, 8); button(L"Restart last", kRestart, 156); button(L"Back to title", kTitle, 304);
        status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 8, 292, 730, 20, hwnd_, (HMENU)(INT_PTR)kStatus, wc.hInstance, nullptr);
    }
    ~Win32QaPanel() override { if (hwnd_) DestroyWindow(hwnd_); }

    void setOptions(const std::vector<Option>& maps, const std::vector<Option>& modes, const std::vector<Option>& characters,
                    const std::vector<Option>& weapons) override {
        const std::vector<Option>* all[4] = {&maps, &modes, &characters, &weapons};
        for (int i = 0; i < 4; ++i) {
            options_[i] = *all[i];
            SendMessageW(lists_[i], LB_RESETCONTENT, 0, 0);
            for (const Option& o : options_[i]) SendMessageW(lists_[i], LB_ADDSTRING, 0, (LPARAM)widen(o.label).c_str());
            if (i < 3 && !options_[i].empty()) SendMessageW(lists_[i], LB_SETCURSEL, 0, 0);
        }
    }
    void show(bool v) override { ShowWindow(hwnd_, v ? SW_SHOWNOACTIVATE : SW_HIDE); visible_ = v; }
    bool visible() const override { return visible_; }
    void setStatus(const std::string& text) override { SetWindowTextW(status_, widen(text).c_str()); }
    QaRequest poll() override { QaRequest r = pending_; pending_ = QaRequest{}; return r; }

private:
    static std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
    std::string selected(int i) const {
        LRESULT k = SendMessageW(lists_[i], LB_GETCURSEL, 0, 0);
        return k >= 0 && (size_t)k < options_[i].size() ? options_[i][(size_t)k].value : std::string();
    }
    void command(int id) {
        if (id != kLaunch && id != kRestart && id != kTitle) return;
        QaRequest r;
        r.kind = id == kLaunch ? QaRequest::Kind::Launch : id == kRestart ? QaRequest::Kind::Restart : QaRequest::Kind::Title;
        r.mapId = std::atoi(selected(0).c_str());
        r.mode = selected(1);
        r.character = selected(2);
        r.weapon = selected(3);
        pending_ = r;
    }
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
        auto* self = (Win32QaPanel*)GetWindowLongPtrW(h, GWLP_USERDATA);
        if (self && m == WM_COMMAND && HIWORD(w) == BN_CLICKED) { self->command(LOWORD(w)); return 0; }
        if (self && m == WM_CLOSE) { self->show(false); return 0; }   // closing only hides it
        return DefWindowProcW(h, m, w, l);
    }
    HWND hwnd_ = nullptr, status_ = nullptr;
    HWND lists_[4] = {};
    std::vector<Option> options_[4];
    QaRequest pending_;
    bool visible_ = false;
};

} // namespace

std::unique_ptr<QaPanel> createQaPanel() { return std::make_unique<Win32QaPanel>(); }

} // namespace platform
