/*
* PROJECT:     ReactOS system libraries
* LICENSE:     GPL - See COPYING in the top level directory
* FILE:        dll/shellext/stobject/csystray.cpp
* PURPOSE:     Systray shell service object implementation
* PROGRAMMERS: David Quintana <gigaherz@gmail.com>
*              Shriraj Sawant a.k.a SR13 <sr.official@hotmail.com>
*/

#include "precomp.h"

#include <regstr.h>
#include <undocshell.h>
#include <shellutils.h>
#include <shlwapi.h>

SysTrayIconHandlers_t g_IconHandlers [] = {
    { VOLUME_SERVICE_FLAG, Volume_Init, Volume_Shutdown, Volume_Update, Volume_Message },
    { HOTPLUG_SERVICE_FLAG, Hotplug_Init, Hotplug_Shutdown, Hotplug_Update, Hotplug_Message },
    { POWER_SERVICE_FLAG, Power_Init, Power_Shutdown, Power_Update, Power_Message }
};
const int g_NumIcons = _countof(g_IconHandlers);

SysTrayIconHandlers_t g_StandaloneHandlers[] = {
    { MOUSE_SERVICE_FLAG, MouseKeys_Init, MouseKeys_Shutdown, MouseKeys_Update, MouseKeys_Message },
};

CSysTray::CSysTray() : dwServicesEnabled(0)
{
    wm_SHELLHOOK = RegisterWindowMessageW(L"SHELLHOOK");
}

CSysTray::~CSysTray()
{
}

VOID CSysTray::GetServicesEnabled()
{
    HKEY hKey;
    DWORD dwSize;

    /* Enable power, volume and hotplug by default */
    this->dwServicesEnabled = POWER_SERVICE_FLAG | VOLUME_SERVICE_FLAG | HOTPLUG_SERVICE_FLAG;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGSTR_PATH_SYSTRAY,
                        0,
                        NULL,
                        REG_OPTION_NON_VOLATILE,
                        KEY_READ,
                        NULL,
                        &hKey,
                        NULL) == ERROR_SUCCESS)
    {
        dwSize = sizeof(DWORD);
        RegQueryValueExW(hKey,
                         L"Services",
                         NULL,
                         NULL,
                         (LPBYTE)&this->dwServicesEnabled,
                         &dwSize);

        RegCloseKey(hKey);
    }
}

VOID CSysTray::EnableService(DWORD dwServiceFlag, BOOL bEnable)
{
    HKEY hKey;

    if (bEnable)
        this->dwServicesEnabled |= dwServiceFlag;
    else
        this->dwServicesEnabled &= ~dwServiceFlag;

    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        REGSTR_PATH_SYSTRAY,
                        0,
                        NULL,
                        REG_OPTION_NON_VOLATILE,
                        KEY_WRITE,
                        NULL,
                        &hKey,
                        NULL) == ERROR_SUCCESS)
    {
        DWORD dwConfig = this->dwServicesEnabled & ~STANDALONESERVICEMASK;
        RegSetValueExW(hKey,
                       L"Services",
                       0,
                       REG_DWORD,
                       (LPBYTE)&dwConfig,
                       sizeof(dwConfig));

        RegCloseKey(hKey);
    }

    ConfigurePollTimer();
}

BOOL CSysTray::IsServiceEnabled(DWORD dwServiceFlag)
{
    return (this->dwServicesEnabled & dwServiceFlag);
}

void CSysTray::ConfigurePollTimer()
{
    // FIXME: VOLUME_SERVICE_FLAG should use mixerOpen(CALLBACK_WINDOW)
    // FIXME: POWER_SERVICE_FLAG should use WM_DEVICECHANGE, WM_POWERBROADCAST

    DWORD fNeedsTimer = VOLUME_SERVICE_FLAG | POWER_SERVICE_FLAG;
    if (this->dwServicesEnabled & fNeedsTimer)
        SetTimer(POLL_TIMER_ID, 2000, NULL);
    else
        KillTimer(POLL_TIMER_ID);
}

HRESULT CSysTray::InitNetShell()
{
    HRESULT hr = CoCreateInstance(CLSID_ConnectionTray, 0, 1u, IID_PPV_ARG(IOleCommandTarget, &pctNetShell));
    if (FAILED(hr))
        return hr;

    return pctNetShell->Exec(&CGID_ShellServiceObject,
                             OLECMDID_NEW,
                             OLECMDEXECOPT_DODEFAULT, NULL, NULL);
}

