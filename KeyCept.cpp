//  KeyCept.cpp
//

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include <StrSafe.h>
#include <Shlobj.h>
#include "Hookey.h"
#include "Resource.h"
#include "ini.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")

// Constants (you shouldn't change)
const LPCWSTR KEYCEPT_NAME = L"KeyCept";
const unsigned int MAX_KEY_ENTRIES = 10;
enum {
    WM_USER_ICON_EVENT = WM_USER+1,
    WM_USER_CONFIG_CHANGED,
    WM_USER_STATE_CHANGED,
    WM_USER_ICON_CHANGED,
    WM_USER_KEYCODE_CHANGED,
    WM_USER_WINDOW_CHANGED,
};

// logging file.
static FILE* logfp = NULL;

static BOOL setClipboardText(HWND hWnd, LPCWSTR text)
{
    if (OpenClipboard(hWnd)) {
        size_t size = (wcslen(text)+1)*sizeof(*text);
        HANDLE handle = GlobalAlloc(GMEM_MOVEABLE, size);
        if (handle != NULL) {
            void* dst = GlobalLock(handle);
            if (dst != NULL) {
                EmptyClipboard();
                CopyMemory(dst, text, size);
                GlobalUnlock(handle);
                SetClipboardData(CF_UNICODETEXT, handle);
                handle = NULL;
            }
            if (handle != NULL) {
                GlobalFree(handle);
            }
        }
        CloseClipboard();
        return TRUE;
    }
    return FALSE;
}

//  KeyCept
// 
typedef struct _KeyCeptHook
{
    LPWSTR className;
    unsigned int maxentries;
    unsigned int nentries;
    KeyHookEntry* entries;
    struct _KeyCeptHook* next;
} KeyCeptHook;

typedef struct _KeyCeptSettings
{
    HMODULE hModule;
    LPCWSTR configPath;
    HookeyDLL hookeyDLL;
    ATOM trayWindowAtom;
    LPCWSTR trayName;
    HICON iconKeyCeptOn;
    HICON iconKeyCeptOff;
    HICON iconKeyCeptDisabled;
    UINT timerInterval;
    HWND dialogHWnd;
    BOOL enabled;
    KeyCeptHook* hooks;
} KeyCeptSettings;

static int keyceptINIHandler(void* user, const char* section,
                             const char* name, const char* value)
{
    KeyCeptSettings* settings = (KeyCeptSettings*)user;
    fprintf(stderr, "section=%s, name=%s, value=%s\n", section, name, value);
    if (stricmp(section, "global") == 0) {
        if (stricmp(name, "enabled") == 0) {
            settings->enabled = atoi(value);
            return 1;
        }
    } else {
        KeyCeptHook* hook = settings->hooks;
        KeyCeptHook* found = NULL;
        wchar_t wsection[64];
        mbstowcs_s(NULL, wsection, _countof(wsection), section, _TRUNCATE);
        while (hook != NULL) {
            if (wcscmp(hook->className, wsection) == 0) {
                found = hook;
                break;
            }
            hook = hook->next;
        }
        if (found == NULL) {
            found = (KeyCeptHook*) calloc(1, sizeof(KeyCeptHook));
            if (found == NULL) exit(111);
            found->className = wcsdup(wsection);
            found->maxentries = MAX_KEY_ENTRIES;
            found->nentries = 0;
            found->entries = (KeyHookEntry*) calloc(found->maxentries, sizeof(KeyHookEntry));
            if (found->entries == NULL) exit(111);
            found->next = settings->hooks;
            settings->hooks = found;
        }
        if (found->nentries < found->maxentries) {
            KeyHookEntry* entry = &(found->entries[found->nentries]);
            char cmd[4];
            sscanf_s(name, "%3s %d:%d", cmd, _countof(cmd), 
                     &entry->vkCode0, &entry->scanCode0);
            sscanf_s(value, "%d:%d", 
                     &entry->vkCode1, &entry->scanCode1);
            if (stricmp(cmd, "key") == 0) {
                found->nentries++;
                return 1;
            }
        }
    }
    return 0;
}

