// What the game sees: GDI display settings, monitors, and d3d9 adapter modes.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
static BOOL CALLBACK mon(HMONITOR m, HDC dc, LPRECT r, LPARAM p) {
    MONITORINFOEXA mi = {sizeof mi}; GetMonitorInfoA(m, (MONITORINFO *)&mi);
    printf("monitor %s rect=(%ld,%ld)-(%ld,%ld) work=(%ld,%ld)-(%ld,%ld) primary=%d\n", mi.szDevice,
           mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
           mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom, !!(mi.dwFlags & MONITORINFOF_PRIMARY));
    return TRUE;
}
int main(void) {
    printf("SM_CXSCREEN=%d SM_CYSCREEN=%d virtual=%dx%d monitors=%d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
           GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), GetSystemMetrics(SM_CMONITORS));
    HDC dc = GetDC(0); printf("LOGPIXELSX=%d HORZRES=%d DESKTOPHORZRES=%d\n", GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, DESKTOPHORZRES));
    EnumDisplayMonitors(0, 0, mon, 0);
    DEVMODEA dm = {.dmSize = sizeof dm};
    EnumDisplaySettingsA(0, ENUM_CURRENT_SETTINGS, &dm);
    printf("current mode %lux%lu @%lu bpp=%lu\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, dm.dmBitsPerPel);
    printf("GDI modes:");
    for (int i = 0; EnumDisplaySettingsA(0, i, &dm); i++) if (dm.dmBitsPerPel == 32) printf(" %lux%lu@%lu", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    printf("\n");
    HMODULE h = LoadLibraryA("d3d9.dll");
    char path[MAX_PATH]; GetModuleFileNameA(h, path, sizeof path);
    IDirect3D9 *(WINAPI *create)(UINT) = (void *)GetProcAddress(h, "Direct3DCreate9");
    IDirect3D9 *d3d = create(D3D_SDK_VERSION);
    D3DADAPTER_IDENTIFIER9 id; IDirect3D9_GetAdapterIdentifier(d3d, 0, 0, &id);
    printf("d3d9: %s  adapters=%u  [%s] driver=%s\n", path, IDirect3D9_GetAdapterCount(d3d), id.Description, id.Driver);
    D3DDISPLAYMODE cur; IDirect3D9_GetAdapterDisplayMode(d3d, 0, &cur); printf("d3d9 current %ux%u@%u\n", cur.Width, cur.Height, cur.RefreshRate);
    UINT n = IDirect3D9_GetAdapterModeCount(d3d, 0, D3DFMT_X8R8G8B8);
    printf("d3d9 X8R8G8B8 modes (%u):", n);
    for (UINT i = 0; i < n; i++) { D3DDISPLAYMODE m; IDirect3D9_EnumAdapterModes(d3d, 0, D3DFMT_X8R8G8B8, i, &m); printf(" %ux%u@%u", m.Width, m.Height, m.RefreshRate); }
    printf("\n");
    return 0;
}
