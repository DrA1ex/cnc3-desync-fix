// Prints the primary desktop size in Wine pixels, as a DPI-unaware game would get for fullscreen.
#include <windows.h>
#include <stdio.h>
int main(void) {
    BOOL (WINAPI *sdac)(HANDLE) = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
    if (sdac) sdac((HANDLE)-4);
    DEVMODEA dm = {.dmSize = sizeof dm};
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) printf("%lu %lu\n", dm.dmPelsWidth, dm.dmPelsHeight);
    else printf("%d %d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    return 0;
}