HRESULT CSysTray::ShutdownNetShell()
{
    if (!pctNetShell)
        return S_FALSE;
    HRESULT hr = pctNetShell->Exec(&CGID_ShellServiceObject,
                                   OLECMDID_SAVE,
                                   OLECMDEXECOPT_DODEFAULT, NULL, NULL);
    pctNetShell.Release();
    return hr;
}

HRESULT CSysTray::InitIcons()
{
    TRACE("Initializing Notification icons...\n");
    for (UINT i = 0; i < g_NumIcons; i++)
    {
        if (this->dwServicesEnabled & g_IconHandlers[i].dwServiceFlag)
        {
            HRESULT hr = g_IconHandlers[i].pfnInit(this);
            if (FAILED(hr))
                return hr;
        }
    }
    for (UINT i = 0; i < _countof(g_StandaloneHandlers); ++i)
    {
        g_StandaloneHandlers[i].pfnInit(this);
    }

    return InitNetShell();
}

HRESULT CSysTray::ShutdownIcons()
{
    TRACE("Shutting down Notification icons...\n");
    for (UINT i = 0; i < g_NumIcons; i++)
    {
        if (this->dwServicesEnabled & g_IconHandlers[i].dwServiceFlag)
        {
            this->dwServicesEnabled &= ~g_IconHandlers[i].dwServiceFlag;
            HRESULT hr = g_IconHandlers[i].pfnShutdown(this);
            FAILED_UNEXPECTEDLY(hr);
        }
    }
    for (UINT i = 0; i < _countof(g_StandaloneHandlers); ++i)
    {
        g_StandaloneHandlers[i].pfnShutdown(this);
    }

    return ShutdownNetShell();
}

HRESULT CSysTray::UpdateIcons()
{
    TRACE("Updating Notification icons...\n");
    for (int i = 0; i < g_NumIcons; i++)
    {
        if (this->dwServicesEnabled & g_IconHandlers[i].dwServiceFlag)
        {
            HRESULT hr = g_IconHandlers[i].pfnUpdate(this);
            if (FAILED(hr))
                return hr;
        }
    }

    return S_OK;
}

HRESULT CSysTray::ProcessIconMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult)
{
    for (UINT i = 0; i < g_NumIcons; i++)
    {
        HRESULT hr = g_IconHandlers[i].pfnMessage(this, uMsg, wParam, lParam, lResult);
        if (FAILED(hr))
            return hr;
        if (hr == S_OK)
            return hr;
    }
    for (UINT i = 0; i < _countof(g_StandaloneHandlers); ++i)
    {
        HRESULT hr = g_StandaloneHandlers[i].pfnMessage(this, uMsg, wParam, lParam, lResult);
        if (FAILED(hr))
            return hr;
        if (hr == S_OK)
            return hr;
    }

    // Not handled by anyone, so return accordingly.
    return S_FALSE;
}

/*++
* @name NotifyIcon
*
* Basically a Shell_NotifyIcon wrapper.
* Based on the parameters provided, it changes the current state of the notification icon.
*
* @param code
*        Determines whether to add, delete or modify the notification icon (represented by uId).
* @param uId
*        Represents the particular notification icon.
* @param hIcon
*        A handle to an icon for the notification object.
* @param szTip
*        A string for the tooltip of the notification.
* @param dwstate
*        Determines whether to show or hide the notification icon.
*
* @return The error code.
*
*--*/
HRESULT CSysTray::NotifyIcon(INT code, UINT uId, HICON hIcon, LPCWSTR szTip, DWORD dwstate)
{
    NOTIFYICONDATA nim = { 0 };

    TRACE("NotifyIcon code=%d, uId=%d, hIcon=%p, szTip=%S\n", code, uId, hIcon, szTip);

    nim.cbSize = sizeof(nim);
    nim.uFlags = NIF_MESSAGE | NIF_ICON | NIF_STATE | NIF_TIP;
    nim.hIcon = hIcon;
    nim.uID = uId;
    nim.uCallbackMessage = uId;
    nim.dwState = dwstate;
    nim.dwStateMask = NIS_HIDDEN;
    nim.hWnd = m_hWnd;
    nim.uVersion = NOTIFYICON_VERSION;
    if (szTip)
        StringCchCopy(nim.szTip, _countof(nim.szTip), szTip);
    else
        nim.szTip[0] = 0;
    BOOL ret = Shell_NotifyIcon(code, &nim);
    return ret ? S_OK : E_FAIL;
}

DWORD WINAPI CSysTray::s_SysTrayThreadProc(PVOID param)
{
    CSysTray * st = (CSysTray*) param;
    return st->SysTrayThreadProc();
}

