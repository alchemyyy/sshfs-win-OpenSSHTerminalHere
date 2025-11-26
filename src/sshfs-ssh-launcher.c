/**
 * sshfs-ssh-launcher.c
 *
 * SSH launcher using Windows ConPTY (Pseudo Console) and built-in OpenSSH.
 * ConPTY properly handles Ctrl+C, terminal resize, and all terminal signals.
 *
 * Usage: sshfs-ssh-launcher.exe user@host[:port] [password] ["remote_command"]
 *
 * If password is provided and non-empty, it will be automatically sent when
 * the SSH password prompt appears.
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>

#define BUFFER_SIZE 4096

/* ConPTY function types */
typedef HRESULT (WINAPI *CreatePseudoConsoleFunc)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef HRESULT (WINAPI *ResizePseudoConsoleFunc)(HPCON, COORD);
typedef void (WINAPI *ClosePseudoConsoleFunc)(HPCON);

/* Global state for threads */
static HANDLE g_hPipeOutRead = NULL;
static HANDLE g_hPipeInWrite = NULL;
static HANDLE g_hProcess = NULL;
static volatile BOOL g_bRunning = TRUE;

/**
 * Thread: Read from SSH output and write to console
 */
static DWORD WINAPI OutputThread(LPVOID param)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    char buffer[BUFFER_SIZE];
    DWORD bytesRead, bytesWritten;

    (void)param;

    while (g_bRunning && ReadFile(g_hPipeOutRead, buffer, BUFFER_SIZE, &bytesRead, NULL) && bytesRead > 0)
    {
        WriteFile(hStdout, buffer, bytesRead, &bytesWritten, NULL);
    }
    return 0;
}

/**
 * Thread: Read from console input and write to SSH
 */
static DWORD WINAPI InputThread(LPVOID param)
{
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    char buffer[BUFFER_SIZE];
    DWORD bytesRead, bytesWritten;

    (void)param;

    while (g_bRunning && ReadFile(hStdin, buffer, BUFFER_SIZE, &bytesRead, NULL) && bytesRead > 0)
    {
        WriteFile(g_hPipeInWrite, buffer, bytesRead, &bytesWritten, NULL);
    }
    return 0;
}

/**
 * Get current console size
 */
static COORD GetConsoleSize(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD size = {80, 25};  /* Default fallback */
    
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        size.X = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        size.Y = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    return size;
}

/**
 * Find ssh.exe - try Windows OpenSSH first
 */
