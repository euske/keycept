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
    WM_NOTIFY_ICON = WM_USER+1,
};

// logging file.
static FILE* logfp = NULL;

// Hookey DLL
static HookeyDLL hookeyDLL = {0};

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
    ATOM trayWindowAtom;
    LPCWSTR trayName;
    ATOM dialogWindowAtom;
    LPCWSTR dialogName;
    HICON iconKeyCeptOff;
    HICON iconKeyCeptOn;
    UINT timerInterval;
} KeyCeptSettings;

typedef struct _KeyCeptDialog
{
    KeyCeptSettings* settings;
    DWORD lastVkCode;
    DWORD lastScanCode;
    HWND lastFHWnd;
    UINT_PTR timerId;
} KeyCeptDialog;

typedef struct _KeyCeptTray
{
    KeyCeptSettings* settings;
    HWND dialogHWnd;
    BOOL enabled;
    BOOL focused;
    UINT iconId;
    UINT_PTR timerId;
} KeyCeptTray;


//  keyceptDialogWndProc
//
static LRESULT CALLBACK keyceptDialogWndProc(
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
        // Create the data structure.
        KeyCeptDialog* self = (KeyCeptDialog*) calloc(1, sizeof(KeyCeptDialog));
        if (self != NULL) {
            self->settings = settings;
            self->timerId = 1;
	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
            // Create a timer.
            SetTimer(hWnd, self->timerId, self->settings->timerInterval, NULL);
        }
	return FALSE;
    }

    case WM_DESTROY:
    {
        // Clean up.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptDialog* self = (KeyCeptDialog*)lp;
        if (self != NULL) {
            // Destroy the timer.
            KillTimer(hWnd, self->timerId);
            // Destroy the data structure.
	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)NULL);
            free(self);
        }
	return FALSE;
    }

    case WM_KEYDOWN:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptDialog* self = (KeyCeptDialog*)lp;
        if (self != NULL) {
            DWORD vkCode = wParam;
            DWORD scanCode = ((lParam >> 16) & 0xff);
            if (vkCode != self->lastVkCode ||
                scanCode != self->lastScanCode) {
                self->lastVkCode = vkCode;
                self->lastScanCode = scanCode;
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
    }
    return FALSE;
    
    case WM_TIMER:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptDialog* self = (KeyCeptDialog*)lp;
        if (self != NULL) {
            HWND fHWnd = GetForegroundWindow();
            if (fHWnd != self->lastFHWnd) {
                self->lastFHWnd = fHWnd;
                InvalidateRect(hWnd, NULL, TRUE);
            }
        }
        return FALSE;
    }

    case WM_PAINT:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptDialog* self = (KeyCeptDialog*)lp;
        if (self != NULL) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            WCHAR text[256];
            StringCchPrintf(text, _countof(text), 
                            L"KeyCode=%d, ScanCode=%d", 
                            self->lastVkCode, self->lastScanCode);
            TextOut(hdc, 0, 0, text, wcslen(text));
            GetClassName(self->lastFHWnd, text, _countof(text)-1);
            TextOut(hdc, 0, 40, text, wcslen(text));
            EndPaint(hWnd, &ps);
        }
	return FALSE;
    }
    return FALSE;

    case WM_CLOSE:
	ShowWindow(hWnd, SW_HIDE);
	return FALSE;

    default:
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

static INT_PTR CALLBACK keyceptDialogProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    fwprintf(stderr, L"msg: %x, hWnd=%p, wParam=%p\n", uMsg, hWnd, wParam);
    return FALSE;
}
    

//  keyceptUpdateStatus
//
static void keyceptUpdateStatus(KeyCeptTray* self, HWND hWnd)
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
    nidata.uID = self->iconId;
    nidata.uFlags = NIF_ICON;
    nidata.hIcon = ((ison)? 
                    self->settings->iconKeyCeptOn : 
                    self->settings->iconKeyCeptOff);
    Shell_NotifyIcon(NIM_MODIFY, &nidata);
}


//  keyceptTrayWndProc
//
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
            self->enabled = FALSE;
            self->focused = FALSE;
            self->iconId = 1;
            self->timerId = 1;

	    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->iconId;
            nidata.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nidata.uCallbackMessage = WM_NOTIFY_ICON;
            nidata.hIcon = self->settings->iconKeyCeptOff;
            StringCchCopy(nidata.szTip, _countof(nidata.szTip), KEYCEPT_NAME);
            Shell_NotifyIcon(NIM_ADD, &nidata);
            SetTimer(hWnd, self->timerId, self->settings->timerInterval, NULL);
            self->dialogHWnd = CreateDialogParam(
                cs->hInstance,
                MAKEINTRESOURCE(IDD_CONFIG_PANEL),
                hWnd, keyceptDialogProc, NULL);
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
            if (self->dialogHWnd != NULL) {
                DestroyWindow(self->dialogHWnd);
                self->dialogHWnd = NULL;
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

    case WM_TIMER:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            HWND fHWnd = GetForegroundWindow();
            WCHAR name[256];
            if (GetClassName(fHWnd, name, _countof(name)-1)) {
                self->focused = (wcscmp(name, TARGET_WINDOW_CLASS) == 0);
                keyceptUpdateStatus(self, hWnd);
            }
        }
        return FALSE;
    }

    case WM_COMMAND:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
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

        case IDM_CONFIG:
            if (self != NULL) {
                ShowWindow(self->dialogHWnd, SW_SHOWDEFAULT);
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
  
    // Load a DLL.
    HMODULE module = LoadLibrary(L"hookey.dll");
    if (module == NULL) return 111;
    hookeyDLL.SetLogFile = (pSetLogFile) GetProcAddress(module, "SetLogFile");
    hookeyDLL.SetKeyHooks = (pSetKeyHooks) GetProcAddress(module, "SetKeyHooks");

    hookeyDLL.SetLogFile(logfp);

    // Create a structure.
    KeyCeptSettings* keycept = (KeyCeptSettings*) calloc(1, sizeof(KeyCeptSettings));
    if (keycept == NULL) return 111;
    keycept->timerInterval = 500;
    keycept->trayName = L"KeyCept Tray";
    keycept->dialogName = L"KeyCept Dialog";
    // Load resources.
    keycept->iconKeyCeptOff = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_OFF));
    if (keycept->iconKeyCeptOff == NULL) return 111;
    keycept->iconKeyCeptOn = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_ON));
    if (keycept->iconKeyCeptOn == NULL) return 111;

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
    {
	WNDCLASS wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = keyceptTrayWndProc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpfnWndProc = keyceptDialogWndProc;
	wc.lpszClassName = L"KeyCeptDialogWindowClass";
	keycept->dialogWindowAtom = RegisterClass(&wc);
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
