// -*- tab-width: 4 -*-
// Hookey.cpp

#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "Hookey.h"

#pragma comment(lib, "user32.lib")

extern "C" {
    __declspec(dllexport) void SetLogFile(FILE* logfp);
    __declspec(dllexport) void SetKeyHooks(KeyHookEntry* entries, int nentries);
}

static FILE* _logfp = NULL;
static HHOOK _hook = NULL;
static KeyHookEntry* _entries = NULL;
static int _nentries = 0;

static BOOL findKeyHook(DWORD* vkCode, DWORD* scanCode)
{
    for (int i = 0; i < _nentries; i++) {
        KeyHookEntry* ent = &(_entries[i]);
        if (ent->vkCode0 == *vkCode &&
            ent->scanCode0 == *scanCode) {
            *vkCode = ent->vkCode1;
            *scanCode = ent->scanCode1;
            return TRUE;
        }
    }
    return FALSE;
}

static LRESULT CALLBACK keyboardProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kdb = (KBDLLHOOKSTRUCT*)lParam;
        if (!(kdb->flags & LLKHF_INJECTED)) {
            if (_logfp != NULL) {
                fprintf(_logfp, "wParam=%p, vkCode=%u, scanCode=%u, flags=0x%x\n", 
                        wParam, kdb->vkCode, kdb->scanCode, kdb->flags);
            }
            DWORD vkCode = kdb->vkCode;
            DWORD scanCode = kdb->scanCode;
            if (findKeyHook(&vkCode, &scanCode)) {
                if (_logfp != NULL) {
                    fprintf(_logfp, "INJECT: vkCode=%u, scanCode=%u\n", vkCode, scanCode);
                }
                DWORD flags = 0;
                if ((kdb->flags & LLKHF_EXTENDED) == LLKHF_EXTENDED) {
                    flags |= KEYEVENTF_EXTENDEDKEY;
                }
                if ((kdb->flags & LLKHF_UP) == LLKHF_UP) {
                    flags |= KEYEVENTF_KEYUP;
                }
                keybd_event(vkCode, scanCode, flags, NULL);
                return TRUE;
            }
        }
    }
    return CallNextHookEx(_hook, nCode, wParam, lParam);
}

__declspec(dllexport) void SetLogFile(FILE* logfp)
{
    _logfp = logfp;
}

__declspec(dllexport) void SetKeyHooks(KeyHookEntry* entries, int nentries)
{
    _entries = entries;
    _nentries = nentries;
}

BOOL WINAPI DllMain(
    HINSTANCE hInstance,
    DWORD fdwReason,
    LPVOID )
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        fprintf(stderr, "ATTACH\n");
        _hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardProc, hInstance, 0);
        return (_hook != NULL);

    case DLL_PROCESS_DETACH:
        if (_hook != NULL) {
            UnhookWindowsHookEx(_hook);
            _hook = NULL;
        }
        fprintf(stderr, "DETACH\n");
        return TRUE;

    default:
        return TRUE;
    }
}