static BOOL FindSSH(LPWSTR pszPath, DWORD cchPath)
{
    WCHAR szSystemPath[MAX_PATH];
    
    /* Try Windows OpenSSH location first */
    if (GetSystemDirectoryW(szSystemPath, MAX_PATH))
    {
        StringCchPrintfW(pszPath, cchPath, L"%s\\OpenSSH\\ssh.exe", szSystemPath);
        if (GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
    }
    
    /* Try in System32 directly (Windows 10 1809+) */
    if (GetSystemDirectoryW(szSystemPath, MAX_PATH))
    {
        StringCchPrintfW(pszPath, cchPath, L"%s\\ssh.exe", szSystemPath);
        if (GetFileAttributesW(pszPath) != INVALID_FILE_ATTRIBUTES)
            return TRUE;
    }

    /* Fallback: just use "ssh.exe" and hope it's in PATH */
    StringCchCopyW(pszPath, cchPath, L"ssh.exe");
    return TRUE;
}

int wmain(int argc, wchar_t *argv[])
{
    HMODULE hKernel;
    CreatePseudoConsoleFunc pCreatePseudoConsole;
    ClosePseudoConsoleFunc pClosePseudoConsole;
    HPCON hPC = NULL;
    HANDLE hPipeInRead = NULL, hPipeInWrite = NULL;
    HANDLE hPipeOutRead = NULL, hPipeOutWrite = NULL;
    HANDLE hOutputThread = NULL, hInputThread = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    STARTUPINFOEXW si = {0};
    PROCESS_INFORMATION pi = {0};
    SIZE_T attrListSize = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attrList = NULL;
    COORD consoleSize;
    HRESULT hr;
    DWORD dwExitCode = 0;
    DWORD dwOrigConsoleMode = 0;
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    
    WCHAR szSSHPath[MAX_PATH];
    WCHAR szCmdLine[4096];
    WCHAR szTarget[512] = {0};
    WCHAR szPassword[256] = {0};
    WCHAR szRemoteCmd[1024] = {0};
    WCHAR szPort[16] = {0};
    WCHAR szTitle[512];
    
    char szPasswordA[256] = {0};
    char buffer[BUFFER_SIZE];
    DWORD bytesRead, bytesWritten;
    BOOL foundPrompt = FALSE;

    if (argc < 2)
    {
        fwprintf(stderr, L"Usage: %s user@host[:port] [password] [\"remote_command\"]\n", argv[0]);
        return 1;
    }

    /* Parse arguments */
    StringCchCopyW(szTarget, 512, argv[1]);
    
    if (argc >= 3 && argv[2][0] != L'\0')
        StringCchCopyW(szPassword, 256, argv[2]);
    
    if (argc >= 4)
        StringCchCopyW(szRemoteCmd, 1024, argv[3]);

    /* Check for port in target (user@host:port format) */
    WCHAR *pColon = wcsrchr(szTarget, L':');
    WCHAR *pAt = wcschr(szTarget, L'@');
    if (pColon && pColon > pAt)
    {
        *pColon = L'\0';
        StringCchCopyW(szPort, 16, pColon + 1);
    }

    /* Set console title */
    StringCchPrintfW(szTitle, 512, L"SSH: %s", szTarget);
    SetConsoleTitleW(szTitle);

    /* Find SSH executable */
    if (!FindSSH(szSSHPath, MAX_PATH))
    {
        fwprintf(stderr, L"Could not find ssh.exe\n");
        return 1;
    }

    /* Build SSH command line */
    if (szPort[0])
    {
        if (szRemoteCmd[0])
            StringCchPrintfW(szCmdLine, 4096, L"\"%s\" -p %s -t %s \"%s\"", 
                szSSHPath, szPort, szTarget, szRemoteCmd);
        else
            StringCchPrintfW(szCmdLine, 4096, L"\"%s\" -p %s %s", 
                szSSHPath, szPort, szTarget);
    }
    else
    {
        if (szRemoteCmd[0])
            StringCchPrintfW(szCmdLine, 4096, L"\"%s\" -t %s \"%s\"", 
                szSSHPath, szTarget, szRemoteCmd);
        else
            StringCchPrintfW(szCmdLine, 4096, L"\"%s\" %s", 
                szSSHPath, szTarget);
    }

    /* Load ConPTY functions */
    hKernel = GetModuleHandleW(L"kernel32.dll");
    pCreatePseudoConsole = (CreatePseudoConsoleFunc)GetProcAddress(hKernel, "CreatePseudoConsole");
    pClosePseudoConsole = (ClosePseudoConsoleFunc)GetProcAddress(hKernel, "ClosePseudoConsole");

    if (!pCreatePseudoConsole || !pClosePseudoConsole)
    {
        fwprintf(stderr, L"ConPTY not available. Requires Windows 10 1809+\n");
        return 1;
    }

    /* Create pipes for I/O */
    if (!CreatePipe(&hPipeInRead, &hPipeInWrite, &sa, 0) ||
        !CreatePipe(&hPipeOutRead, &hPipeOutWrite, &sa, 0))
    {
        fwprintf(stderr, L"CreatePipe failed: %lu\n", GetLastError());
        return 1;
    }

    SetHandleInformation(hPipeInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hPipeOutRead, HANDLE_FLAG_INHERIT, 0);

    /* Get console size */
    consoleSize = GetConsoleSize();

    /* Create pseudo console */
    hr = pCreatePseudoConsole(consoleSize, hPipeInRead, hPipeOutWrite, 0, &hPC);
    if (FAILED(hr))
    {
        fwprintf(stderr, L"CreatePseudoConsole failed: 0x%08lx\n", hr);
        return 1;
    }

    /* Set up process to use pseudo console */
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrListSize);
    attrList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attrListSize);
    if (!attrList)
    {
        fwprintf(stderr, L"Memory allocation failed\n");
        pClosePseudoConsole(hPC);
        return 1;
    }
    
    InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize);
    UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              hPC, sizeof(HPCON), NULL, NULL);

    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.lpAttributeList = attrList;

    /* Create SSH process */
    if (!CreateProcessW(NULL, szCmdLine, NULL, NULL, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, 
                        (LPSTARTUPINFOW)&si, &pi))
    {
        fwprintf(stderr, L"CreateProcess failed: %lu\nCommand: %s\n", GetLastError(), szCmdLine);
        pClosePseudoConsole(hPC);
        free(attrList);
        return 1;
    }

    g_hProcess = pi.hProcess;

    /* Close pipe ends that SSH process uses */
    CloseHandle(hPipeInRead);
    hPipeInRead = NULL;
    CloseHandle(hPipeOutWrite);
    hPipeOutWrite = NULL;

    /* Set global handles for threads */
    g_hPipeOutRead = hPipeOutRead;
    g_hPipeInWrite = hPipeInWrite;

    /* Convert password to ANSI for sending */
    if (szPassword[0])
    {
        WideCharToMultiByte(CP_UTF8, 0, szPassword, -1, szPasswordA, 256, NULL, NULL);
    }

    /* If password provided, wait for password prompt */
    if (szPasswordA[0])
    {
        HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        
        while (ReadFile(hPipeOutRead, buffer, BUFFER_SIZE - 1, &bytesRead, NULL) && bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            WriteFile(hStdout, buffer, bytesRead, &bytesWritten, NULL);

            /* Check for password prompt */
            if (!foundPrompt && (strstr(buffer, "password:") || strstr(buffer, "Password:")))
            {
                foundPrompt = TRUE;
                /* Send password */
                char passLine[512];
                StringCchPrintfA(passLine, 512, "%s\r", szPasswordA);
                WriteFile(hPipeInWrite, passLine, (DWORD)strlen(passLine), &bytesWritten, NULL);
                FlushFileBuffers(hPipeInWrite);
                
                /* Clear password from memory */
                SecureZeroMemory(szPasswordA, sizeof(szPasswordA));
                SecureZeroMemory(szPassword, sizeof(szPassword));
                break;
            }
            
            /* Check if SSH exited (e.g., key-based auth succeeded) */
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
                break;
        }
    }

    /* Set console to raw mode for proper terminal handling */
    GetConsoleMode(hStdin, &dwOrigConsoleMode);
    SetConsoleMode(hStdin, ENABLE_VIRTUAL_TERMINAL_INPUT);

    /* Start I/O threads */
    hOutputThread = CreateThread(NULL, 0, OutputThread, NULL, 0, NULL);
    hInputThread = CreateThread(NULL, 0, InputThread, NULL, 0, NULL);

    /* Wait for SSH process to exit */
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &dwExitCode);

    /* Signal threads to stop */
    g_bRunning = FALSE;
    
    /* Cancel blocking reads */
    CancelIoEx(hPipeOutRead, NULL);
    CancelIoEx(hStdin, NULL);

    /* Wait for threads */
    WaitForSingleObject(hOutputThread, 1000);
    WaitForSingleObject(hInputThread, 1000);

    /* Restore console mode */
    SetConsoleMode(hStdin, dwOrigConsoleMode);

    /* Cleanup */
    CloseHandle(hOutputThread);
    CloseHandle(hInputThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hPipeInWrite);
    CloseHandle(hPipeOutRead);
    DeleteProcThreadAttributeList(attrList);
    free(attrList);
    pClosePseudoConsole(hPC);

    /* If SSH exited with error, pause */
    if (dwExitCode != 0)
    {
        fwprintf(stderr, L"\nSSH exited with code %lu. Press any key to close...", dwExitCode);
        _getch();
    }

    return (int)dwExitCode;
}

