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
    __declspec(dllexport) void SetKeyHooks(KeyHookEntry* entries, unsigned int nentries);
    __declspec(dllexport) void GetLastKey(DWORD* vkCode, DWORD* scanCode);
}

static FILE* _logfp = NULL;
static HHOOK _hook = NULL;
static KeyHookEntry* _entries = NULL;
static unsigned int _nentries = 0;
static DWORD _vkCode = 0;
static DWORD _scanCode = 0;

static const LONG DELTA = 4;

static BOOL findKeyHook(DWORD* vkCode, DWORD* scanCode)
{
    if (_entries == NULL) return FALSE;

    for (unsigned int i = 0; i < _nentries; i++) {
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
            BOOL extended = ((kdb->flags & LLKHF_EXTENDED) == LLKHF_EXTENDED);
            BOOL keyup = ((kdb->flags & LLKHF_UP) == LLKHF_UP);
            if (findKeyHook(&vkCode, &scanCode)) {
                if (_logfp != NULL) {
                    fprintf(_logfp, " INJECT: vkCode=%u, scanCode=%u\n", vkCode, scanCode);
                }
                if (vkCode != 0) {
                    INPUT input = {0};
                    if (scanCode) {
                        // Emit a keyboard event.
                        input.type = INPUT_KEYBOARD;
                        input.ki.wVk = vkCode;
                        input.ki.wScan = scanCode;
                        input.ki.dwFlags = (((extended)? KEYEVENTF_EXTENDEDKEY : 0) |
                                            ((keyup)? KEYEVENTF_KEYUP : 0));
                    } else {
                        // Emit a mouse event.
                        input.type = INPUT_MOUSE;
                        switch (vkCode) {
                        case 0x31: // 1
                            input.mi.dwFlags = ((keyup)? 
                                                MOUSEEVENTF_LEFTUP : 
                                                MOUSEEVENTF_LEFTDOWN);
                            break;
                        case 0x32: // 2
                            input.mi.dwFlags = ((keyup)? 
                                                MOUSEEVENTF_RIGHTUP : 
                                                MOUSEEVENTF_RIGHTDOWN);
                            break;

                        case VK_LEFT:
                            input.mi.dx = -DELTA;
                            input.mi.dwFlags = ((!keyup)? MOUSEEVENTF_MOVE : 0);
                            break;
                        case VK_RIGHT:
                            input.mi.dx = +DELTA;
                            input.mi.dwFlags = ((!keyup)? MOUSEEVENTF_MOVE : 0);
                            break;
                        case VK_UP:
                            input.mi.dy = -DELTA;
                            input.mi.dwFlags = ((!keyup)? MOUSEEVENTF_MOVE : 0);
                            break;
                        case VK_DOWN:
                            input.mi.dy = +DELTA;
                            input.mi.dwFlags = ((!keyup)? MOUSEEVENTF_MOVE : 0);
                            break;
                        }
                    }
                    SendInput(1, &input, sizeof(input));
                }
                return TRUE;
            }
            if (!keyup) {
                _vkCode = vkCode;
                _scanCode = scanCode;
            }
        }
    }
    return CallNextHookEx(_hook, nCode, wParam, lParam);
}

__declspec(dllexport) void SetLogFile(FILE* logfp)
{
    _logfp = logfp;
}

__declspec(dllexport) void SetKeyHooks(KeyHookEntry* entries, unsigned int nentries)
{
    if (_entries != NULL) {
        free(_entries);
        _entries = NULL;
    }
    _entries = (KeyHookEntry*) calloc(nentries, sizeof(KeyHookEntry));
    if (_entries != NULL) {
        memcpy(_entries, entries, nentries*sizeof(KeyHookEntry));
        _nentries = nentries;
    }
}

__declspec(dllexport) void GetLastKey(DWORD* pvkCode, DWORD* pscanCode)
{
    *pvkCode = _vkCode;
    *pscanCode = _scanCode;
}

BOOL WINAPI DllMain(
    HINSTANCE hInstance,
    DWORD fdwReason,
    LPVOID )
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        //fprintf(stderr, "ATTACH\n");
        _hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardProc, hInstance, 0);
        return (_hook != NULL);

    case DLL_PROCESS_DETACH:
        if (_hook != NULL) {
            UnhookWindowsHookEx(_hook);
            _hook = NULL;
        }
        if (_entries != NULL) {
            free(_entries);
            _entries = NULL;
        }
        //fprintf(stderr, "DETACH\n");
        return TRUE;

    default:
        return TRUE;
    }
}
