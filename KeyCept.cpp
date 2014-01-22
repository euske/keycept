//  KeyCept.cpp
//

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <StrSafe.h>
#include <Shlobj.h>
#include "Hookey.h"
#include "Resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// Constants (you shouldn't change)
const LPCWSTR KEYCEPT_NAME = L"Keycept";
const LPCWSTR KEYCEPT_WNDCLASS = L"KeyceptClass";
static HICON HICON_KEYCEPT_OFF = NULL;
static HICON HICON_KEYCEPT_ON = NULL;
enum {
    WM_NOTIFY_ICON = WM_USER+1,
};

// logging
static FILE* logfp = NULL;

//  Keycept
// 
static HookeyDLL hookeyDLL = {0};
static KeyHookEntry hooks[] = {
    { 40, 80,   98, 80, },     // DOWN -> VK_2
    { 37, 75,  100, 75, },     // LEFT -> VK_4
    { 39, 77,  102, 77, },     // RIGHT -> VK_6
    { 38, 72,  104, 72, },     // UP -> VK_8
};

//  keyceptWndProc
//
static LRESULT CALLBACK keyceptWndProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    //fwprintf(stderr, L"msg: %x, hWnd=%p, wParam=%p\n", uMsg, hWnd, wParam);

    switch (uMsg) {
    case WM_CREATE:
    {
        // Initialization.
	CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        NOTIFYICONDATA nidata = {0};
        nidata.cbSize = sizeof(nidata);
        nidata.hWnd = hWnd;
        nidata.uID = 0;
        nidata.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nidata.uCallbackMessage = WM_NOTIFY_ICON;
        nidata.hIcon = HICON_KEYCEPT_OFF;
        StringCchCopy(nidata.szTip, _countof(nidata.szTip), KEYCEPT_NAME);
        Shell_NotifyIcon(NIM_ADD, &nidata);
	return FALSE;
    }
    
    case WM_DESTROY:
    {
        // Clean up.
        NOTIFYICONDATA nidata = {0};
        nidata.cbSize = sizeof(nidata);
        nidata.hWnd = hWnd;
        nidata.uID = 0;
        Shell_NotifyIcon(NIM_DELETE, &nidata);
	PostQuitMessage(0);
	return FALSE;
    }

    case WM_NOTIFY_ICON:
    {
        // UI event handling.
	POINT pt;
        HMENU menu = GetMenu(hWnd);
        if (menu != NULL) {
            menu = GetSubMenu(menu, 0);
        }
	switch (lParam) {
	case WM_LBUTTONDBLCLK:
            if (menu != NULL) {
                UINT item = GetMenuDefaultItem(menu, FALSE, 0);
                SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(item, 1), NULL);
            }
	    break;
	case WM_LBUTTONUP:
	    break;
	case WM_RBUTTONUP:
	    if (GetCursorPos(&pt)) {
                SetForegroundWindow(hWnd);
                if (menu != NULL) {
                    TrackPopupMenu(menu, TPM_LEFTALIGN, 
                                   pt.x, pt.y, 0, hWnd, NULL);
                }
		PostMessage(hWnd, WM_NULL, 0, 0);
	    }
	    break;
	}
	return FALSE;
    }

    case WM_COMMAND:
    {
        // Command specified.
	switch (LOWORD(wParam)) {
        case IDM_TURNON:
            hookeyDLL.SetKeyHooks(hooks, _countof(hooks));
            {
                NOTIFYICONDATA nidata = {0};
                nidata.cbSize = sizeof(nidata);
                nidata.hWnd = hWnd;
                nidata.uID = 0;
                nidata.uFlags = NIF_ICON;
                nidata.hIcon = HICON_KEYCEPT_ON;
                Shell_NotifyIcon(NIM_MODIFY, &nidata);
                HMENU menu = GetMenu(hWnd);
                if (menu != NULL) {
                    menu = GetSubMenu(menu, 0);
                    if (menu != NULL) {
                        SetMenuDefaultItem(menu, IDM_TURNOFF, FALSE);
                    }
                }
            }
            break;
        case IDM_TURNOFF:
            hookeyDLL.SetKeyHooks(NULL, 0);
            {
                NOTIFYICONDATA nidata = {0};
                nidata.cbSize = sizeof(nidata);
                nidata.hWnd = hWnd;
                nidata.uID = 0;
                nidata.uFlags = NIF_ICON;
                nidata.hIcon = HICON_KEYCEPT_OFF;
                Shell_NotifyIcon(NIM_MODIFY, &nidata);
                HMENU menu = GetMenu(hWnd);
                if (menu != NULL) {
                    menu = GetSubMenu(menu, 0);
                    if (menu != NULL) {
                        SetMenuDefaultItem(menu, IDM_TURNON, FALSE);
                    }
                }
            }
            break;
	case IDM_EXIT:
	    SendMessage(hWnd, WM_CLOSE, 0, 0);
	    break;
	}
	return FALSE;
    }

    case WM_CLOSE:
	DestroyWindow(hWnd);
	return FALSE;

    default:
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}


