// DEV TOOL (WFC_FILELOG=<file>): logs every file the game's own code opens, deduplicated, one line per path:
//   "<r|w> <ok|missing> <full path>"
// for the package manifest check (only files the runtime reads ship). There is no single read choke point (std::ifstream,
// fopen, CreateFile across every lane), but all of them go through the exe's import table: at startup this patches the
// exe's own imports of fopen / _wfopen / _fsopen / _wfsopen / _open / _wopen / CreateFileA / CreateFileW. Opens made inside
// other DLLs (the GL driver, system libraries) are not logged. Self-installing (a static initializer); with the variable
// unset nothing is patched and nothing runs.
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unordered_set>

namespace {

using FopenFn = FILE*(__cdecl*)(const char*, const char*);
using WfopenFn = FILE*(__cdecl*)(const wchar_t*, const wchar_t*);
using FsopenFn = FILE*(__cdecl*)(const char*, const char*, int);
using WfsopenFn = FILE*(__cdecl*)(const wchar_t*, const wchar_t*, int);
using OpenFn = int(__cdecl*)(const char*, int, ...);
using WopenFn = int(__cdecl*)(const wchar_t*, int, ...);
using CreateAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

FopenFn oFopen; WfopenFn oWfopen; FsopenFn oFsopen; WfsopenFn oWfsopen; OpenFn oOpen; WopenFn oWopen;
CreateAFn oCreateA; CreateWFn oCreateW;
HANDLE gOut = INVALID_HANDLE_VALUE;
std::mutex gMx;
std::unordered_set<std::wstring>* gSeen;   // leaked: opens may happen during static destruction
thread_local int tInHook = 0;

void record(const wchar_t* path, bool write, bool ok) {
    if (!path || !*path || gOut == INVALID_HANDLE_VALUE || tInHook) return;
    if (std::wcsncmp(path, L"\\\\.\\", 4) == 0 || std::wcsncmp(path, L"CON", 3) == 0 || std::wcsncmp(path, L"NUL", 3) == 0) return;
    ++tInHook;
    wchar_t full[1024];
    const DWORD n = GetFullPathNameW(path, 1024, full, nullptr);
    std::wstring key(write ? L"w " : L"r ");
    key += ok ? L"ok " : L"missing ";
    key += (n > 0 && n < 1024) ? full : path;
    {
        std::lock_guard<std::mutex> lk(gMx);
        if (gSeen->insert(key).second) {
            char utf8[4096];
            const int len = WideCharToMultiByte(CP_UTF8, 0, key.c_str(), (int)key.size(), utf8, (int)sizeof utf8 - 2, nullptr, nullptr);
            if (len > 0) {
                utf8[len] = '\n';
                DWORD wr = 0;
                WriteFile(gOut, utf8, (DWORD)len + 1, &wr, nullptr);
            }
        }
    }
    --tInHook;
}
void recordA(const char* path, bool write, bool ok) {
    if (!path) return;
    wchar_t w[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) > 0 || MultiByteToWideChar(CP_ACP, 0, path, -1, w, 1024) > 0) record(w, write, ok);
}
bool modeWrites(const char* m) { return m && (std::strpbrk(m, "wa+") != nullptr); }
bool modeWritesW(const wchar_t* m) { return m && (std::wcspbrk(m, L"wa+") != nullptr); }
bool flagsWrite(int f) { return (f & (_O_WRONLY | _O_RDWR | _O_APPEND | _O_CREAT)) != 0; }

FILE* __cdecl hFopen(const char* p, const char* m) { FILE* f = oFopen(p, m); recordA(p, modeWrites(m), f != nullptr); return f; }
FILE* __cdecl hWfopen(const wchar_t* p, const wchar_t* m) { FILE* f = oWfopen(p, m); record(p, modeWritesW(m), f != nullptr); return f; }
FILE* __cdecl hFsopen(const char* p, const char* m, int s) { FILE* f = oFsopen(p, m, s); recordA(p, modeWrites(m), f != nullptr); return f; }
FILE* __cdecl hWfsopen(const wchar_t* p, const wchar_t* m, int s) { FILE* f = oWfsopen(p, m, s); record(p, modeWritesW(m), f != nullptr); return f; }
int __cdecl hOpen(const char* p, int fl, int pm) { const int r = oOpen(p, fl, pm); recordA(p, flagsWrite(fl), r >= 0); return r; }
int __cdecl hWopen(const wchar_t* p, int fl, int pm) { const int r = oWopen(p, fl, pm); record(p, flagsWrite(fl), r >= 0); return r; }
HANDLE WINAPI hCreateA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
    HANDLE h = oCreateA(p, a, s, sa, d, f, t);
    if (!(f & FILE_FLAG_BACKUP_SEMANTICS)) recordA(p, (a & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0, h != INVALID_HANDLE_VALUE);
    return h;
}
HANDLE WINAPI hCreateW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
    HANDLE h = oCreateW(p, a, s, sa, d, f, t);
    if (!(f & FILE_FLAG_BACKUP_SEMANTICS)) record(p, (a & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0, h != INVALID_HANDLE_VALUE);
    return h;
}

// Replace one imported function in the exe's import table; returns how many slots were patched.
template <class F> int patch(const char* name, F hook, F& original) {
    auto* base = (unsigned char*)GetModuleHandleW(nullptr);
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int n = 0;
    for (auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (!imp->OriginalFirstThunk) continue;
        auto* names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        auto* slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (std::strcmp((const char*)ibn->Name, name) != 0) continue;
            DWORD old = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            if (!original) original = (F)(void*)slots->u1.Function;
            slots->u1.Function = (ULONG_PTR)(void*)hook;
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            ++n;
        }
    }
    return n;
}

struct Installer {
    Installer() {
        const char* out = std::getenv("WFC_FILELOG");
        if (!out || !*out) return;
        gOut = CreateFileA(out, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (gOut == INVALID_HANDLE_VALUE) return;
        gSeen = new std::unordered_set<std::wstring>();
        int n = 0;
        n += patch("fopen", (FopenFn)hFopen, oFopen);
        n += patch("_wfopen", (WfopenFn)hWfopen, oWfopen);
        n += patch("_fsopen", (FsopenFn)hFsopen, oFsopen);
        n += patch("_wfsopen", (WfsopenFn)hWfsopen, oWfsopen);
        n += patch("_open", (OpenFn)(void*)hOpen, oOpen);
        n += patch("_wopen", (WopenFn)(void*)hWopen, oWopen);
        n += patch("CreateFileA", (CreateAFn)hCreateA, oCreateA);
        n += patch("CreateFileW", (CreateWFn)hCreateW, oCreateW);
        char line[96];
        const int len = std::snprintf(line, sizeof line, "# WFC_FILELOG: %d import slots hooked\n", n);
        DWORD wr = 0;
        WriteFile(gOut, line, (DWORD)len, &wr, nullptr);
    }
} gInstaller;

}   // namespace
#endif
