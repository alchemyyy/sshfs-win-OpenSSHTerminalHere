/**
 * sshfs-ssh.c
 *
 * Native Windows SSH terminal launcher for SSHFS-Win
 * Uses PuTTY plink.exe for SSH connections with password support
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
#define DEBUG_PASSWORD 0  /* Show the actual password retrieved (SECURITY RISK - disable after debugging) */

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
 * Try to read password from Windows Credential Manager
 */
static BOOL GetStoredPassword(
    LPCWSTR pszUser,
    LPCWSTR pszHost,
    LPCWSTR pszPort,
    LPWSTR pszPassword,
    DWORD cchPassword)
{
    PCREDENTIALW pCred = NULL;
    BOOL bFound = FALSE;
    WCHAR szTargetNames[5][MAX_PATH];
    int numTargets = 0;

    /* Build various possible target names that WinFsp might use */
    if (pszPort && pszPort[0])
    {
        StringCchPrintfW(szTargetNames[numTargets++], MAX_PATH, 
            L"\\\\sshfs\\%s@%s!%s", pszUser, pszHost, pszPort);
        StringCchPrintfW(szTargetNames[numTargets++], MAX_PATH,
            L"\\\\sshfs.r\\%s@%s!%s", pszUser, pszHost, pszPort);
    }
    
    StringCchPrintfW(szTargetNames[numTargets++], MAX_PATH,
        L"\\\\sshfs\\%s@%s", pszUser, pszHost);
    StringCchPrintfW(szTargetNames[numTargets++], MAX_PATH,
        L"\\\\sshfs.r\\%s@%s", pszUser, pszHost);
    StringCchPrintfW(szTargetNames[numTargets++], MAX_PATH,
        L"%s@%s", pszUser, pszHost);

    pszPassword[0] = L'\0';

    for (int i = 0; i < numTargets && !bFound; i++)
    {
#if DEBUG_CRED
        {
            WCHAR szDebug[MAX_PATH * 2];
            StringCchPrintfW(szDebug, MAX_PATH * 2, L"Trying credential target: %s", szTargetNames[i]);
            MessageBoxW(NULL, szDebug, L"Debug - Credential Lookup", MB_OK);
        }
#endif
        
        if (CredReadW(szTargetNames[i], CRED_TYPE_GENERIC, 0, &pCred))
        {
            if (pCred->CredentialBlobSize > 0 && pCred->CredentialBlob != NULL)
            {
                DWORD blobSize = pCred->CredentialBlobSize;
                BYTE *blob = pCred->CredentialBlob;
                
                /* Detect if password is stored as Unicode or ANSI
                 * Unicode (UTF-16 LE) typically has null bytes in odd positions for ASCII
                 * ANSI has no null bytes until the end
                 */
                BOOL isUnicode = FALSE;
                if (blobSize >= 4 && (blobSize % 2) == 0)
                {
                    /* Check if odd bytes are zeros (typical UTF-16 LE for ASCII) */
                    if (blob[1] == 0 && blob[3] == 0)
                        isUnicode = TRUE;
                }
                
                if (isUnicode)
                {
                    /* Copy as Unicode */
                    DWORD charCount = blobSize / sizeof(WCHAR);
                    if (charCount < cchPassword)
                    {
                        memcpy(pszPassword, blob, blobSize);
                        pszPassword[charCount] = L'\0';
                    }
                }
                else
                {
                    /* Convert from ANSI/UTF-8 to Unicode */
                    int result = MultiByteToWideChar(CP_ACP, 0,
                        (LPCCH)blob, blobSize,
                        pszPassword, cchPassword - 1);
                    if (result > 0)
                        pszPassword[result] = L'\0';
                    else
                        pszPassword[0] = L'\0';
                }
                
                bFound = TRUE;
#if DEBUG_CRED
                MessageBoxW(NULL, L"Credential found!", L"Debug - Credential Lookup", MB_OK);
#endif
#if DEBUG_PASSWORD
                {
                    WCHAR szDebug[512];
                    StringCchPrintfW(szDebug, 512, 
                        L"Password retrieved:\nLength: %d chars\nBlob size: %lu bytes\nIs Unicode: %s\nValue: [%s]", 
                        (int)wcslen(pszPassword), blobSize, 
                        isUnicode ? L"YES" : L"NO",
                        pszPassword);
                    MessageBoxW(NULL, szDebug, L"Debug - Password", MB_OK);
                }
#endif
            }
            CredFree(pCred);
        }
    }

#if DEBUG_CRED
    if (!bFound)
        MessageBoxW(NULL, L"No stored credential found", L"Debug - Credential Lookup", MB_OK);
#endif

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
        /* Home mount: use $HOME instead of ~ when there are .. components
         * because $HOME expands inside double quotes, allowing proper path resolution
         */
        if (hasParentDir)
        {
            if (szCombined[0] == L'/')
                StringCchPrintfW(pszFullRemotePath, cchFullRemotePath, L"$HOME%s", szCombined);
            else if (szCombined[0])
                StringCchPrintfW(pszFullRemotePath, cchFullRemotePath, L"$HOME/%s", szCombined);
            else
                StringCchCopyW(pszFullRemotePath, cchFullRemotePath, L"$HOME");
        }
        else
        {
            /* No .. components, use ~ which is simpler */
            if (szCombined[0] == L'/')
                StringCchPrintfW(pszFullRemotePath, cchFullRemotePath, L"~%s", szCombined);
            else if (szCombined[0])
                StringCchPrintfW(pszFullRemotePath, cchFullRemotePath, L"~/%s", szCombined);
            else
                StringCchCopyW(pszFullRemotePath, cchFullRemotePath, L"~");
        }
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
 * Get path to plink.exe (bundled with sshfs-ctx)
 */
static BOOL GetPlinkPath(LPWSTR pszPath, DWORD cchPath)
{
    WCHAR szModulePath[MAX_PATH];
    
    /* Get the directory where sshfs-ssh.exe is located */
    if (GetModuleFileNameW(NULL, szModulePath, MAX_PATH))
    {
        WCHAR *pLastSlash = wcsrchr(szModulePath, L'\\');
        if (pLastSlash)
        {
            *pLastSlash = L'\0';
            StringCchPrintfW(pszPath, cchPath, L"%s\\plink.exe", szModulePath);
            if (GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES)
                return TRUE;
        }
    }
    
    return FALSE;
}

/**
 * Get the user's SSH identity file path (for key-based auth)
 */
static BOOL GetSSHIdentityFile(LPWSTR pszPath, DWORD cchPath)
{
    WCHAR szUserProfile[MAX_PATH];
    
    if (GetEnvironmentVariableW(L"USERPROFILE", szUserProfile, MAX_PATH))
    {
        /* Try PuTTY .ppk format first */
        StringCchPrintfW(pszPath, cchPath, L"%s\\.ssh\\id_rsa.ppk", szUserProfile);
        if (GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
        
        /* Try OpenSSH format (plink can use these too) */
        StringCchPrintfW(pszPath, cchPath, L"%s\\.ssh\\id_rsa", szUserProfile);
        if (GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
        
        StringCchPrintfW(pszPath, cchPath, L"%s\\.ssh\\id_ed25519", szUserProfile);
        if (GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
    }
    
    return FALSE;
}

/**
 * Create batch script to launch plink
 */
static BOOL CreatePlinkLaunchScript(
    LPCWSTR pszPlinkPath,
    LPCWSTR pszUser,
    LPCWSTR pszHost,
    LPCWSTR pszPort,
    LPCWSTR pszRemotePath,
    LPCWSTR pszIdentityFile,
    LPCWSTR pszPassword,
    LPWSTR pszScriptPath,
    DWORD cchScriptPath)
{
    WCHAR szTempDir[MAX_PATH];
    WCHAR szContent[MAX_PATH * 4];
    HANDLE hFile;
    DWORD dwWritten;
    
    if (!GetTempPathW(MAX_PATH, szTempDir))
        return FALSE;
    
    StringCchPrintfW(pszScriptPath, cchScriptPath, L"%ssshfs-plink-launch.bat", szTempDir);
    
    /* Build options */
    WCHAR szPortOpt[64] = {0};
    WCHAR szIdentityOpt[MAX_PATH] = {0};
    WCHAR szPasswordOpt[MAX_PATH] = {0};
    
    if (pszPort && pszPort[0])
        StringCchPrintfW(szPortOpt, 64, L"-P %s ", pszPort);
    
    if (pszIdentityFile && pszIdentityFile[0])
        StringCchPrintfW(szIdentityOpt, MAX_PATH, L"-i \"%s\" ", pszIdentityFile);
    
    if (pszPassword && pszPassword[0])
        StringCchPrintfW(szPasswordOpt, MAX_PATH, L"-pw \"%s\" ", pszPassword);
    
    /* Build plink command
     * plink -no-antispoof -t user@host "cd /path; exec bash --login"
     * -no-antispoof skips the "Access granted. Press Return" prompt
     * Use semicolon instead of && for better compatibility
     * Special case: if path is just "/", use cd / without quotes
     */
    WCHAR szCdCommand[MAX_PATH * 2];
    if (wcscmp(pszRemotePath, L"/") == 0)
    {
        StringCchCopyW(szCdCommand, MAX_PATH * 2, L"cd /");
    }
    else
    {
        /* Use double quotes to allow $HOME expansion */
        StringCchPrintfW(szCdCommand, MAX_PATH * 2, L"cd \\\"%s\\\"", pszRemotePath);
    }
    
    StringCchPrintfW(szContent, MAX_PATH * 4,
        L"@echo off\r\n"
        L"title SSH: %s@%s - %s\r\n"
        L"\"%s\" %s%s%s-no-antispoof -t %s@%s \"%s; exec bash --login\"\r\n"
        L"if errorlevel 1 pause\r\n",
        pszUser, pszHost, pszRemotePath,
        pszPlinkPath, szPasswordOpt, szIdentityOpt, szPortOpt, 
        pszUser, pszHost, szCdCommand);
    
#if DEBUG_SSH_CMD
    {
        WCHAR szDebugPw[512];
        StringCchPrintfW(szDebugPw, 512, L"Password passed to plink: [%s]\nLength: %d", 
            pszPassword ? pszPassword : L"(null)", 
            pszPassword ? (int)wcslen(pszPassword) : 0);
        MessageBoxW(NULL, szDebugPw, L"Debug - Password to Plink", MB_OK);
    }
    MessageBoxW(NULL, szContent, L"Plink Command", MB_OK);
#endif

    hFile = CreateFileW(pszScriptPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    
    char szAnsiContent[MAX_PATH * 4];
    WideCharToMultiByte(CP_ACP, 0, szContent, -1, szAnsiContent, sizeof(szAnsiContent), NULL, NULL);
    
    WriteFile(hFile, szAnsiContent, (DWORD)strlen(szAnsiContent), &dwWritten, NULL);
    CloseHandle(hFile);
    
    return TRUE;
}

/**
 * Launch SSH terminal using plink
 */
static BOOL LaunchSSHTerminal(
    LPCWSTR pszUser,
    LPCWSTR pszHost,
    LPCWSTR pszPort,
    LPCWSTR pszRemotePath,
    MountType mountType)
{
    WCHAR szPlinkPath[MAX_PATH];
    WCHAR szIdentityFile[MAX_PATH] = {0};
    WCHAR szPassword[256] = {0};
    WCHAR szScriptPath[MAX_PATH];
    WCHAR szCmdLine[MAX_PATH * 2];
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    BOOL bResult;
    
    /* Find plink.exe */
    if (!GetPlinkPath(szPlinkPath, MAX_PATH))
    {
        MessageBoxW(NULL, 
            L"Could not find plink.exe.\n\n"
            L"Please ensure plink.exe is in the same directory as sshfs-ssh.exe.",
            L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONERROR);
        return FALSE;
    }

    /* For key-based mounts, try to use identity file */
    if (mountType == MOUNT_TYPE_KEY || mountType == MOUNT_TYPE_KEY_ROOT)
    {
        GetSSHIdentityFile(szIdentityFile, MAX_PATH);
    }
    else
    {
        /* For password-based mounts, try to read stored credential */
        GetStoredPassword(pszUser, pszHost, pszPort, szPassword, 256);
    }

    /* Create launch script */
    if (!CreatePlinkLaunchScript(szPlinkPath, pszUser, pszHost, pszPort, 
        pszRemotePath, szIdentityFile, szPassword, szScriptPath, MAX_PATH))
    {
        MessageBoxW(NULL, L"Failed to create launch script.", 
            L"SSHFS-Win - SSH Terminal", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    
    /* Clear password from memory */
    SecureZeroMemory(szPassword, sizeof(szPassword));

    /* Try Windows Terminal first */
    WCHAR szWTPath[MAX_PATH];
    BOOL useWT = FALSE;
    
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", szWTPath, MAX_PATH))
    {
        StringCchCatW(szWTPath, MAX_PATH, L"\\Microsoft\\WindowsApps\\wt.exe");
        if (GetFileAttributesW(szWTPath) != INVALID_FILE_ATTRIBUTES)
            useWT = TRUE;
    }

    if (useWT)
    {
        StringCchPrintfW(szCmdLine, MAX_PATH * 2,
            L"\"%s\" new-tab --title \"SSH: %s@%s\" -- cmd /c \"%s\"",
            szWTPath, pszUser, pszHost, szScriptPath);
    }
    else
    {
        StringCchPrintfW(szCmdLine, MAX_PATH * 2,
            L"cmd.exe /c \"%s\"", szScriptPath);
    }
    
    si.cb = sizeof(si);
    bResult = CreateProcessW(NULL, szCmdLine, NULL, NULL, FALSE,
        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

    if (bResult)
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return TRUE;
    }

    DWORD dwError = GetLastError();
    WCHAR szError[256];
    StringCchPrintfW(szError, 256, L"Failed to launch SSH terminal.\nError code: %lu", dwError);
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
