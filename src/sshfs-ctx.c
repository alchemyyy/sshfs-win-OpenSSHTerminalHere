/**
 * sshfs-ctx.c
 *
 * Shell extension DLL for SSHFS-Win context menu
 * Provides "Open SSH Terminal Here" option only on SSHFS mounted drives
 *
 * This is a native Windows COM shell extension - no Cygwin dependencies
 * Compile with: cl /LD /O2 sshfs-ctx.c /link ole32.lib shell32.lib shlwapi.lib advapi32.lib mpr.lib
 * Or MinGW: gcc -shared -o sshfs-ctx.dll sshfs-ctx.c -lole32 -lshell32 -lshlwapi -ladvapi32 -lmpr -luuid
 */

#define COBJMACROS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <strsafe.h>

/* Resource ID for embedded icon */
#define IDI_MENUICON 101

/* {7B3F4E8A-1C2D-4E5F-9A8B-0C1D2E3F4A5B} */
static const GUID CLSID_SSHFSContextMenu = 
    {0x7b3f4e8a, 0x1c2d, 0x4e5f, {0x9a, 0x8b, 0x0c, 0x1d, 0x2e, 0x3f, 0x4a, 0x5b}};

static HINSTANCE g_hInstance = NULL;
static LONG g_RefCount = 0;
static HBITMAP g_hMenuBitmap = NULL;

/* Forward declarations */
typedef struct SSHFSContextMenu SSHFSContextMenu;

/* ------------------------------------------------------------------------- */
/* Utility Functions                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Check if a path is on an SSHFS mount by examining its UNC path
 * Returns TRUE if the path is on an SSHFS mount
 */
static BOOL IsSSHFSPath(LPCWSTR pszPath)
{
    WCHAR szDrive[4] = {0};
    WCHAR szUNCPath[MAX_PATH] = {0};
    DWORD dwLen = MAX_PATH;
    DWORD dwResult;

    if (!pszPath || !pszPath[0])
        return FALSE;

    /* Handle UNC paths directly */
    if (pszPath[0] == L'\\' && pszPath[1] == L'\\')
    {
        /* Check if UNC path starts with \\sshfs\ or \\sshfs. */
        if (_wcsnicmp(pszPath, L"\\\\sshfs\\", 8) == 0 ||
            _wcsnicmp(pszPath, L"\\\\sshfs.", 8) == 0)
        {
            return TRUE;
        }
        return FALSE;
    }

    /* For drive letter paths, get the UNC connection */
    if (pszPath[1] == L':')
    {
        szDrive[0] = pszPath[0];
        szDrive[1] = L':';
        szDrive[2] = L'\0';

        dwResult = WNetGetConnectionW(szDrive, szUNCPath, &dwLen);
        if (dwResult == NO_ERROR)
        {
            /* Check if UNC path starts with \\sshfs\ or \\sshfs. */
            if (_wcsnicmp(szUNCPath, L"\\\\sshfs\\", 8) == 0 ||
                _wcsnicmp(szUNCPath, L"\\\\sshfs.", 8) == 0)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/**
 * Convert an HICON to HBITMAP with alpha channel for menu display
 * Windows Vista+ menus require 32-bit ARGB bitmaps for proper transparency
 */
static HBITMAP IconToBitmap(HICON hIcon, int cx, int cy)
{
    BITMAPINFO bmi = {0};
    HDC hdcScreen, hdcMem;
    HBITMAP hBitmap;
    void *pvBits;

    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy;  /* Top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (hBitmap)
    {
        HBITMAP hOldBmp = SelectObject(hdcMem, hBitmap);
        /* Fill with transparent black */
        PatBlt(hdcMem, 0, 0, cx, cy, BLACKNESS);
        DrawIconEx(hdcMem, 0, 0, hIcon, cx, cy, 0, NULL, DI_NORMAL);
        SelectObject(hdcMem, hOldBmp);
    }

    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hBitmap;
}

/**
 * Load the menu icon from embedded resource
 * Returns cached bitmap on subsequent calls
 */
static HBITMAP GetMenuBitmap(void)
{
    HICON hIcon;
    int cx, cy;

    if (g_hMenuBitmap)
        return g_hMenuBitmap;

    /* Get system menu icon size */
    cx = GetSystemMetrics(SM_CXSMICON);
    cy = GetSystemMetrics(SM_CYSMICON);

    /* Load icon from embedded resource */
    hIcon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_MENUICON), 
        IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (hIcon)
    {
        g_hMenuBitmap = IconToBitmap(hIcon, cx, cy);
        DestroyIcon(hIcon);
    }

    return g_hMenuBitmap;
}

/**
 * Get the installation directory from registry
 */
static BOOL GetInstallDir(LPWSTR pszPath, DWORD cchPath)
{
    HKEY hKey;
    DWORD dwType, dwSize;
    LONG lResult;

    lResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
        L"SOFTWARE\\SSHFS-Win", 0, KEY_READ | KEY_WOW64_64KEY, &hKey);
    
    if (lResult != ERROR_SUCCESS)
    {
        lResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\SSHFS-Win", 0, KEY_READ | KEY_WOW64_32KEY, &hKey);
    }
    
    if (lResult != ERROR_SUCCESS)
    {
        /* Fallback to default */
        StringCchCopyW(pszPath, cchPath, L"C:\\Program Files\\SSHFS-Win\\");
        return TRUE;
    }

    dwSize = cchPath * sizeof(WCHAR);
    lResult = RegQueryValueExW(hKey, L"InstallDir", NULL, &dwType, 
        (LPBYTE)pszPath, &dwSize);
    RegCloseKey(hKey);

    if (lResult != ERROR_SUCCESS)
    {
        StringCchCopyW(pszPath, cchPath, L"C:\\Program Files\\SSHFS-Win\\");
    }

    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* IUnknown Implementation                                                    */
/* ------------------------------------------------------------------------- */

struct SSHFSContextMenu
{
    IContextMenuVtbl *lpVtbl;
    IShellExtInitVtbl *lpVtblShellExtInit;
    LONG m_RefCount;
    WCHAR m_szPath[MAX_PATH];
    BOOL m_bIsSSHFS;
};

static HRESULT STDMETHODCALLTYPE ContextMenu_QueryInterface(
    IContextMenu *This, REFIID riid, void **ppvObject)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)This;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IContextMenu))
    {
        *ppvObject = &pExt->lpVtbl;
        InterlockedIncrement(&pExt->m_RefCount);
        return S_OK;
    }
    else if (IsEqualIID(riid, &IID_IShellExtInit))
    {
        *ppvObject = &pExt->lpVtblShellExtInit;
        InterlockedIncrement(&pExt->m_RefCount);
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ContextMenu_AddRef(IContextMenu *This)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)This;
    return InterlockedIncrement(&pExt->m_RefCount);
}

