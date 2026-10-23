/**************************************************************************/
/*  uwp_file_api.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

// UWP: route the file APIs used by FileAccessWindows/DirAccessWindows through their brokered
// "FromApp" variants. Plain Win32 calls only reach the app's own folders; the FromApp ones also
// reach everything the app was granted (broadFileSystemAccess, picked folders, USB drives).
// Include after <windows.h>, and only from those two translation units.

#ifdef UWP_ENABLED

#include <fcntl.h>
#include <fileapifromapp.h>
#include <io.h>
#include <share.h>
#include <stdio.h>
#include <wchar.h>

inline HANDLE godot_uwp_CreateFileW(LPCWSTR p_name, DWORD p_access, DWORD p_share, LPSECURITY_ATTRIBUTES p_security, DWORD p_disposition, DWORD p_flags, HANDLE p_template) {
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	params.dwFileAttributes = p_flags & 0x0000FFFF;
	params.dwFileFlags = p_flags & 0xFFF00000;
	params.dwSecurityQosFlags = p_flags & 0x000F0000;
	params.lpSecurityAttributes = p_security;
	params.hTemplateFile = p_template;
	return CreateFile2FromAppW(p_name, p_access, p_share, p_disposition, &params);
}

inline DWORD godot_uwp_GetFileAttributesW(LPCWSTR p_name) {
	WIN32_FILE_ATTRIBUTE_DATA data = {};
	if (!GetFileAttributesExFromAppW(p_name, GetFileExInfoStandard, &data)) {
		return INVALID_FILE_ATTRIBUTES;
	}
	return data.dwFileAttributes;
}

inline HANDLE godot_uwp_FindFirstFileW(LPCWSTR p_name, LPWIN32_FIND_DATAW r_data) {
	return FindFirstFileExFromAppW(p_name, FindExInfoStandard, r_data, FindExSearchNameMatch, nullptr, 0);
}

// Replacement for _wfsopen(): open the handle through the broker, then wrap it in a FILE*.
inline FILE *godot_uwp_wfsopen(const wchar_t *p_name, const wchar_t *p_mode, int p_share) {
	const bool plus = wcschr(p_mode, L'+') != nullptr;
	const bool write = p_mode[0] == L'w';
	DWORD access = write ? GENERIC_WRITE : GENERIC_READ;
	if (plus) {
		access = GENERIC_READ | GENERIC_WRITE;
	}
	DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
	if (p_share == _SH_DENYWR) {
		share = FILE_SHARE_READ;
	} else if (p_share == _SH_DENYRW) {
		share = 0;
	}
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	params.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	HANDLE handle = CreateFile2FromAppW(p_name, access, share, write ? CREATE_ALWAYS : OPEN_EXISTING, &params);
	if (handle == INVALID_HANDLE_VALUE) {
		errno = GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
		return nullptr;
	}
	const int fd = _open_osfhandle((intptr_t)handle, (plus || write ? _O_RDWR : _O_RDONLY) | _O_BINARY);
	if (fd == -1) {
		CloseHandle(handle);
		return nullptr;
	}
	FILE *file = _wfdopen(fd, p_mode);
	if (!file) {
		_close(fd);
	}
	return file;
}

#define CreateFileW godot_uwp_CreateFileW
#define GetFileAttributesW godot_uwp_GetFileAttributesW
#define FindFirstFileW godot_uwp_FindFirstFileW
#define FindFirstFileExW FindFirstFileExFromAppW
#define CreateDirectoryW CreateDirectoryFromAppW
#define RemoveDirectoryW RemoveDirectoryFromAppW
#define DeleteFileW DeleteFileFromAppW
#define MoveFileW MoveFileFromAppW
#define ReplaceFileW ReplaceFileFromAppW
#define SetFileAttributesW SetFileAttributesFromAppW
#define _wfsopen godot_uwp_wfsopen

#endif // UWP_ENABLED
