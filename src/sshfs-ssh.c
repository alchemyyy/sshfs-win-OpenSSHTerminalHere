/**
 * sshfs-ssh.c
 *
 * Native Windows SSH terminal launcher for SSHFS-Win
 * Uses Windows built-in OpenSSH via ConPTY for proper terminal emulation
 * Supports both password and key-based authentication
 *
 * This is a native Windows program - no Cygwin dependencies
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <wincred.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <stdio.h>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "credui.lib")
#pragma comment(lib, "mpr.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

/* Debug flags - set to 1 to enable debug message boxes */
#define DEBUG_PATHS 0      /* Show UNC path parsing debug info */
#define DEBUG_SSH_CMD 0    /* Show the final SSH command before execution */
#define DEBUG_CRED 0       /* Show credential lookup debug info */
#define DEBUG_PASSWORD 0   /* Show the actual password retrieved (SECURITY RISK - disable after debugging) */

/**
 * Mount type enumeration
 */
typedef enum {
    MOUNT_TYPE_PASSWORD,        /* \\sshfs\... - password auth */
    MOUNT_TYPE_PASSWORD_ROOT,   /* \\sshfs.r\... - password auth, root path */
    MOUNT_TYPE_KEY,             /* \\sshfs.k\... - key auth */
    MOUNT_TYPE_KEY_ROOT         /* \\sshfs.kr\... - key auth, root path */
} MountType;

/**
 * Extract password from credential blob, handling Unicode vs ANSI encoding
 */
