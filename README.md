# sshfs-win-OpenSSHTerminalHere
Adds an entry to the folder and background context menus of sshfs-win network drive items to open an ssh terminal session to the corresponding server at the corresponding directory.

## Install

Install sshfs-win and make sure its working, then run install-ctx.bat

**Warning:** this will restart explorer.exe automatically.

## Uninstall

Run uninstall-ctx.bat. sshfs-win will not be affected.

**Warning:** this will restart explorer.exe automatically.

## Building from Source

A C compiler is needed to build this project:

MSVC: Run build-ctx.bat from the VS Developer Console or have cl.exe added to your PATH

GCC (mingw, cygwin, etc.): check the build-ctx.bat file for expected gcc.exe paths

## Artifacts

Installing this program puts the following into the "SSHFS-Win\usr\bin" folder:

* sshfs-ctx.dll
* sshfs-ssh.exe
* sshfs-ssh-launcher.exe

Then simply registers the shell extension.

## About

This was punched out in a day to fit my own use case, so there may be bugs.

This program uses a disgusting hacky launcher to pass passwords into ssh logins so the user doesn't have to do it themselves. The passwords are grabbed from the Windows credential manager, where they are already stored from WinFsp.

I haven't tested it properly yet but this program *should* respect using pubkeys instead of passwords if the sshfs-win drive is mounted that way.

This program aims to handle any pathing that is valid from sshfs-win. For example it will handle the edge case where "../../../../"... does correctly resolve back out to the root directory, as it does in sshfs-win (and Linux).