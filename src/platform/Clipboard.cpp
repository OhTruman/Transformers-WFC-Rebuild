#include "platform/Clipboard.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#endif

namespace platform {

bool setClipboardText(const std::string& utf8) {
#ifdef _WIN32
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0 || !OpenClipboard(nullptr)) return false;
    bool ok = false;
    if (EmptyClipboard()) {
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wlen * sizeof(wchar_t))) {
            if (void* p = GlobalLock(h)) {
                MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, (wchar_t*)p, wlen);
                GlobalUnlock(h);
                ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
            }
            if (!ok) GlobalFree(h);   // on success the clipboard owns the memory
        }
    }
    CloseClipboard();
    return ok;
#else
    (void)utf8;
    return false;
#endif
}

} // namespace platform