static ULONG STDMETHODCALLTYPE ContextMenu_Release(IContextMenu *This)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)This;
    LONG ref = InterlockedDecrement(&pExt->m_RefCount);
    if (ref == 0)
    {
        CoTaskMemFree(pExt);
        InterlockedDecrement(&g_RefCount);
    }
    return ref;
}

/* ------------------------------------------------------------------------- */
/* IShellExtInit Implementation                                               */
/* ------------------------------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE ShellExtInit_QueryInterface(
    IShellExtInit *This, REFIID riid, void **ppvObject)
{
    /* Adjust pointer to get the main object */
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)((BYTE *)This - 
        offsetof(SSHFSContextMenu, lpVtblShellExtInit));
    return ContextMenu_QueryInterface((IContextMenu *)pExt, riid, ppvObject);
}

static ULONG STDMETHODCALLTYPE ShellExtInit_AddRef(IShellExtInit *This)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)((BYTE *)This - 
        offsetof(SSHFSContextMenu, lpVtblShellExtInit));
    return ContextMenu_AddRef((IContextMenu *)pExt);
}

static ULONG STDMETHODCALLTYPE ShellExtInit_Release(IShellExtInit *This)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)((BYTE *)This - 
        offsetof(SSHFSContextMenu, lpVtblShellExtInit));
    return ContextMenu_Release((IContextMenu *)pExt);
}

static HRESULT STDMETHODCALLTYPE ShellExtInit_Initialize(
    IShellExtInit *This,
    PCIDLIST_ABSOLUTE pidlFolder,
    IDataObject *pdtobj,
    HKEY hkeyProgID)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)((BYTE *)This - 
        offsetof(SSHFSContextMenu, lpVtblShellExtInit));
    FORMATETC fmt = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg = {0};
    HDROP hDrop;
    HRESULT hr;

    pExt->m_szPath[0] = L'\0';
    pExt->m_bIsSSHFS = FALSE;

    /* Try to get the folder from data object (selected item) */
    if (pdtobj)
    {
        hr = IDataObject_GetData(pdtobj, &fmt, &stg);
        if (SUCCEEDED(hr))
        {
            hDrop = (HDROP)GlobalLock(stg.hGlobal);
            if (hDrop)
            {
                if (DragQueryFileW(hDrop, 0, pExt->m_szPath, MAX_PATH) > 0)
                {
                    pExt->m_bIsSSHFS = IsSSHFSPath(pExt->m_szPath);
                }
                GlobalUnlock(stg.hGlobal);
            }
            ReleaseStgMedium(&stg);
            
            if (pExt->m_bIsSSHFS)
                return S_OK;
        }
    }

    /* Try to get from pidlFolder (background click) */
    if (pidlFolder)
    {
        if (SHGetPathFromIDListW(pidlFolder, pExt->m_szPath))
        {
            pExt->m_bIsSSHFS = IsSSHFSPath(pExt->m_szPath);
        }
    }

    return S_OK;
}

