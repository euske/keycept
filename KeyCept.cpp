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

// setClipboardText(hwnd, text)
//   Copy the text to the clipboard.
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
    WCHAR defaultConfig[1024];
    UINT timerInterval;
    HWND dialogHWnd;
    BOOL enabled;
    KeyCeptHook* hooks;
} KeyCeptSettings;

//  keyceptCreateHook
//    Creates a new KeyCeptHook structure.
static KeyCeptHook* keyceptCreateHook(const wchar_t* name)
{
    KeyCeptHook* hook = (KeyCeptHook*) calloc(1, sizeof(KeyCeptHook));
    if (hook == NULL) exit(111);

    hook->className = wcsdup(name);
    hook->maxentries = MAX_KEY_ENTRIES;
    hook->nentries = 0;
    hook->entries = (KeyHookEntry*) calloc(hook->maxentries, sizeof(KeyHookEntry));
    if (hook->entries == NULL) exit(111);

    return hook;
}

//  keyceptINIHandler
//   Called for each config line.
static int keyceptINIHandler(void* user, const char* section,
                             const char* name, const char* value)
{
    KeyCeptSettings* settings = (KeyCeptSettings*)user;
    fprintf(stderr, "section=%s, name=%s, value=%s\n", section, name, value);

    KeyCeptHook* found = NULL;
    if (stricmp(section, "global") == 0) {
        // [global] enabled: special option.
        if (stricmp(name, "enabled") == 0) {
            settings->enabled = atoi(value);
            return 1;
        }
        // global - first hook.
        found = settings->hooks;
    } else {
        // Search from the second hook.
        wchar_t wsection[256];
        mbstowcs_s(NULL, wsection, _countof(wsection), section, _TRUNCATE);
        for (KeyCeptHook* hooks = settings->hooks->next;
             hooks != NULL;
             hooks = hooks->next) {
            if (wcscmp(hooks->className, wsection) == 0) {
                found = hooks;
                break;
            }
        }
        // Create one if it doesn't exist.
        if (found == NULL) {
            found = keyceptCreateHook(wsection);
            // Insert it as a second hook. 
            // (The first one is reserved for global.)
            found->next = settings->hooks->next;
            settings->hooks->next = found;
        }
    }

    // Add an entry.
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
    return 0;
}

//  keyceptClearConfig
//    Remove every hook.
static void keyceptClearConfig(KeyCeptSettings* settings)
{
    while (settings->hooks != NULL) {
        KeyCeptHook* hook = settings->hooks;
        free(hook->className);
        free(hook->entries);
        settings->hooks = hook->next;
    }
}

//  keyceptLoadConfig
//    Loads .ini file.
static BOOL keyceptLoadConfig(KeyCeptSettings* settings)
{
    fwprintf(stderr, L"loadConfig=%s\n", settings->configPath);

    // Clear all the hooks.
    keyceptClearConfig(settings);
    // Add the global hook as the first hook.
    settings->hooks = keyceptCreateHook(L"global");

    // Open the .ini file.
    {
        FILE* fp = NULL;
        if (_wfopen_s(&fp, settings->configPath, L"r") == 0) {
            ini_parse_file(fp, keyceptINIHandler, settings);
            fclose(fp);
        }
    }
    return TRUE;
}

//  keyceptCreateDefaultConfig
//    Create a default .ini file.
static void keyceptCreateDefaultConfig(KeyCeptSettings* settings)
{
    fwprintf(stderr, L"createDefaultConfig=%s\n", settings->configPath);
    
    // Create the config file.
    {
        FILE* fp = NULL;
        if (_wfopen_s(&fp, settings->configPath, L"w") == 0) {
            fputws(settings->defaultConfig, fp);
            fclose(fp);
        }
    }
}

//  keyceptUpdateStatus
//    Switch the active keyboard hook.
static KeyCeptHook* keyceptUpdateStatus(KeyCeptSettings* settings, HWND hWnd)
{
    KeyCeptHook* found = settings->hooks;

    // Find the hook to activate.
    WCHAR name[256];
    if (GetClassName(hWnd, name, _countof(name)-1)) {
        for (KeyCeptHook* hooks = settings->hooks->next;
             hooks != NULL;
             hooks = hooks->next) {
            if (wcscmp(hooks->className, name) == 0) {
                found = hooks;
                break;
            }
        }
    }

    if (found != NULL && 0 < found->nentries) {
        // Activate the hook.
        fwprintf(stderr, L"name=%s\n", found->className);
        for (unsigned int i = 0 ; i < found->nentries; i++) {
            KeyHookEntry* entry = &(found->entries[i]);
            fwprintf(stderr, L" %d:%d = %d:%d\n", 
                     entry->vkCode0, entry->scanCode0,
                     entry->vkCode1, entry->scanCode1);
        }
        settings->hookeyDLL.SetKeyHooks(found->entries, found->nentries);
        return found;
    }
     
    // Deactivate the hook.
    settings->hookeyDLL.SetKeyHooks(NULL, 0);
    return NULL;
}