//  KeyceptMain
// 
int KeyceptMain(
    HINSTANCE hInstance, 
    HINSTANCE hPrevInstance, 
    int nCmdShow,
    int argc, LPWSTR* argv)
{
    // Prevent a duplicate process.
    HANDLE mutex = CreateMutex(NULL, TRUE, KEYCEPT_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
	CloseHandle(mutex);
	return 0;
    }

    // Load resources.
    HICON_KEYCEPT_OFF = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_OFF));
    if (HICON_KEYCEPT_OFF == NULL) return 111;
    HICON_KEYCEPT_ON = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_ON));
    if (HICON_KEYCEPT_ON == NULL) return 111;

    // Load a DLL.
    HMODULE module = LoadLibrary(L"hookey.dll");
    if (module == NULL) return 111;
    hookeyDLL.SetLogFile = (pSetLogFile) GetProcAddress(module, "SetLogFile");
    hookeyDLL.SetKeyHooks = (pSetKeyHooks) GetProcAddress(module, "SetKeyHooks");

    hookeyDLL.SetLogFile(logfp);

    // Register the window class.
    ATOM atom;
    {
	WNDCLASS klass;
	ZeroMemory(&klass, sizeof(klass));
	klass.lpfnWndProc = keyceptWndProc;
	klass.hInstance = hInstance;
	klass.hIcon = HICON_KEYCEPT_OFF;
	klass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        klass.lpszMenuName = MAKEINTRESOURCE(IDM_POPUPMENU);
	klass.lpszClassName = KEYCEPT_WNDCLASS;
	atom = RegisterClass(&klass);
    }
    
    // Create a SysTray window.
    HWND hWnd = CreateWindow(
	(LPCWSTR)atom,
	KEYCEPT_NAME,
	(WS_OVERLAPPED | WS_SYSMENU),
	CW_USEDEFAULT, CW_USEDEFAULT,
	CW_USEDEFAULT, CW_USEDEFAULT,
	NULL, NULL, hInstance, NULL);
    UpdateWindow(hWnd);
    {
        // Set the default item.
        HMENU menu = GetMenu(hWnd);
        if (menu != NULL) {
            menu = GetSubMenu(menu, 0);
            if (menu != NULL) {
                SetMenuDefaultItem(menu, IDM_TURNON, FALSE);
            }
        }
    }

    // Event loop.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    FreeLibrary(module);

    return (int)msg.wParam;
}


// WinMain and wmain
#ifdef WINDOWS
int WinMain(HINSTANCE hInstance, 
	    HINSTANCE hPrevInstance, 
	    LPSTR lpCmdLine,
	    int nCmdShow)
{
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    return KeyceptMain(hInstance, hPrevInstance, nCmdShow, argc, argv);
}
#else
int wmain(int argc, wchar_t* argv[])
{
    logfp = stderr;
    return KeyceptMain(GetModuleHandle(NULL), NULL, 0, argc, argv);
}
#endif