//  keyceptLoadConfig
//
static BOOL keyceptLoadConfig(KeyCeptSettings* settings)
{
    fwprintf(stderr, L"loadConfig=%s\n", settings->configPath);
    FILE* fp = NULL;
    if (_wfopen_s(&fp, settings->configPath, L"r") != 0) return FALSE;
    while (settings->hooks != NULL) {
        KeyCeptHook* hook = settings->hooks;
        free(hook->className);
        free(hook->entries);
        settings->hooks = hook->next;
    }
    ini_parse_file(fp, keyceptINIHandler, settings);
    fclose(fp);
    return TRUE;
}

//  keyceptUpdateStatus
//
static KeyCeptHook* keyceptUpdateStatus(KeyCeptSettings* settings, HWND hWnd)
{
    KeyCeptHook* found = NULL;
    WCHAR name[256];
    if (GetClassName(hWnd, name, _countof(name)-1)) {
        KeyCeptHook* hook = settings->hooks;
        while (hook != NULL) {
            if (wcscmp(hook->className, name) == 0) {
                found = hook;
                break;
            }
            hook = hook->next;
        }
    }
    
    if (found != NULL) {
        fwprintf(stderr, L"name=%s\n", name);
        for (unsigned int i = 0 ; i < found->nentries; i++) {
            KeyHookEntry* entry = &(found->entries[i]);
            fwprintf(stderr, L" %d:%d = %d:%d\n", 
                     entry->vkCode0, entry->scanCode0,
                     entry->vkCode1, entry->scanCode1);
        }
        settings->hookeyDLL.SetKeyHooks(found->entries, found->nentries);
    } else {
        settings->hookeyDLL.SetKeyHooks(NULL, 0);
    }

    return found;
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
        if (self == NULL) exit(111);
        self->settings = settings;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
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

    case WM_COMMAND:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptDialog* self = (KeyCeptDialog*)lp;
	switch (LOWORD(wParam)) {
        case IDC_BUTTON_RELOAD:
            SendMessage(GetParent(hWnd), WM_USER_CONFIG_CHANGED, 0, 0);
            break;

        case IDC_BUTTON_OPEN:
        {
            if (self != NULL) {
                LPCWSTR path = self->settings->configPath;
                ShellExecute(NULL, L"open", path, NULL, NULL, SW_SHOWDEFAULT);
            }
            break;
        }

        case IDC_BUTTON_COPY_KEYCODE:
        {
            WCHAR text[256];
            if (GetDlgItemText(hWnd, IDC_TEXT_KEYCODE, text, _countof(text))) {
                setClipboardText(hWnd, text);
            }
            break;
        }

        case IDC_BUTTON_COPY_WINDOW:
        {
            WCHAR text[256];
            if (GetDlgItemText(hWnd, IDC_TEXT_WINDOW, text, _countof(text))) {
                setClipboardText(hWnd, text);
            }
            break;
        }
        }
        return FALSE;
    }

    case WM_USER_KEYCODE_CHANGED:
    {
        DWORD vkCode = (DWORD)wParam;
        DWORD scanCode = (DWORD)lParam;
        WCHAR text[256];
        StringCchPrintf(text, _countof(text), L"Key %d:%d", vkCode, scanCode);
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
        if (self == NULL) exit(111);
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
        nidata.uFlags = NIF_MESSAGE;
        nidata.uCallbackMessage = WM_USER_ICON_EVENT;
        Shell_NotifyIcon(NIM_ADD, &nidata);
        SetTimer(hWnd, self->timerId, self->settings->timerInterval, NULL);

        SendMessage(hWnd, WM_USER_CONFIG_CHANGED, 0, 0);
        SendMessage(hWnd, WM_USER_STATE_CHANGED, 0, 0);
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
                self->settings->enabled = !(self->settings->enabled);
                SendMessage(hWnd, WM_USER_STATE_CHANGED, 0, 0);
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
                SendMessage(hWnd, WM_USER_WINDOW_CHANGED, 0, (LPARAM)fHWnd);
            }
        }
        return FALSE;
    }

    case WM_USER_CONFIG_CHANGED:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            keyceptLoadConfig(self->settings);
            SendMessage(hWnd, WM_USER_STATE_CHANGED, 0, 0);
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
                    info.fState = ((self->settings->enabled)?
                                   MFS_CHECKED : 
                                   MFS_UNCHECKED);
                    info.fState |= MFS_DEFAULT;
                    SetMenuItemInfo(menu, IDM_TOGGLE, FALSE, &info);
                }
            }
            KeyCeptHook* active = keyceptUpdateStatus(self->settings, self->lastFHWnd);
            SendMessage(hWnd, WM_USER_ICON_CHANGED, 0, (LPARAM)active);
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
            KeyCeptHook* active = keyceptUpdateStatus(self->settings, (HWND)lParam);
            SendMessage(hWnd, WM_USER_ICON_CHANGED, 0, (LPARAM)active);
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

    case WM_USER_ICON_CHANGED:
    {
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            KeyCeptHook* active = (KeyCeptHook*)lParam;
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->iconId;
            nidata.uFlags = NIF_ICON | NIF_TIP;
            nidata.hIcon = self->settings->iconKeyCeptDisabled;
            StringCchCopy(nidata.szTip, _countof(nidata.szTip), KEYCEPT_NAME);
            if (self->settings->enabled) {
                if (active != NULL) {
                    nidata.hIcon = self->settings->iconKeyCeptOn;
                    StringCchPrintf(nidata.szTip, _countof(nidata.szTip), 
                                    L"Active: %s (%d keys)", 
                                    active->className, active->nentries);
                } else {
                    nidata.hIcon = self->settings->iconKeyCeptOff;
                }
            }
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
    if (keycept == NULL) exit(111);
    keycept->timerInterval = 200;
    keycept->trayName = L"KeyCept Tray";
    // Load resources.
    keycept->iconKeyCeptOn = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_ON));
    if (keycept->iconKeyCeptOn == NULL) return 111;
    keycept->iconKeyCeptOff = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_OFF));
    if (keycept->iconKeyCeptOff == NULL) return 111;
    keycept->iconKeyCeptDisabled = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_DISABLED));
    if (keycept->iconKeyCeptDisabled == NULL) return 111;
    // Load a DLL.
    keycept->hModule = LoadLibrary(L"hookey.dll");
    if (keycept->hModule == NULL) return 111;
    keycept->hookeyDLL.SetLogFile = \
        (pSetLogFile) GetProcAddress(keycept->hModule, "SetLogFile");
    keycept->hookeyDLL.SetKeyHooks = \
        (pSetKeyHooks) GetProcAddress(keycept->hModule, "SetKeyHooks");
    keycept->hookeyDLL.GetLastKey = \
        (pGetLastKey) GetProcAddress(keycept->hModule, "GetLastKey");
    keycept->hookeyDLL.SetLogFile(logfp);

    // Ini file path.
    WCHAR path[MAX_PATH];
    WCHAR basedir[MAX_PATH];
    if (GetCurrentDirectory(_countof(basedir), basedir)) {
        StringCchPrintf(path, _countof(path), L"%s\\KeyCept.ini", basedir);
        keycept->configPath = path;
    }
    if (2 <= argc) {
        keycept->configPath = argv[1];
    }

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
    HWND hWnd = CreateWindowEx(
        WS_EX_NOACTIVATE,
	(LPCWSTR)keycept->trayWindowAtom,
	KEYCEPT_NAME,
	WS_POPUP,
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

    FreeLibrary(keycept->hModule);
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