//  keyceptDialogProc
//    Handle the config modeless dialog.
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
        // Just hide the window instead of closing.
	ShowWindow(hWnd, SW_HIDE);
	return FALSE;

    case WM_COMMAND:
    {
        // Respond to menu/button events.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptDialog* self = (KeyCeptDialog*)lp;

	switch (LOWORD(wParam)) {
        case IDC_BUTTON_RELOAD:
            // Reload the config file.
            SendMessage(GetParent(hWnd), WM_USER_CONFIG_CHANGED, 0, 0);
            break;

        case IDC_BUTTON_OPEN:
        {
            // Open the config file in Explorer.
            if (self != NULL) {
                LPCWSTR path = self->settings->configPath;
                // Create one if the file doesn't exist.
                if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) {
                    keyceptCreateDefaultConfig(self->settings);
                }
                ShellExecute(NULL, L"open", path, NULL, NULL, SW_SHOWDEFAULT);
            }
            break;
        }

        case IDC_BUTTON_COPY_KEYCODE:
        {
            // Copy the keycode.
            WCHAR text[256];
            if (GetDlgItemText(hWnd, IDC_TEXT_KEYCODE, text, _countof(text))) {
                setClipboardText(hWnd, text);
            }
            break;
        }

        case IDC_BUTTON_COPY_WINDOW:
        {
            // Copy the window class name.
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
        // Show the keycode of the last key pressed.
        DWORD vkCode = (DWORD)wParam;
        DWORD scanCode = (DWORD)lParam;
        WCHAR text[256];
        StringCchPrintf(text, _countof(text), L"Key %d:%d", vkCode, scanCode);
        SetDlgItemText(hWnd, IDC_TEXT_KEYCODE, text);
        return FALSE;
    }

    case WM_USER_WINDOW_CHANGED:
    {
        // Show the window class name of the current foreground window.
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
//    Handles the tray icon.
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
        // Load the dialog resource.
        self->settings->dialogHWnd = CreateDialogParam(
            cs->hInstance,
            MAKEINTRESOURCE(IDD_CONFIG_PANEL),
            hWnd, keyceptDialogProc, (LPARAM)self->settings);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);

        // Set the tray icon.
        NOTIFYICONDATA nidata = {0};
        nidata.cbSize = sizeof(nidata);
        nidata.hWnd = hWnd;
        nidata.uID = self->iconId;
        nidata.uFlags = NIF_MESSAGE;
        nidata.uCallbackMessage = WM_USER_ICON_EVENT;
        Shell_NotifyIcon(NIM_ADD, &nidata);

        // Set the timer.
        SetTimer(hWnd, self->timerId, self->settings->timerInterval, NULL);
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
        // Exit the program.
	PostQuitMessage(0);
	return FALSE;
    }

    case WM_CLOSE:
	DestroyWindow(hWnd);
	return FALSE;

    case WM_COMMAND:
    {
        // Respond to menu choices.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;

	switch (LOWORD(wParam)) {
        case IDM_TOGGLE:
            // Toggle enable/disable the function.
            if (self != NULL) {
                self->settings->enabled = !(self->settings->enabled);
                SendMessage(hWnd, WM_USER_STATE_CHANGED, 0, 0);
            }
            break;

        case IDM_CONFIG:
            // Open the config dialog.
            if (self != NULL && self->settings->dialogHWnd != NULL) {
                ShowWindow(self->settings->dialogHWnd, SW_SHOWDEFAULT);
            }
            break;

	case IDM_EXIT:
            // Exiting.
	    SendMessage(hWnd, WM_CLOSE, 0, 0);
	    break;
	}
	return FALSE;
    }

    case WM_TIMER:
    {
        // Periodically called to monitor the keyboard/window status.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            // Check if the last keycode is changed.
            DWORD vkCode, scanCode;
            self->settings->hookeyDLL.GetLastKey(&vkCode, &scanCode);
            if (self->lastVkCode != vkCode ||
                self->lastScanCode != scanCode) {
                self->lastVkCode = vkCode;
                self->lastScanCode = scanCode;
                SendMessage(hWnd, WM_USER_KEYCODE_CHANGED, 
                            (WPARAM)vkCode, (LPARAM)scanCode);
            }
            // Check if the foreground window is changed.
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
        // Respond to a config file change.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            keyceptLoadConfig(self->settings);
            // Refresh the current hook.
            SendMessage(hWnd, WM_USER_STATE_CHANGED, 0, 0);
        }
        return FALSE;
    }

    case WM_USER_STATE_CHANGED:
    {
        // Respond to a status change.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            HMENU menu = GetMenu(hWnd);
            if (menu != NULL) {
                menu = GetSubMenu(menu, 0);
                if (menu != NULL) {
                    // Check/uncheck the menu item.
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
            // Refresh the current hook.
            KeyCeptHook* active = keyceptUpdateStatus(self->settings, self->lastFHWnd);
            SendMessage(hWnd, WM_USER_ICON_CHANGED, 0, (LPARAM)active);
        }
        return FALSE;
    }

    case WM_USER_WINDOW_CHANGED:
    {
        // Respond to a foreground window change.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            // Forward the event to the dialog.
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
        // Respond to a keycode change.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            // Forward the event to the dialog.
            if (self->settings->dialogHWnd != NULL) {
                SendMessage(self->settings->dialogHWnd, uMsg, wParam, lParam);
            }
        }
        return FALSE;
    }

    case WM_USER_ICON_CHANGED:
    {
        // Change the icon and status text.
	LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
        KeyCeptTray* self = (KeyCeptTray*)lp;
        if (self != NULL) {
            // LPARAM: active hook.
            KeyCeptHook* active = (KeyCeptHook*)lParam;
            NOTIFYICONDATA nidata = {0};
            nidata.cbSize = sizeof(nidata);
            nidata.hWnd = hWnd;
            nidata.uID = self->iconId;
            nidata.uFlags = NIF_ICON | NIF_TIP;
            // Set the icon and status text.
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
        // Respond to tray icon events.
	POINT pt;
        HMENU menu = GetMenu(hWnd);
        if (menu != NULL) {
            menu = GetSubMenu(menu, 0);
        }
	switch (lParam) {
	case WM_LBUTTONDBLCLK:
            // Double click - choose the default item.
            if (menu != NULL) {
                UINT item = GetMenuDefaultItem(menu, FALSE, 0);
                SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(item, 1), NULL);
            }
	    break;
	case WM_LBUTTONUP:
	    break;
	case WM_RBUTTONUP:
            // Right click - open the popup menu.
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
    KeyCeptSettings* settings = (KeyCeptSettings*) calloc(1, sizeof(KeyCeptSettings));
    if (settings == NULL) return 111;
    settings->timerInterval = 200;
    settings->trayName = L"KeyCept Tray";

    // Load a DLL.
    settings->hModule = LoadLibrary(L"hookey.dll");
    if (settings->hModule == NULL) {
        MessageBox(NULL, 
                   L"hookey.dll is not found.", 
                   L"KeyCept", 
                   MB_ICONERROR | MB_OK);
        return 111;
    }
    settings->hookeyDLL.SetLogFile = \
        (pSetLogFile) GetProcAddress(settings->hModule, "SetLogFile");
    settings->hookeyDLL.SetKeyHooks = \
        (pSetKeyHooks) GetProcAddress(settings->hModule, "SetKeyHooks");
    settings->hookeyDLL.GetLastKey = \
        (pGetLastKey) GetProcAddress(settings->hModule, "GetLastKey");
    settings->hookeyDLL.SetLogFile(logfp);

    // Load resources.
    settings->iconKeyCeptOn = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_ON));
    if (settings->iconKeyCeptOn == NULL) return 111;
    settings->iconKeyCeptOff = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_OFF));
    if (settings->iconKeyCeptOff == NULL) return 111;
    settings->iconKeyCeptDisabled = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KEYCEPT_DISABLED));
    if (settings->iconKeyCeptDisabled == NULL) return 111;
    LoadString(hInstance, IDS_DEFAULT_CONFIG, 
               settings->defaultConfig, _countof(settings->defaultConfig));
    
    // Set the ini file path.
    WCHAR path[MAX_PATH];
    WCHAR basedir[MAX_PATH];
    if (GetCurrentDirectory(_countof(basedir), basedir)) {
        StringCchPrintf(path, _countof(path), L"%s\\KeyCept.ini", basedir);
        settings->configPath = path;
    }
    if (2 <= argc) {
        settings->configPath = argv[1];
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
	settings->trayWindowAtom = RegisterClass(&wc);
    }

    // Create a SysTray window.
    HWND hWnd = CreateWindowEx(
        WS_EX_NOACTIVATE,
	(LPCWSTR)settings->trayWindowAtom,
	KEYCEPT_NAME,
	WS_POPUP,
	CW_USEDEFAULT, CW_USEDEFAULT,
	CW_USEDEFAULT, CW_USEDEFAULT,
	NULL, NULL, hInstance, settings);
    UpdateWindow(hWnd);
    SendMessage(hWnd, WM_USER_CONFIG_CHANGED, 0, 0);
    SendMessage(hWnd, WM_USER_STATE_CHANGED, 0, 0);
    
    // Event loop.
    MSG msg;
    while (0 < GetMessage(&msg, NULL, 0, 0)) {
        if (settings->dialogHWnd != NULL &&
            IsDialogMessage(settings->dialogHWnd, &msg)) {
            // do nothing.
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    FreeLibrary(settings->hModule);
    keyceptClearConfig(settings);
    free(settings);

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