static IShellExtInitVtbl g_ShellExtInitVtbl = {
    ShellExtInit_QueryInterface,
    ShellExtInit_AddRef,
    ShellExtInit_Release,
    ShellExtInit_Initialize
};

/* ------------------------------------------------------------------------- */
/* IContextMenu Implementation                                                */
/* ------------------------------------------------------------------------- */

#define IDM_OPENSSH 0

static HRESULT STDMETHODCALLTYPE ContextMenu_QueryContextMenu(
    IContextMenu *This,
    HMENU hmenu,
    UINT indexMenu,
    UINT idCmdFirst,
    UINT idCmdLast,
    UINT uFlags)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)This;
    MENUITEMINFOW mii = {0};
    HBITMAP hBmp;

    /* Only add menu if this is an SSHFS path */
    if (!pExt->m_bIsSSHFS)
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    /* Don't add for default verb only requests */
    if (uFlags & CMF_DEFAULTONLY)
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
    mii.fState = MFS_ENABLED;
    mii.wID = idCmdFirst + IDM_OPENSSH;
    mii.dwTypeData = L"Open SSH Terminal Here";

    /* Add icon if available */
    hBmp = GetMenuBitmap();
    if (hBmp)
    {
        mii.fMask |= MIIM_BITMAP;
        mii.hbmpItem = hBmp;
    }

    /* Insert at position 0 to place at top of context menu */
    InsertMenuItemW(hmenu, 0, TRUE, &mii);

    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, IDM_OPENSSH + 1);
}

