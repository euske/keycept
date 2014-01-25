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
#pragma comment(lib, "gdi32.lib")

// Constants (you shouldn't change)
const LPCWSTR KEYCEPT_NAME = L"KeyCept";
enum {
    WM_USER_ICON_EVENT = WM_USER+1,
    WM_USER_STATE_CHANGED,
    WM_USER_ICON_CHANGED,
    WM_USER_KEYCODE_CHANGED,
    WM_USER_WINDOW_CHANGED,
};

// logging file.
static FILE* logfp = NULL;

//  KeyCept
// 
const LPCWSTR TARGET_WINDOW_CLASS = L"ConsoleWindowClass";
static KeyHookEntry hooks[] = {
    { 40, 80,   98, 80, },     // DOWN -> VK_2
    { 37, 75,  100, 75, },     // LEFT -> VK_4
    { 39, 77,  102, 77, },     // RIGHT -> VK_6
    { 38, 72,  104, 72, },     // UP -> VK_8
};

typedef struct _KeyCeptSettings
{
    HMODULE module;
    HookeyDLL hookeyDLL;
    ATOM trayWindowAtom;
    LPCWSTR trayName;
    HICON iconKeyCeptOff;
    HICON iconKeyCeptOn;
    UINT timerInterval;
    HWND dialogHWnd;
    BOOL enabled;
} KeyCeptSettings;


//  keyceptUpdateStatus
//
static BOOL keyceptUpdateStatus(KeyCeptSettings* settings, HWND hWnd)
{
    BOOL ison = settings->enabled;
    WCHAR name[256];
    if (ison && GetClassName(hWnd, name, _countof(name)-1)) {
        ison = (wcscmp(name, TARGET_WINDOW_CLASS) == 0);
    }
    
    if (ison) {
        settings->hookeyDLL.SetKeyHooks(hooks, _countof(hooks));
    } else {
        settings->hookeyDLL.SetKeyHooks(NULL, 0);
    }

    return ison;
}


//  keyceptDialogProc
//
typedef struct _KeyCeptDialog
{
    KeyCeptSettings* settings;
} KeyCeptDialog;

static INT_PTR CALLBACK keyceptDialogProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    //fwprintf(stderr, L"msg: %x, hWnd=%p, wParam=%p\n", uMsg, hWnd, wParam);

    switch (uMsg) {
    case WM_INITDIALOG:
    {
        // Initialization.
        KeyCeptSettings* settings = (KeyCeptSettings*)lParam;
        // Create the data structure.
        KeyCeptDialog* self = (KeyCeptDialog*) calloc(1, sizeof(KeyCeptDialog));
        if (self != NULL) {
            self->settings = settings;
	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        }
	return TRUE;
    }

    case WM_DESTROY:
    {
        // Clean up.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptDialog* self = (KeyCeptDialog*)lp;
        if (self != NULL) {
            // Destroy the data structure.
	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)NULL);
            free(self);
        }
	return FALSE;
    }

    case WM_CLOSE:
	ShowWindow(hWnd, SW_HIDE);
	return FALSE;

    case WM_USER_KEYCODE_CHANGED:
    {
        DWORD vkCode = (DWORD)wParam;
        DWORD scanCode = (DWORD)lParam;
        WCHAR text[256];
        StringCchPrintf(text, _countof(text), 
                        L"KeyCode=%d, ScanCode=%d", vkCode, scanCode);
        SetDlgItemText(hWnd, IDC_TEXT_KEYCODE, text);
        return FALSE;
    }

    case WM_USER_WINDOW_CHANGED:
    {
        HWND fHWnd = (HWND)lParam;
        if (fHWnd != hWnd) {
            WCHAR name[256];
            if (GetClassName(fHWnd, name, _countof(name)-1)) {
                WCHAR text[256];
                StringCchPrintf(text, _countof(text), L"[%s]", name);
                SetDlgItemText(hWnd, IDC_TEXT_WINDOW, text);
            }
        }
        return FALSE;
    }

    default:
        return FALSE;
    }
}
    