HRESULT CSysTray::SysTrayMessageLoop()
{
    BOOL ret;
    MSG msg;

    while ((ret = GetMessage(&msg, NULL, 0, 0)) != 0)
    {
        if (ret < 0)
            break;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return S_OK;
}

HRESULT CSysTray::SysTrayThreadProc()
{
    WCHAR strFileName[MAX_PATH];
    GetModuleFileNameW(g_hInstance, strFileName, MAX_PATH);
    HMODULE hLib = LoadLibraryW(strFileName);

    CoInitializeEx(NULL, COINIT_DISABLE_OLE1DDE | COINIT_APARTMENTTHREADED);

    Create(NULL);

    HRESULT ret = SysTrayMessageLoop();

    CoUninitialize();

    Release();
    FreeLibraryAndExitThread(hLib, ret);
}

HRESULT CSysTray::CreateSysTrayThread()
{
    TRACE("CSysTray Init TODO: Initialize tray icon handlers.\n");
    AddRef();

    HANDLE hThread = CreateThread(NULL, 0, s_SysTrayThreadProc, this, 0, NULL);

    CloseHandle(hThread);

    return S_OK;
}

HRESULT CSysTray::DestroySysTrayWindow()
{
    if (!DestroyWindow())
    {
        // Window is from another thread, ask it politely to destroy itself:
        SendMessage(WM_CLOSE);
    }
    return S_OK;
}

// *** IOleCommandTarget methods ***
HRESULT STDMETHODCALLTYPE CSysTray::QueryStatus(const GUID *pguidCmdGroup, ULONG cCmds, OLECMD prgCmds [], OLECMDTEXT *pCmdText)
{
    UNIMPLEMENTED;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CSysTray::Exec(const GUID *pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT *pvaIn, VARIANT *pvaOut)
{
    if (!IsEqualGUID(*pguidCmdGroup, CGID_ShellServiceObject))
        return E_FAIL;

    switch (nCmdID)
    {
    case OLECMDID_NEW: // init
        return CreateSysTrayThread();
    case OLECMDID_SAVE: // shutdown
        return DestroySysTrayWindow();
    }
    return S_OK;
}

BOOL CSysTray::ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult, DWORD dwMsgMapID)
{
    HRESULT hr;

    if (hWnd != m_hWnd)
        return FALSE;

    if (uMsg == wm_SHELLHOOK && wm_SHELLHOOK)
    {
        if (wParam == HSHELL_ACCESSIBILITYSTATE && lParam == ACCESS_STICKYKEYS)
        {
            StickyKeys_Update(this);
        }
        else if (wParam == HSHELL_ACCESSIBILITYSTATE && lParam == ACCESS_MOUSEKEYS)
        {
            MouseKeys_Update(this);
        }
        lResult = 0L;
        return TRUE;
    }

    switch (uMsg)
    {
    case WM_NCCREATE:
    case WM_NCDESTROY:
        return FALSE;

    case WM_CLOSE:
        return DestroyWindow();

    case WM_CREATE:
        GetServicesEnabled();
        InitIcons();
        RegisterShellHookWindow(hWnd);
        ConfigurePollTimer();
        return TRUE;

    case WM_TIMER:
        if (wParam == POLL_TIMER_ID)
            UpdateIcons();
        else
            ProcessIconMessage(uMsg, wParam, lParam, lResult);
        return TRUE;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETSTICKYKEYS)
            StickyKeys_Update(this);
        if (wParam == SPI_SETMOUSEKEYS)
            MouseKeys_Update(this);
        break;

    case WM_DESTROY:
        KillTimer(POLL_TIMER_ID);
        DeregisterShellHookWindow(hWnd);
        ShutdownIcons();
        PostQuitMessage(0);
        return TRUE;
    }

    TRACE("SysTray message received %u (%08p %08p)\n", uMsg, wParam, lParam);

    hr = ProcessIconMessage(uMsg, wParam, lParam, lResult);
    if (FAILED(hr))
        return FALSE;

    return (hr == S_OK);
}

void CSysTray::RunDll(PCSTR Dll, PCSTR Parameters)
{
    WCHAR buf[400], rundll[MAX_PATH];
    GetSystemDirectory(rundll, _countof(rundll));
    PathAppendW(rundll, L"rundll32.exe");

    wsprintfW(buf, L"%hs %hs%hs", "shell32.dll,Control_RunDLL", Dll, Parameters);
    ShellExecuteW(NULL, NULL, rundll, buf, NULL, SW_SHOW);
}

void CSysTray::RunAccessCpl(PCSTR Parameters)
{
    RunDll("access.cpl", Parameters);
}