static HRESULT STDMETHODCALLTYPE ContextMenu_InvokeCommand(
    IContextMenu *This,
    CMINVOKECOMMANDINFO *pici)
{
    SSHFSContextMenu *pExt = (SSHFSContextMenu *)This;
    WCHAR szInstallDir[MAX_PATH];
    WCHAR szExePath[MAX_PATH];
    WCHAR szCmdLine[MAX_PATH * 2];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};

    /* Check if invoked by command ID (not verb string) */
    if (HIWORD(pici->lpVerb) != 0)
        return E_INVALIDARG;

    if (LOWORD(pici->lpVerb) != IDM_OPENSSH)
        return E_INVALIDARG;

    if (!pExt->m_bIsSSHFS || !pExt->m_szPath[0])
        return E_FAIL;

    /* Build path to sshfs-ssh.exe */
    GetInstallDir(szInstallDir, MAX_PATH);
    StringCchPrintfW(szExePath, MAX_PATH, L"%susr\\bin\\sshfs-ssh.exe", szInstallDir);

    /* Build command line with quoted path */
    StringCchPrintfW(szCmdLine, MAX_PATH * 2, L"\"%s\" \"%s\"", szExePath, pExt->m_szPath);

    /* Launch the SSH terminal opener */
    si.cb = sizeof(si);
    if (!CreateProcessW(szExePath, szCmdLine, NULL, NULL, FALSE, 
        0, NULL, NULL, &si, &pi))
    {
        /* Fallback: try without full path (if in PATH) */
        StringCchPrintfW(szCmdLine, MAX_PATH * 2, L"sshfs-ssh.exe \"%s\"", pExt->m_szPath);
        if (!CreateProcessW(NULL, szCmdLine, NULL, NULL, FALSE,
            0, NULL, NULL, &si, &pi))
        {
            MessageBoxW(NULL, L"Failed to launch SSH terminal.\n\n"
                L"Make sure SSHFS-Win is properly installed.",
                L"SSHFS-Win", MB_OK | MB_ICONERROR);
            return E_FAIL;
        }
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ContextMenu_GetCommandString(
    IContextMenu *This,
    UINT_PTR idCmd,
    UINT uType,
    UINT *pReserved,
    CHAR *pszName,
    UINT cchMax)
{
    if (idCmd != IDM_OPENSSH)
        return E_INVALIDARG;

    switch (uType)
    {
    case GCS_HELPTEXTA:
        StringCchCopyA(pszName, cchMax, "Open an SSH terminal to this location");
        return S_OK;
    case GCS_HELPTEXTW:
        StringCchCopyW((LPWSTR)pszName, cchMax, L"Open an SSH terminal to this location");
        return S_OK;
    case GCS_VERBA:
        StringCchCopyA(pszName, cchMax, "sshfs_openssh");
        return S_OK;
    case GCS_VERBW:
        StringCchCopyW((LPWSTR)pszName, cchMax, L"sshfs_openssh");
        return S_OK;
    }

    return E_INVALIDARG;
}

static IContextMenuVtbl g_ContextMenuVtbl = {
    ContextMenu_QueryInterface,
    ContextMenu_AddRef,
    ContextMenu_Release,
    ContextMenu_QueryContextMenu,
    ContextMenu_InvokeCommand,
    ContextMenu_GetCommandString
};

/* ------------------------------------------------------------------------- */
/* Class Factory Implementation                                               */
/* ------------------------------------------------------------------------- */

typedef struct ClassFactory
{
    IClassFactoryVtbl *lpVtbl;
    LONG m_RefCount;
} ClassFactory;

static HRESULT STDMETHODCALLTYPE ClassFactory_QueryInterface(
    IClassFactory *This, REFIID riid, void **ppvObject)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppvObject = This;
        IClassFactory_AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ClassFactory_AddRef(IClassFactory *This)
{
    ClassFactory *pFactory = (ClassFactory *)This;
    return InterlockedIncrement(&pFactory->m_RefCount);
}

static ULONG STDMETHODCALLTYPE ClassFactory_Release(IClassFactory *This)
{
    ClassFactory *pFactory = (ClassFactory *)This;
    LONG ref = InterlockedDecrement(&pFactory->m_RefCount);
    if (ref == 0)
    {
        CoTaskMemFree(pFactory);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE ClassFactory_CreateInstance(
    IClassFactory *This, IUnknown *pUnkOuter, REFIID riid, void **ppvObject)
{
    SSHFSContextMenu *pExt;
    HRESULT hr;

    *ppvObject = NULL;

    if (pUnkOuter != NULL)
        return CLASS_E_NOAGGREGATION;

    pExt = CoTaskMemAlloc(sizeof(SSHFSContextMenu));
    if (!pExt)
        return E_OUTOFMEMORY;

    ZeroMemory(pExt, sizeof(SSHFSContextMenu));
    pExt->lpVtbl = &g_ContextMenuVtbl;
    pExt->lpVtblShellExtInit = &g_ShellExtInitVtbl;
    pExt->m_RefCount = 1;
    InterlockedIncrement(&g_RefCount);

    hr = ContextMenu_QueryInterface((IContextMenu *)pExt, riid, ppvObject);
    ContextMenu_Release((IContextMenu *)pExt);

    return hr;
}

static HRESULT STDMETHODCALLTYPE ClassFactory_LockServer(
    IClassFactory *This, BOOL fLock)
{
    if (fLock)
        InterlockedIncrement(&g_RefCount);
    else
        InterlockedDecrement(&g_RefCount);
    return S_OK;
}

static IClassFactoryVtbl g_ClassFactoryVtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    ClassFactory_CreateInstance,
    ClassFactory_LockServer
};

/* ------------------------------------------------------------------------- */
/* DLL Entry Points                                                           */
/* ------------------------------------------------------------------------- */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        break;
    case DLL_PROCESS_DETACH:
        if (g_hMenuBitmap)
        {
            DeleteObject(g_hMenuBitmap);
            g_hMenuBitmap = NULL;
        }
        break;
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    ClassFactory *pFactory;

    *ppv = NULL;

    if (!IsEqualCLSID(rclsid, &CLSID_SSHFSContextMenu))
        return CLASS_E_CLASSNOTAVAILABLE;

    pFactory = CoTaskMemAlloc(sizeof(ClassFactory));
    if (!pFactory)
        return E_OUTOFMEMORY;

    pFactory->lpVtbl = &g_ClassFactoryVtbl;
    pFactory->m_RefCount = 1;

    HRESULT hr = ClassFactory_QueryInterface((IClassFactory *)pFactory, riid, ppv);
    ClassFactory_Release((IClassFactory *)pFactory);

    return hr;
}

STDAPI DllCanUnloadNow(void)
{
    return (g_RefCount == 0) ? S_OK : S_FALSE;
}

/* For regsvr32 registration */
STDAPI DllRegisterServer(void)
{
    WCHAR szModulePath[MAX_PATH];
    WCHAR szClsid[] = L"{7B3F4E8A-1C2D-4E5F-9A8B-0C1D2E3F4A5B}";
    WCHAR szSubKey[256];
    HKEY hKey;
    DWORD dwDisp;

    GetModuleFileNameW(g_hInstance, szModulePath, MAX_PATH);

    /* Register CLSID */
    StringCchPrintfW(szSubKey, 256, L"CLSID\\%s", szClsid);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, szSubKey, 0, NULL, 0, 
        KEY_WRITE, NULL, &hKey, &dwDisp) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, 
            (BYTE *)L"SSHFS-Win Context Menu", 
            sizeof(L"SSHFS-Win Context Menu"));
        RegCloseKey(hKey);
    }

    StringCchPrintfW(szSubKey, 256, L"CLSID\\%s\\InProcServer32", szClsid);
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, szSubKey, 0, NULL, 0,
        KEY_WRITE, NULL, &hKey, &dwDisp) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, 
            (BYTE *)szModulePath, (DWORD)((wcslen(szModulePath) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ,
            (BYTE *)L"Apartment", sizeof(L"Apartment"));
        RegCloseKey(hKey);
    }

    /* Remove old registry keys (upgrade from previous versions) */
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Directory\\Background\\shellex\\ContextMenuHandlers\\SSHFSWin");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Directory\\shellex\\ContextMenuHandlers\\SSHFSWin");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Drive\\shellex\\ContextMenuHandlers\\SSHFSWin");

    /* Register for Directory background - use 000- prefix to appear at top of menu */
    StringCchPrintfW(szSubKey, 256, 
        L"Directory\\Background\\shellex\\ContextMenuHandlers\\000-SSHFSWin");
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, szSubKey, 0, NULL, 0,
        KEY_WRITE, NULL, &hKey, &dwDisp) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, 
            (BYTE *)szClsid, sizeof(szClsid));
        RegCloseKey(hKey);
    }

    /* Register for Directory (folder selection) */
    StringCchPrintfW(szSubKey, 256,
        L"Directory\\shellex\\ContextMenuHandlers\\000-SSHFSWin");
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, szSubKey, 0, NULL, 0,
        KEY_WRITE, NULL, &hKey, &dwDisp) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ,
            (BYTE *)szClsid, sizeof(szClsid));
        RegCloseKey(hKey);
    }

    /* Register for Drive */
    StringCchPrintfW(szSubKey, 256,
        L"Drive\\shellex\\ContextMenuHandlers\\000-SSHFSWin");
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, szSubKey, 0, NULL, 0,
        KEY_WRITE, NULL, &hKey, &dwDisp) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, NULL, 0, REG_SZ,
            (BYTE *)szClsid, sizeof(szClsid));
        RegCloseKey(hKey);
    }

    /* Approve the extension (required for some Windows versions) */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, szClsid, 0, REG_SZ,
            (BYTE *)L"SSHFS-Win Context Menu",
            sizeof(L"SSHFS-Win Context Menu"));
        RegCloseKey(hKey);
    }

    /* Notify shell of changes */
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    return S_OK;
}

STDAPI DllUnregisterServer(void)
{
    WCHAR szClsid[] = L"{7B3F4E8A-1C2D-4E5F-9A8B-0C1D2E3F4A5B}";
    WCHAR szSubKey[256];
    HKEY hKey;

    /* Remove context menu handler registrations (both old and new key names) */
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Directory\\Background\\shellex\\ContextMenuHandlers\\SSHFSWin");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Directory\\shellex\\ContextMenuHandlers\\SSHFSWin");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Drive\\shellex\\ContextMenuHandlers\\SSHFSWin");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Directory\\Background\\shellex\\ContextMenuHandlers\\000-SSHFSWin");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Directory\\shellex\\ContextMenuHandlers\\000-SSHFSWin");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"Drive\\shellex\\ContextMenuHandlers\\000-SSHFSWin");

    /* Remove CLSID registration */
    StringCchPrintfW(szSubKey, 256, L"CLSID\\%s\\InProcServer32", szClsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szSubKey);
    StringCchPrintfW(szSubKey, 256, L"CLSID\\%s", szClsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, szSubKey);

    /* Remove from approved list */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, szClsid);
        RegCloseKey(hKey);
    }

    /* Notify shell of changes */
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    return S_OK;
}

