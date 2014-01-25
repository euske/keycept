// -*- tab-width: 4 -*-
// Hookey.h

#include <windows.h>

typedef struct _KeyHookEntry
{
    DWORD vkCode0;
    DWORD scanCode0;
    DWORD vkCode1;
    DWORD scanCode1;
} KeyHookEntry;

typedef void (*pSetLogFile)(FILE* logfp);
typedef void (*pSetKeyHooks)(KeyHookEntry* entries, unsigned int nentries);

typedef struct _HookeyDLL
{
    pSetLogFile SetLogFile;
    pSetKeyHooks SetKeyHooks;
} HookeyDLL;
