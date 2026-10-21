// Reads the setFPMode probe table from a running cnc3game.dat / cnc3ep1.dat.
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    DWORD table = strtoul(argv[1], NULL, 16);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = {sizeof pe}; DWORD pid = 0;
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!_stricmp(pe.szExeFile, "cnc3game.dat") || !_stricmp(pe.szExeFile, "cnc3ep1.dat") || !_stricmp(pe.szExeFile, "cnc3probe.dat")) pid = pe.th32ProcessID;
    if (!pid) { printf("game not running\n"); return 1; }
    HANDLE h = OpenProcess(PROCESS_VM_READ, FALSE, pid);
    DWORD hdr[2]; SIZE_T got;
    if (!ReadProcessMemory(h, (void *)(table - 16), hdr, 8, &got)) { printf("read failed %lu\n", GetLastError()); return 1; }
    printf("distinct=%lu total_calls=%lu\n", hdr[0], hdr[1]);
    static DWORD t[250][4];
    ReadProcessMemory(h, (void *)table, t, sizeof t, &got);
    for (DWORD i = 0; i < hdr[0] && i < 250; i++)
        printf("caller=%08lx  x87cw=%04lx  mxcsr=%08lx%s%s  count=%lu\n", t[i][0], t[i][1], t[i][2],
               (t[i][2] & 0x8000) ? " FTZ" : "", (t[i][2] & 0x40) ? " DAZ" : "", t[i][3]);
    return 0;
}
