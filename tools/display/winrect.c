// Lists top-level windows of cnc3game.dat / cnc3ep1.dat with physical and logical rects.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
static DWORD pid;
static BOOL CALLBACK cb(HWND w, LPARAM p) {
    DWORD wp; GetWindowThreadProcessId(w, &wp);
    if (wp != pid || !IsWindowVisible(w)) return TRUE;
    RECT r, c; GetWindowRect(w, &r); GetClientRect(w, &c);
    char t[128] = ""; GetWindowTextA(w, t, sizeof t);
    UINT dpi = 0; HMODULE u = GetModuleHandleA("user32.dll");
    UINT (WINAPI *gdfw)(HWND) = (void *)GetProcAddress(u, "GetDpiForWindow"); if (gdfw) dpi = gdfw(w);
    printf("hwnd=%p '%s' window=(%ld,%ld)-(%ld,%ld) %ldx%ld client=%ldx%ld style=%08lx dpi=%u\n", w, t, r.left, r.top, r.right, r.bottom,
           r.right - r.left, r.bottom - r.top, c.right, c.bottom, GetWindowLongA(w, GWL_STYLE), dpi);
    return TRUE;
}
int main(void) {
    // be per-monitor aware ourselves so we see physical pixels
    HMODULE u = GetModuleHandleA("user32.dll");
    BOOL (WINAPI *sdac)(HANDLE) = (void *)GetProcAddress(u, "SetProcessDpiAwarenessContext"); if (sdac) sdac((HANDLE)-4);
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 pe = {sizeof pe};
    for (BOOL ok = Process32First(s, &pe); ok; ok = Process32Next(s, &pe))
        if (!_stricmp(pe.szExeFile, "cnc3game.dat") || !_stricmp(pe.szExeFile, "cnc3ep1.dat")) pid = pe.th32ProcessID;
    if (!pid) { printf("game not running\n"); return 1; }
    printf("screen %dx%d (physical, per-monitor aware)\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    EnumWindows(cb, 0);
    return 0;
}