static BOOL ExtractPasswordFromCredential(PCREDENTIALW pCred, LPWSTR pszPassword, DWORD cchPassword)
{
    if (!pCred || pCred->CredentialBlobSize == 0 || !pCred->CredentialBlob)
        return FALSE;

    DWORD blobSize = pCred->CredentialBlobSize;
    BYTE *blob = pCred->CredentialBlob;

    /* Detect if password is stored as Unicode or ANSI */
    BOOL isUnicode = FALSE;
    if (blobSize >= 4 && (blobSize % 2) == 0)
    {
        if (blob[1] == 0 && blob[3] == 0)
            isUnicode = TRUE;
    }

    if (isUnicode)
    {
        DWORD charCount = blobSize / sizeof(WCHAR);
        if (charCount < cchPassword)
        {
            memcpy(pszPassword, blob, blobSize);
            pszPassword[charCount] = L'\0';
            return TRUE;
        }
    }
    else
    {
        int result = MultiByteToWideChar(CP_ACP, 0,
            (LPCCH)blob, blobSize,
            pszPassword, cchPassword - 1);
        if (result > 0)
        {
            pszPassword[result] = L'\0';
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Try to read password from Windows Credential Manager
 * First tries exact target names, then enumerates all credentials to find a match
 */
static BOOL GetStoredPassword(
    LPCWSTR pszUser,
    LPCWSTR pszHost,
    LPCWSTR pszPort,
    LPWSTR pszPassword,
    DWORD cchPassword)
{
    PCREDENTIALW pCred = NULL;
    PCREDENTIALW *pCredentials = NULL;
    DWORD dwCount = 0;
    BOOL bFound = FALSE;

    pszPassword[0] = L'\0';

    /* Build search pattern for user@host */
    WCHAR szSearchPattern[512];
    if (pszPort && pszPort[0])
        StringCchPrintfW(szSearchPattern, 512, L"%s@%s", pszUser, pszHost);
    else
        StringCchPrintfW(szSearchPattern, 512, L"%s@%s", pszUser, pszHost);

#if DEBUG_CRED
    {
        WCHAR szDebug[512];
        StringCchPrintfW(szDebug, 512, L"Looking for credentials matching: %s", szSearchPattern);
        MessageBoxW(NULL, szDebug, L"Debug - Credential Search", MB_OK);
    }
#endif

    /* Enumerate ALL credentials and find one containing our user@host */
    if (CredEnumerateW(NULL, 0, &dwCount, &pCredentials))
    {
#if DEBUG_CRED
        {
            WCHAR szDebug[256];
            StringCchPrintfW(szDebug, 256, L"Found %lu credentials in Credential Manager", dwCount);
            MessageBoxW(NULL, szDebug, L"Debug - Credential Enumerate", MB_OK);
        }
#endif

        for (DWORD i = 0; i < dwCount && !bFound; i++)
        {
            PCREDENTIALW pc = pCredentials[i];
            if (pc->TargetName)
            {
                /* Check if target contains our user@host pattern (case-insensitive) */
                WCHAR szTargetLower[MAX_PATH];
                WCHAR szPatternLower[512];
                StringCchCopyW(szTargetLower, MAX_PATH, pc->TargetName);
                StringCchCopyW(szPatternLower, 512, szSearchPattern);
                _wcslwr_s(szTargetLower, MAX_PATH);
                _wcslwr_s(szPatternLower, 512);

                if (wcsstr(szTargetLower, szPatternLower))
                {
#if DEBUG_CRED
                    {
                        WCHAR szDebug[MAX_PATH * 2];
                        StringCchPrintfW(szDebug, MAX_PATH * 2,
                            L"MATCH FOUND!\nTarget: %s\nType: %lu",
                            pc->TargetName, pc->Type);
                        MessageBoxW(NULL, szDebug, L"Debug - Credential Match", MB_OK);
                    }
#endif
                    if (ExtractPasswordFromCredential(pc, pszPassword, cchPassword))
                    {
                        bFound = TRUE;
#if DEBUG_PASSWORD
                        {
                            WCHAR szDebug[512];
                            StringCchPrintfW(szDebug, 512,
                                L"Password extracted!\nLength: %d chars\nValue: [%s]",
                                (int)wcslen(pszPassword), pszPassword);
                            MessageBoxW(NULL, szDebug, L"Debug - Password", MB_OK);
                        }
#endif
                    }
                }
            }
        }

#if DEBUG_CRED
        if (!bFound)
        {
            /* Show first few credentials for debugging */
            WCHAR szDebug[2048] = L"No match found.\n\nFirst 10 credentials:\n";
            for (DWORD i = 0; i < dwCount && i < 10; i++)
            {
                WCHAR szLine[256];
                StringCchPrintfW(szLine, 256, L"%lu: %s\n", i, pCredentials[i]->TargetName);
                StringCchCatW(szDebug, 2048, szLine);
            }
            MessageBoxW(NULL, szDebug, L"Debug - No Match", MB_OK);
        }
#endif

        CredFree(pCredentials);
    }
    else
    {
#if DEBUG_CRED
        {
            WCHAR szDebug[256];
            StringCchPrintfW(szDebug, 256, L"CredEnumerateW failed: %lu", GetLastError());
            MessageBoxW(NULL, szDebug, L"Debug - Credential Error", MB_OK);
        }
#endif
    }

    return bFound;
}

/**
 * Parse SSHFS UNC path to extract connection info
 */
static BOOL ParseSSHFSUNCPath(
    LPCWSTR pszUNC,
    LPWSTR pszUser, DWORD cchUser,
    LPWSTR pszHost, DWORD cchHost,
    LPWSTR pszPort, DWORD cchPort,
    LPWSTR pszBasePath, DWORD cchBasePath,
    MountType *pMountType)
{
    WCHAR szWork[MAX_PATH * 2];
    WCHAR *p, *pInstance, *pPath;
    WCHAR *pAt, *pBang, *pEquals;

    if (!pszUNC || wcslen(pszUNC) < 10)
        return FALSE;

    StringCchCopyW(szWork, MAX_PATH * 2, pszUNC);

    p = szWork;
    while (*p == L'\\') p++;

    if (_wcsnicmp(p, L"sshfs.kr\\", 9) == 0 || _wcsnicmp(p, L"sshfs.kr/", 9) == 0)
    {
        *pMountType = MOUNT_TYPE_KEY_ROOT;
        p += 9;
    }
    else if (_wcsnicmp(p, L"sshfs.k\\", 8) == 0 || _wcsnicmp(p, L"sshfs.k/", 8) == 0)
    {
        *pMountType = MOUNT_TYPE_KEY;
        p += 8;
    }
    else if (_wcsnicmp(p, L"sshfs.r\\", 8) == 0 || _wcsnicmp(p, L"sshfs.r/", 8) == 0)
    {
        *pMountType = MOUNT_TYPE_PASSWORD_ROOT;
        p += 8;
    }
    else if (_wcsnicmp(p, L"sshfs\\", 6) == 0 || _wcsnicmp(p, L"sshfs/", 6) == 0)
    {
        *pMountType = MOUNT_TYPE_PASSWORD;
        p += 6;
    }
    else
    {
        return FALSE;
    }

    pInstance = p;
    pPath = NULL;

    while (*p && *p != L'\\' && *p != L'/') p++;

    if (*p)
    {
        *p = L'\0';
        pPath = p + 1;
    }

    pEquals = wcschr(pInstance, L'=');
    if (pEquals)
        pInstance = pEquals + 1;

    pAt = wcschr(pInstance, L'@');
    if (!pAt)
        return FALSE;

    *pAt = L'\0';
    StringCchCopyW(pszUser, cchUser, pInstance);

    pBang = wcschr(pAt + 1, L'!');
    if (pBang)
    {
        *pBang = L'\0';
        StringCchCopyW(pszHost, cchHost, pAt + 1);
        StringCchCopyW(pszPort, cchPort, pBang + 1);
    }
    else
    {
        StringCchCopyW(pszHost, cchHost, pAt + 1);
        pszPort[0] = L'\0';
    }

    if (pPath && *pPath)
    {
        StringCchCopyW(pszBasePath, cchBasePath, pPath);
        for (WCHAR *q = pszBasePath; *q; q++)
            if (*q == L'\\') *q = L'/';
    }
    else
    {
        pszBasePath[0] = L'\0';
    }

    return TRUE;
}

/**
 * Get the UNC path for a drive letter
 */
static BOOL GetDriveUNCPath(WCHAR driveLetter, LPWSTR pszUNC, DWORD cchUNC)
{
    WCHAR szDrive[4] = {driveLetter, L':', L'\0'};
    DWORD dwLen = cchUNC;
    return WNetGetConnectionW(szDrive, pszUNC, &dwLen) == NO_ERROR;
}

/**
 * Build the full remote path
 */
static void BuildFullRemotePath(
    LPCWSTR pszLocalPath,
    LPCWSTR pszUNCPath,
    LPCWSTR pszUNCBasePath,
    MountType mountType,
    LPWSTR pszFullRemotePath,
    DWORD cchFullRemotePath)
{
    WCHAR szRemotePath[MAX_PATH * 2] = {0};
    WCHAR szLocalSubPath[MAX_PATH] = {0};
    BOOL bRootMount = (mountType == MOUNT_TYPE_PASSWORD_ROOT || mountType == MOUNT_TYPE_KEY_ROOT);

    if (pszUNCPath && pszUNCPath[0])
    {
        WCHAR *p = (WCHAR *)pszUNCPath;
        while (*p == L'\\') p++;
        while (*p && *p != L'\\' && *p != L'/') p++;
        if (*p) p++;
        while (*p && *p != L'\\' && *p != L'/') p++;
        if (*p) StringCchCopyW(szRemotePath, MAX_PATH * 2, p);
    }

    if (pszLocalPath[1] == L':' && pszLocalPath[2])
        StringCchCopyW(szLocalSubPath, MAX_PATH, pszLocalPath + 2);

    for (WCHAR *q = szRemotePath; *q; q++)
        if (*q == L'\\') *q = L'/';
    for (WCHAR *q = szLocalSubPath; *q; q++)
        if (*q == L'\\') *q = L'/';

    WCHAR szCombined[MAX_PATH * 2];
    StringCchPrintfW(szCombined, MAX_PATH * 2, L"%s%s", szRemotePath, szLocalSubPath);

    /* Check if path contains .. components */
    BOOL hasParentDir = (wcsstr(szCombined, L"../") != NULL || wcsstr(szCombined, L"..\\") != NULL);

    if (bRootMount)
    {
        /* Root mount: path is absolute from / */
        if (szCombined[0] == L'/')
            StringCchCopyW(pszFullRemotePath, cchFullRemotePath, szCombined);
        else
            StringCchPrintfW(pszFullRemotePath, cchFullRemotePath, L"/%s", szCombined);
    }
    else
    {
        /* Home mount: path is relative to home directory (~) */
        if (szCombined[0] == L'/')
            StringCchPrintfW(pszFullRemotePath, cchFullRemotePath, L"~%s", szCombined);
        else if (szCombined[0])
            StringCchPrintfW(pszFullRemotePath, cchFullRemotePath, L"~/%s", szCombined);
        else
            StringCchCopyW(pszFullRemotePath, cchFullRemotePath, L"~");
    }

    /* Clean up double slashes */
    WCHAR szClean[MAX_PATH * 2];
    WCHAR *src = pszFullRemotePath;
    WCHAR *dst = szClean;
    BOOL lastWasSlash = FALSE;
    
    while (*src)
    {
        if (*src == L'/')
        {
            if (!lastWasSlash) { *dst++ = *src; lastWasSlash = TRUE; }
        }
        else
        {
            *dst++ = *src;
            lastWasSlash = FALSE;
        }
        src++;
    }
    *dst = L'\0';

    StringCchCopyW(pszFullRemotePath, cchFullRemotePath, szClean);
}

/**
 * Get path to sshfs-ssh-launcher.exe (bundled with sshfs-ctx)
 */
static BOOL GetLauncherPath(LPWSTR pszPath, DWORD cchPath)
{
    WCHAR szModulePath[MAX_PATH];
    
    /* Get the directory where sshfs-ssh.exe is located */
    if (GetModuleFileNameW(NULL, szModulePath, MAX_PATH))
    {
        WCHAR *pLastSlash = wcsrchr(szModulePath, L'\\');
        if (pLastSlash)
        {
            *pLastSlash = L'\0';
            StringCchPrintfW(pszPath, cchPath, L"%s\\sshfs-ssh-launcher.exe", szModulePath);
            if (GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES)
                return TRUE;
        }
    }
    
    return FALSE;
}

/**
 * Launch SSH terminal using Windows OpenSSH via ConPTY launcher
 *
 * Uses sshfs-ssh-launcher.exe which:
 * - Creates a ConPTY (pseudo-console) for proper terminal emulation
 * - Launches ssh.exe attached to the ConPTY
 * - Handles password prompt if credentials are stored
 * - Properly handles Ctrl+C and all terminal signals
 */
static BOOL LaunchSSHTerminal(
    LPCWSTR pszUser,
    LPCWSTR pszHost,
    LPCWSTR pszPort,
    LPCWSTR pszRemotePath,
    MountType mountType)
{
    WCHAR szLauncherPath[MAX_PATH];
    WCHAR szPassword[256] = {0};
    WCHAR szCmdLine[MAX_PATH * 8];
    WCHAR szTarget[512];
    WCHAR szRemoteCmd[MAX_PATH * 2];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    BOOL bResult;
    
    /* Find the launcher */
    if (!GetLauncherPath(szLauncherPath, MAX_PATH))
    {
        MessageBoxW(NULL, 
            L"Could not find sshfs-ssh-launcher.exe.\n\n"
            L"Please ensure sshfs-ssh-launcher.exe is in the same directory as sshfs-ssh.exe.",
            L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    /* For password-based mounts, try to read stored credential */
    if (mountType == MOUNT_TYPE_PASSWORD || mountType == MOUNT_TYPE_PASSWORD_ROOT)
    {
        GetStoredPassword(pszUser, pszHost, pszPort, szPassword, 256);
    }
    /* For key-based mounts, OpenSSH will use ~/.ssh/ keys automatically */

    /* Build target: user@host or user@host:port */
    if (pszPort && pszPort[0])
        StringCchPrintfW(szTarget, 512, L"%s@%s:%s", pszUser, pszHost, pszPort);
    else
        StringCchPrintfW(szTarget, 512, L"%s@%s", pszUser, pszHost);

    /* Build remote command: cd to path and start login shell */
    {
        /* First, sanitize the path - remove any quotes that might break things */
        WCHAR szCleanPath[MAX_PATH * 2];
        WCHAR *src = (WCHAR*)pszRemotePath;
        WCHAR *dst = szCleanPath;
        while (*src && dst < szCleanPath + MAX_PATH * 2 - 1)
        {
            if (*src != L'"' && *src != L'\'')
                *dst++ = *src;
            src++;
        }
        *dst = L'\0';

        if (szCleanPath[0] == L'\0' || wcscmp(szCleanPath, L"~") == 0)
        {
            StringCchCopyW(szRemoteCmd, MAX_PATH * 2, L"cd ~; exec $SHELL");
        }
        else if (szCleanPath[0] == L'/')
        {
            StringCchPrintfW(szRemoteCmd, MAX_PATH * 2, L"cd '%s'; exec $SHELL", szCleanPath);
        }
        else if (szCleanPath[0] == L'~')
        {
            /* Path like ~/something or ~/../../.. */
            StringCchPrintfW(szRemoteCmd, MAX_PATH * 2, L"cd %s; exec $SHELL", szCleanPath);
        }
        else
        {
            StringCchPrintfW(szRemoteCmd, MAX_PATH * 2, L"cd '%s'; exec $SHELL", szCleanPath);
        }
    }

    /* Create pipe for secure password passing (not visible in process list) */
    HANDLE hPipeRead = NULL, hPipeWrite = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};  /* Inheritable */

    if (szPassword[0])
    {
        if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0))
        {
            MessageBoxW(NULL, L"Failed to create password pipe", L"SSHFS-Win", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        /* Make write handle non-inheritable (only read handle goes to child) */
        SetHandleInformation(hPipeWrite, HANDLE_FLAG_INHERIT, 0);
    }

    /* Build command line for launcher:
     * sshfs-ssh-launcher.exe user@host[:port] pipe_handle ["remote_command"]
     * Password is passed via inherited pipe, not command line (security)
     */
    if (szPassword[0])
    {
        if (szRemoteCmd[0])
            StringCchPrintfW(szCmdLine, MAX_PATH * 8,
                L"\"%s\" \"%s\" %llu \"%s\"",
                szLauncherPath, szTarget, (ULONGLONG)(ULONG_PTR)hPipeRead, szRemoteCmd);
        else
            StringCchPrintfW(szCmdLine, MAX_PATH * 8,
                L"\"%s\" \"%s\" %llu",
                szLauncherPath, szTarget, (ULONGLONG)(ULONG_PTR)hPipeRead);
    }
    else
    {
        if (szRemoteCmd[0])
            StringCchPrintfW(szCmdLine, MAX_PATH * 8,
                L"\"%s\" \"%s\" 0 \"%s\"",
                szLauncherPath, szTarget, szRemoteCmd);
        else
            StringCchPrintfW(szCmdLine, MAX_PATH * 8,
                L"\"%s\" \"%s\" 0",
                szLauncherPath, szTarget);
    }

#if DEBUG_SSH_CMD
    {
        WCHAR szTempPath[MAX_PATH], szTempFile[MAX_PATH];
        GetTempPathW(MAX_PATH, szTempPath);
        StringCchPrintfW(szTempFile, MAX_PATH, L"%ssshfs-debug.txt", szTempPath);
        HANDLE hFile = CreateFileW(szTempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            char szUtf8[MAX_PATH * 16];
            WideCharToMultiByte(CP_UTF8, 0, szCmdLine, -1, szUtf8, sizeof(szUtf8), NULL, NULL);
            WriteFile(hFile, szUtf8, (DWORD)strlen(szUtf8), &written, NULL);
            CloseHandle(hFile);
            ShellExecuteW(NULL, L"open", L"notepad.exe", szTempFile, NULL, SW_SHOW);
            Sleep(500);  /* Give notepad time to open */
        }
    }
#endif

    /* Launch the ConPTY-based SSH launcher in a new console */
    si.cb = sizeof(si);
    bResult = CreateProcessW(NULL, szCmdLine, NULL, NULL, TRUE,  /* TRUE = inherit handles */
        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

    if (bResult)
    {
        /* Write password to pipe, then close handles */
        if (szPassword[0] && hPipeWrite)
        {
            char szPasswordA[256];
            DWORD written;
            WideCharToMultiByte(CP_UTF8, 0, szPassword, -1, szPasswordA, 256, NULL, NULL);
            WriteFile(hPipeWrite, szPasswordA, (DWORD)strlen(szPasswordA) + 1, &written, NULL);
            SecureZeroMemory(szPasswordA, sizeof(szPasswordA));
            CloseHandle(hPipeWrite);
        }
        if (hPipeRead) CloseHandle(hPipeRead);  /* Close our copy, child has its own */

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        /* Clear password from memory */
        SecureZeroMemory(szPassword, sizeof(szPassword));
        return TRUE;
    }

    /* Cleanup on failure */
    if (hPipeRead) CloseHandle(hPipeRead);
    if (hPipeWrite) CloseHandle(hPipeWrite);
    SecureZeroMemory(szPassword, sizeof(szPassword));

    DWORD dwError = GetLastError();
    WCHAR szError[512];
    StringCchPrintfW(szError, 512, 
        L"Failed to launch SSH terminal.\nError code: %lu\n\nCommand: %s", 
        dwError, szCmdLine);
    MessageBoxW(NULL, szError, L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONERROR);

    return FALSE;
}

/**
 * Main entry point
 */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine, int nCmdShow)
{
    int argc;
    LPWSTR *argv;
    WCHAR szPath[MAX_PATH];
    WCHAR szUNCPath[MAX_PATH * 2];
    WCHAR szUser[128], szHost[256], szPort[16], szBasePath[MAX_PATH];
    WCHAR szFullRemotePath[MAX_PATH * 2];
    MountType mountType = MOUNT_TYPE_PASSWORD;

    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2)
    {
        MessageBoxW(NULL,
            L"Usage: sshfs-ssh.exe <path>\n\n"
            L"Opens an SSH terminal to the location on an SSHFS mounted drive.",
            L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONINFORMATION);
        if (argv) LocalFree(argv);
        return 1;
    }

    StringCchCopyW(szPath, MAX_PATH, argv[1]);
    LocalFree(argv);

    /* Remove trailing backslash if present */
    size_t len = wcslen(szPath);
    if (len > 3 && (szPath[len - 1] == L'\\' || szPath[len - 1] == L'/'))
        szPath[len - 1] = L'\0';

    /* Get UNC path */
    if (szPath[0] == L'\\' && szPath[1] == L'\\')
    {
        StringCchCopyW(szUNCPath, MAX_PATH * 2, szPath);
    }
    else if (szPath[1] == L':')
    {
        if (!GetDriveUNCPath(szPath[0], szUNCPath, MAX_PATH * 2))
        {
            MessageBoxW(NULL,
                L"This drive is not a network drive.\n\n"
                L"The \"Open SSH Terminal Here\" feature only works on SSHFS mounted drives.",
                L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONWARNING);
            return 1;
        }
    }
    else
    {
        MessageBoxW(NULL,
            L"Invalid path format.\n\n"
            L"Please use a drive letter path (X:\\folder) or UNC path.",
            L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONERROR);
        return 1;
    }

#if DEBUG_PATHS
    {
        WCHAR szDebug[MAX_PATH * 2];
        StringCchPrintfW(szDebug, MAX_PATH * 2, L"Local path: %s\nUNC path: %s", szPath, szUNCPath);
        MessageBoxW(NULL, szDebug, L"Debug - Paths", MB_OK);
    }
#endif

    /* Check if it's an SSHFS path */
    if (_wcsnicmp(szUNCPath, L"\\\\sshfs", 7) != 0)
    {
        MessageBoxW(NULL,
            L"This is not an SSHFS mounted drive.\n\n"
            L"The \"Open SSH Terminal Here\" feature only works on SSHFS mounted drives.",
            L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONWARNING);
        return 1;
    }

    /* Parse the UNC path */
    if (!ParseSSHFSUNCPath(szUNCPath, 
        szUser, 128, szHost, 256, szPort, 16, szBasePath, MAX_PATH, &mountType))
    {
        MessageBoxW(NULL,
            L"Could not parse SSHFS connection information from the path.\n\n"
            L"The path format may be unsupported.",
            L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Build full remote path */
    BuildFullRemotePath(szPath, szUNCPath, szBasePath, mountType, szFullRemotePath, MAX_PATH * 2);

#if DEBUG_PATHS
    {
        WCHAR szDebug[MAX_PATH * 2];
        StringCchPrintfW(szDebug, MAX_PATH * 2, 
            L"User: %s\nHost: %s\nPort: %s\nBase: %s\nFull remote: %s\nType: %d",
            szUser, szHost, szPort, szBasePath, szFullRemotePath, mountType);
        MessageBoxW(NULL, szDebug, L"Debug - Parsed", MB_OK);
    }
#endif

    /* Launch SSH terminal */
    if (!LaunchSSHTerminal(szUser, szHost, szPort, szFullRemotePath, mountType))
        return 1;

    return 0;
}
