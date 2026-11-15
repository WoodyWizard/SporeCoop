/* A DLL that does nothing: the lab loads it under another program's DLL name (rtsshooks.dll, BUG-0782) to check
   that SporeKit and the mod name that program and tell the player (tools/tests/coop_rtss_note.py). */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r)
{
    (void)h;
    (void)why;
    (void)r;
    return TRUE;
}
