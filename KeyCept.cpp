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
const LPCWSTR KEYCEPT_NAME = L"KeyCept";
static HICON HICON_KEYCEPT_OFF = NULL;
static HICON HICON_KEYCEPT_ON = NULL;
enum {
    WM_NOTIFY_ICON = WM_USER+1,
};

// logging
static FILE* logfp = NULL;

//  KeyCept
// 
const LPCWSTR TARGET_WINDOW_CLASS = L"ConsoleWindowClass";
static HookeyDLL hookeyDLL = {0};
static KeyHookEntry hooks[] = {
    { 40, 80,   98, 80, },     // DOWN -> VK_2
    { 37, 75,  100, 75, },     // LEFT -> VK_4
    { 39, 77,  102, 77, },     // RIGHT -> VK_6
    { 38, 72,  104, 72, },     // UP -> VK_8
};

typedef struct _KeyCept
{
    BOOL enabled;
    BOOL focused;
    UINT icon_id;
    UINT_PTR timer_id;
    UINT timer_interval;
} KeyCept;

// keyceptUpdateStatus
static void keyceptUpdateStatus(KeyCept* self, HWND hWnd)
{
    BOOL ison = (self->focused && self->enabled);
    
    if (ison) {
        hookeyDLL.SetKeyHooks(hooks, _countof(hooks));
    } else {
        hookeyDLL.SetKeyHooks(NULL, 0);
    }
    
    NOTIFYICONDATA nidata = {0};
    nidata.cbSize = sizeof(nidata);
    nidata.hWnd = hWnd;
    nidata.uID = self->icon_id;
    nidata.uFlags = NIF_ICON;
    nidata.hIcon = (ison)? HICON_KEYCEPT_ON : HICON_KEYCEPT_OFF;
    Shell_NotifyIcon(NIM_MODIFY, &nidata);
}

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
        KeyCept* self = (KeyCept*)cs->lpCreateParams;
        if (self != NULL) {
	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->icon_id;
            nidata.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nidata.uCallbackMessage = WM_NOTIFY_ICON;
            nidata.hIcon = HICON_KEYCEPT_OFF;
            StringCchCopy(nidata.szTip, _countof(nidata.szTip), KEYCEPT_NAME);
            Shell_NotifyIcon(NIM_ADD, &nidata);
            SetTimer(hWnd, self->timer_id, self->timer_interval, NULL);
        }
	return FALSE;
    }
    
    case WM_DESTROY:
    {
        // Clean up.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCept* self = (KeyCept*)lp;
        if (self != NULL) {
            KillTimer(hWnd, self->timer_id);
	    // Unregister the icon.
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->icon_id;
            Shell_NotifyIcon(NIM_DELETE, &nidata);
        }
	PostQuitMessage(0);
	return FALSE;
    }

    case WM_TIMER:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCept* self = (KeyCept*)lp;
        if (self != NULL) {
            HWND fHwnd = GetForegroundWindow();
            WCHAR name[256];
            if (GetClassName(fHwnd, name, _countof(name)-1)) {
                self->focused = (wcscmp(name, TARGET_WINDOW_CLASS) == 0);
                keyceptUpdateStatus(self, hWnd);
            }
        }
        return FALSE;
    }

    case WM_COMMAND:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCept* self = (KeyCept*)lp;
        // Command specified.
	switch (LOWORD(wParam)) {
        case IDM_TOGGLE:
            if (self != NULL) {
                HMENU menu = GetMenu(hWnd);
                self->enabled = !self->enabled;
                if (menu != NULL) {
                    menu = GetSubMenu(menu, 0);
                    if (menu != NULL) {
                        MENUITEMINFO info = {0};
                        info.cbSize = sizeof(info);
                        info.fMask = MIIM_STATE;
                        info.fState = (self->enabled)? MFS_CHECKED : MFS_UNCHECKED;
                        info.fState |= MFS_DEFAULT;
                        SetMenuItemInfo(menu, IDM_TOGGLE, FALSE, &info);
                    }
                }
                keyceptUpdateStatus(self, hWnd);
            }
            break;
	case IDM_EXIT:
	    SendMessage(hWnd, WM_CLOSE, 0, 0);
	    break;
	}
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

    case WM_CLOSE:
	DestroyWindow(hWnd);
	return FALSE;

    default:
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}


//  KeyCeptMain
// 
int KeyCeptMain(
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
	klass.lpszClassName = L"KeyCeptWindowClass";
	atom = RegisterClass(&klass);
    }

    // Create a structure.
    KeyCept* keycept = (KeyCept*) calloc(1, sizeof(KeyCept));
    if (keycept == NULL) return 111;
    keycept->enabled = FALSE;
    keycept->focused = FALSE;
    keycept->icon_id = 1;
    keycept->timer_id = 1;
    keycept->timer_interval = 500;
    
    // Create a SysTray window.
    HWND hWnd = CreateWindow(
	(LPCWSTR)atom,
	KEYCEPT_NAME,
	(WS_OVERLAPPED | WS_SYSMENU),
	CW_USEDEFAULT, CW_USEDEFAULT,
	CW_USEDEFAULT, CW_USEDEFAULT,
	NULL, NULL, hInstance, keycept);
    UpdateWindow(hWnd);
    {
        // Set the default item.
        HMENU menu = GetMenu(hWnd);
        if (menu != NULL) {
            menu = GetSubMenu(menu, 0);
            if (menu != NULL) {
                SetMenuDefaultItem(menu, IDM_TOGGLE, FALSE);
            }
        }
    }

    // Event loop.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    free(keycept);
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
    return KeyCeptMain(hInstance, hPrevInstance, nCmdShow, argc, argv);
}
#else
int wmain(int argc, wchar_t* argv[])
{
    logfp = stderr;
    return KeyCeptMain(GetModuleHandle(NULL), NULL, 0, argc, argv);
}
#endif
