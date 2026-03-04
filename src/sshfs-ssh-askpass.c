/**
 * sshfs-ssh-askpass.c
 *
 * Minimal SSH_ASKPASS helper for SSHFS-Win.
 * Reads the password from SSHFS_PASSWORD environment variable and prints
 * it to stdout. Used by ssh.exe when SSH_ASKPASS_REQUIRE=force is set.
 *
 * The password is passed via env var by sshfs-ssh.exe, which reads it
 * from Windows Credential Manager.
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <stdio.h>

int wmain(int argc, wchar_t *argv[])
{
    WCHAR szPassword[256];
    char szPasswordA[256];

    (void)argc;
    (void)argv;

    if (!GetEnvironmentVariableW(L"SSHFS_PASSWORD", szPassword, 256) || !szPassword[0])
        return 1;

    WideCharToMultiByte(CP_UTF8, 0, szPassword, -1, szPasswordA, 256, NULL, NULL);
    printf("%s\n", szPasswordA);

    SecureZeroMemory(szPassword, sizeof(szPassword));
    SecureZeroMemory(szPasswordA, sizeof(szPasswordA));

    return 0;
}