//  keyceptTrayWndProc
//
typedef struct _KeyCeptTray
{
    KeyCeptSettings* settings;
    UINT iconId;
    UINT_PTR timerId;
    HWND lastFHWnd;
    DWORD lastVkCode;
    DWORD lastScanCode;
} KeyCeptTray;

static LRESULT CALLBACK keyceptTrayWndProc(
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
        KeyCeptSettings* settings = (KeyCeptSettings*)cs->lpCreateParams;
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
        // Create the data structure.
        KeyCeptTray* self = (KeyCeptTray*) calloc(1, sizeof(KeyCeptTray));
        if (self != NULL) {
            self->settings = settings;
            self->iconId = 1;
            self->timerId = 1;
            self->lastFHWnd = NULL;
            self->settings->dialogHWnd = CreateDialogParam(
                cs->hInstance,
                MAKEINTRESOURCE(IDD_CONFIG_PANEL),
                hWnd, keyceptDialogProc, (LPARAM)self->settings);

	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->iconId;
            nidata.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nidata.uCallbackMessage = WM_USER_ICON_EVENT;
            nidata.hIcon = self->settings->iconKeyCeptOff;
            StringCchCopy(nidata.szTip, _countof(nidata.szTip), KEYCEPT_NAME);
            Shell_NotifyIcon(NIM_ADD, &nidata);
            SetTimer(hWnd, self->timerId, self->settings->timerInterval, NULL);
            SendMessage(hWnd, WM_USER_STATE_CHANGED, 
                        (WPARAM)(self->settings->enabled), 0);
        }
	return FALSE;
    }
    
    case WM_DESTROY:
    {
        // Clean up.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            // Destroy the timer.
            KillTimer(hWnd, self->timerId);
            // Cleanup the child window.
            if (self->settings->dialogHWnd != NULL) {
                DestroyWindow(self->settings->dialogHWnd);
                self->settings->dialogHWnd = NULL;
            }
	    // Unregister the icon.
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->iconId;
            Shell_NotifyIcon(NIM_DELETE, &nidata);
            // Destroy the data structure.
	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)NULL);
            free(self);
        }
	PostQuitMessage(0);
	return FALSE;
    }

    case WM_CLOSE:
	DestroyWindow(hWnd);
	return FALSE;

    case WM_COMMAND:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        // Command specified.
	switch (LOWORD(wParam)) {
        case IDM_TOGGLE:
            if (self != NULL) {
                SendMessage(hWnd, WM_USER_STATE_CHANGED, 
                            (WPARAM)(!self->settings->enabled), 0);
            }
            break;

        case IDM_CONFIG:
            if (self != NULL && self->settings->dialogHWnd != NULL) {
                ShowWindow(self->settings->dialogHWnd, SW_SHOWDEFAULT);
            }
            break;

	case IDM_EXIT:
	    SendMessage(hWnd, WM_CLOSE, 0, 0);
	    break;
	}
	return FALSE;
    }

    case WM_TIMER:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            DWORD vkCode, scanCode;
            self->settings->hookeyDLL.GetLastKey(&vkCode, &scanCode);
            if (self->lastVkCode != vkCode ||
                self->lastScanCode != scanCode) {
                self->lastVkCode = vkCode;
                self->lastScanCode = scanCode;
                SendMessage(hWnd, WM_USER_KEYCODE_CHANGED, 
                            (WPARAM)vkCode, (LPARAM)scanCode);
            }
            HWND fHWnd = GetForegroundWindow();
            if (self->lastFHWnd != fHWnd) {
                self->lastFHWnd = fHWnd;
                SendMessage(hWnd, WM_USER_WINDOW_CHANGED, 
                            0, (LPARAM)fHWnd);
            }
        }
        return FALSE;
    }

    case WM_USER_STATE_CHANGED:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            HMENU menu = GetMenu(hWnd);
            if (menu != NULL) {
                menu = GetSubMenu(menu, 0);
                if (menu != NULL) {
                    MENUITEMINFO info = {0};
                    info.cbSize = sizeof(info);
                    info.fMask = MIIM_STATE;
                    info.fState = (((BOOL)wParam)?
                                   MFS_CHECKED : 
                                   MFS_UNCHECKED);
                    info.fState |= MFS_DEFAULT;
                    SetMenuItemInfo(menu, IDM_TOGGLE, FALSE, &info);
                }
            }
            BOOL ison = keyceptUpdateStatus(self->settings, self->lastFHWnd);
            SendMessage(hWnd, WM_USER_ICON_CHANGED, ison, 0);
        }
        return FALSE;
    }

    case WM_USER_KEYCODE_CHANGED:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            if (self->settings->dialogHWnd != NULL) {
                SendMessage(self->settings->dialogHWnd, uMsg, wParam, lParam);
            }
        }
        return FALSE;
    }

    case WM_USER_WINDOW_CHANGED:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            if (self->settings->dialogHWnd != NULL) {
                SendMessage(self->settings->dialogHWnd, uMsg, wParam, lParam);
            }
            BOOL ison = keyceptUpdateStatus(self->settings, (HWND)lParam);
            SendMessage(hWnd, WM_USER_ICON_CHANGED, ison, 0);
        }
        return FALSE;
    }

    case WM_USER_ICON_CHANGED:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->iconId;
            nidata.uFlags = NIF_ICON;
            nidata.hIcon = ((wParam)? 
                            self->settings->iconKeyCeptOn : 
                            self->settings->iconKeyCeptOff);
            Shell_NotifyIcon(NIM_MODIFY, &nidata);
        }
        return FALSE;
    }
    
    case WM_USER_ICON_EVENT:
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
  
    // Create a structure.
    KeyCeptSettings* keycept = (KeyCeptSettings*) calloc(1, sizeof(KeyCeptSettings));
    if (keycept == NULL) return 111;
    keycept->timerInterval = 200;
    keycept->trayName = L"KeyCept Tray";
    // Load resources.
    keycept->iconKeyCeptOff = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_OFF));
    if (keycept->iconKeyCeptOff == NULL) return 111;
    keycept->iconKeyCeptOn = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_ON));
    if (keycept->iconKeyCeptOn == NULL) return 111;
    // Load a DLL.
    keycept->module = LoadLibrary(L"hookey.dll");
    if (keycept->module == NULL) return 111;
    keycept->hookeyDLL.SetLogFile = \
        (pSetLogFile) GetProcAddress(keycept->module, "SetLogFile");
    keycept->hookeyDLL.SetKeyHooks = \
        (pSetKeyHooks) GetProcAddress(keycept->module, "SetKeyHooks");
    keycept->hookeyDLL.GetLastKey = \
        (pGetLastKey) GetProcAddress(keycept->module, "GetLastKey");
    keycept->hookeyDLL.SetLogFile(logfp);
    keycept->enabled = TRUE;

    // Register the window class.
    {
	WNDCLASS wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = keyceptTrayWndProc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        wc.lpszMenuName = MAKEINTRESOURCE(IDM_POPUPMENU);
	wc.lpszClassName = L"KeyCeptTrayWindowClass";
	keycept->trayWindowAtom = RegisterClass(&wc);
    }

    // Create a SysTray window.
    HWND hWnd = CreateWindow(
	(LPCWSTR)keycept->trayWindowAtom,
	KEYCEPT_NAME,
	(WS_OVERLAPPED | WS_SYSMENU),
	CW_USEDEFAULT, CW_USEDEFAULT,
	CW_USEDEFAULT, CW_USEDEFAULT,
	NULL, NULL, hInstance, keycept);
    UpdateWindow(hWnd);
    
    // Event loop.
    MSG msg;
    while (0 < GetMessage(&msg, NULL, 0, 0)) {
        if (keycept->dialogHWnd != NULL &&
            IsDialogMessage(keycept->dialogHWnd, &msg)) {
            // do nothing.
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    FreeLibrary(keycept->module);
    free(keycept);

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
