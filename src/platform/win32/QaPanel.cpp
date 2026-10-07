// Clean-room reconstruction — DEBUG-ONLY QA panel, Win32 (NOT ORIGINAL). A plain tool window with list boxes; its
// messages are dispatched by the game window's thread-wide message pump.
#include "platform/QaPanel.h"

#if WFC_DEV_TOOLS
#include <windows.h>

#include <cstdlib>

namespace platform {
namespace {

enum : int { kMaps = 101, kModes, kChars, kWeapons, kLaunch, kRestart, kTitle, kStatus, kRespawn, kNextStart, kNoclip, kGod, kDummy, kSwap,
             kBotOverlay, kFreezeBots, kKillBots, kTeleportAim, kBotList };

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
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 40, 40, 760, 640, nullptr, nullptr, wc.hInstance, this);
        auto label = [&](const wchar_t* t, int x) {
            CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE, x, 8, 170, 18, hwnd_, nullptr, wc.hInstance, nullptr);
        };
        auto list = [&](int id, int x) {
            return CreateWindowW(L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, x, 28, 175, 220, hwnd_,
                                 (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        };
        auto button = [&](const wchar_t* t, int id, int x) {
            CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 252, 140, 28, hwnd_, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        };
        label(L"Map", 8); label(L"Mode", 192); label(L"Class", 376); label(L"Weapon (Gameplay)", 560);
        lists_[0] = list(kMaps, 8); lists_[1] = list(kModes, 192); lists_[2] = list(kChars, 376); lists_[3] = list(kWeapons, 560);
        button(L"Launch", kLaunch, 8); button(L"Restart last", kRestart, 156); button(L"Back to title", kTitle, 304);
        // in-match tools (Gameplay QA API; no-ops when unavailable)
        auto small = [&](const wchar_t* t, int id, int x) {
            CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 316, 140, 26, hwnd_, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        };
        small(L"Respawn", kRespawn, 8); small(L"Next start", kNextStart, 156); small(L"Noclip on/off", kNoclip, 304);
        small(L"God mode on/off", kGod, 452); small(L"Spawn dummy", kDummy, 600);
        // live character swap (Gameplay World::qaSetCharacter): the class picked in the character list
        CreateWindowW(L"BUTTON", L"Swap to selected character", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 8, 348, 290, 26, hwnd_,
                      (HMENU)(INT_PTR)kSwap, wc.hInstance, nullptr);
        CreateWindowW(L"BUTTON", L"Teleport to aim point", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 304, 348, 290, 26, hwnd_,
                      (HMENU)(INT_PTR)kTeleportAim, wc.hInstance, nullptr);
        // bot debugging (DEV TOOL; Gameplay World qa* bot calls, no-ops when unavailable)
        CreateWindowW(L"STATIC", L"Bots (DEV TOOL)", WS_CHILD | WS_VISIBLE, 8, 386, 200, 18, hwnd_, nullptr, wc.hInstance, nullptr);
        auto botButton = [&](const wchar_t* t, int id, int x) {
            CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 406, 180, 26, hwnd_, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        };
        botButton(L"Bot overlay on/off", kBotOverlay, 8); botButton(L"Freeze bots on/off", kFreezeBots, 196);
        botButton(L"Kill all bots", kKillBots, 384);
        botList_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                 8, 438, 730, 160, hwnd_, (HMENU)(INT_PTR)kBotList, wc.hInstance, nullptr);
        SendMessageW(botList_, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
        status_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 8, 286, 730, 26, hwnd_, (HMENU)(INT_PTR)kStatus, wc.hInstance, nullptr);
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
    void setMaps(const std::vector<Option>& maps) override {
        const std::string keep = selected(0);
        options_[0] = maps;
        SendMessageW(lists_[0], LB_RESETCONTENT, 0, 0);
        int sel = 0;
        for (size_t i = 0; i < maps.size(); ++i) {
            SendMessageW(lists_[0], LB_ADDSTRING, 0, (LPARAM)widen(maps[i].label).c_str());
            if (maps[i].value == keep) sel = (int)i;
        }
        if (!maps.empty()) SendMessageW(lists_[0], LB_SETCURSEL, (WPARAM)sel, 0);
    }
    void setBots(const std::string& text) override {
        if (text == botText_) return;   // unchanged: keep the scroll position
        botText_ = text;
        std::wstring w;
        for (char c : text) { if (c == '\n') w += L'\r'; w += (wchar_t)(unsigned char)c; }   // the edit control wants CRLF
        SetWindowTextW(botList_, w.c_str());
    }

private:
    static std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
    std::string selected(int i) const {
        LRESULT k = SendMessageW(lists_[i], LB_GETCURSEL, 0, 0);
        return k >= 0 && (size_t)k < options_[i].size() ? options_[i][(size_t)k].value : std::string();
    }
    void command(int id) {
        QaRequest r;
        switch (id) {
        case kLaunch: r.kind = QaRequest::Kind::Launch; break;
        case kRestart: r.kind = QaRequest::Kind::Restart; break;
        case kTitle: r.kind = QaRequest::Kind::Title; break;
        case kRespawn: r.kind = QaRequest::Kind::Respawn; break;
        case kNextStart: r.kind = QaRequest::Kind::NextStart; break;
        case kNoclip: r.kind = QaRequest::Kind::Noclip; break;
        case kGod: r.kind = QaRequest::Kind::God; break;
        case kDummy: r.kind = QaRequest::Kind::Dummy; break;
        case kSwap: r.kind = QaRequest::Kind::SwapCharacter; break;
        case kBotOverlay: r.kind = QaRequest::Kind::BotOverlay; break;
        case kFreezeBots: r.kind = QaRequest::Kind::FreezeBots; break;
        case kKillBots: r.kind = QaRequest::Kind::KillBots; break;
        case kTeleportAim: r.kind = QaRequest::Kind::TeleportAim; break;
        default: return;
        }
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
        if (self && m == WM_COMMAND && HIWORD(w) == LBN_SELCHANGE && LOWORD(w) == kModes) {   // the map list follows the mode
            QaRequest r;
            r.kind = QaRequest::Kind::ModeChanged;
            r.mode = self->selected(1);
            self->pending_ = r;
            return 0;
        }
        if (self && m == WM_CLOSE) { self->show(false); return 0; }   // closing only hides it
        // F10 while the panel has focus hides it (otherwise the system key would open its window menu)
        if (self && m == WM_SYSKEYDOWN && w == VK_F10) { self->show(false); return 0; }
        if (m == WM_SYSKEYUP && w == VK_F10) return 0;
        return DefWindowProcW(h, m, w, l);
    }
    HWND hwnd_ = nullptr, status_ = nullptr, botList_ = nullptr;
    std::string botText_;
    HWND lists_[4] = {};
    std::vector<Option> options_[4];
    QaRequest pending_;
    bool visible_ = false;
};

} // namespace

std::unique_ptr<QaPanel> createQaPanel() { return std::make_unique<Win32QaPanel>(); }

} // namespace platform
#else   // shipping-style build: no panel
namespace platform {
std::unique_ptr<QaPanel> createQaPanel() { return nullptr; }

} // namespace platform
#endif   // WFC_DEV_TOOLS
