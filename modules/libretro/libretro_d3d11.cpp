/**************************************************************************/
/*  libretro_d3d11.cpp                                                    */
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


#include "libretro_d3d11.h"

#ifdef WINDOWS_ENABLED

#include "core/string/print_string.h"

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include "libretro.h"

namespace {
// libretro_d3d11.h, version 1 (the fields a version 1 core reads).
struct retro_hw_render_interface_d3d11 {
	enum retro_hw_render_interface_type interface_type;
	unsigned interface_version;
	void *handle;
	ID3D11Device *device;
	ID3D11DeviceContext *context;
	D3D_FEATURE_LEVEL featureLevel;
	pD3DCompile D3DCompile;
};
} // namespace

// Frames come back through a ring of staging textures: this frame is copied into one, and the one
// copied STAGING_RING - 1 frames ago is read - the GPU finished it long ago. Mapping the texture just
// copied made the CPU wait for the GPU to draw the whole frame, every frame (half the PS2's speed).
static constexpr int STAGING_RING = 3;

struct LibretroD3D11::Data {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	ID3D11Texture2D *staging[STAGING_RING] = {};
	unsigned staging_width[STAGING_RING] = {};
	unsigned staging_height[STAGING_RING] = {};
	bool staging_filled[STAGING_RING] = {};
	D3D11_TEXTURE2D_DESC staging_desc = {};
	int next = 0;
	retro_hw_render_interface_d3d11 iface = {};
	bool format_reported = false;

	void release_staging() {
		for (int i = 0; i < STAGING_RING; i++) {
			if (staging[i]) {
				staging[i]->Release();
				staging[i] = nullptr;
			}
			staging_filled[i] = false;
		}
		next = 0;
	}
};

LibretroD3D11 *LibretroD3D11::create() {
	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, &level, &context);
	if (FAILED(hr)) {
		print_error(vformat("libretro: can't create a D3D11 device (0x%08x).", (uint32_t)hr));
		return nullptr;
	}
	LibretroD3D11 *self = new LibretroD3D11();
	self->data = new Data();
	self->data->device = device;
	self->data->context = context;
	retro_hw_render_interface_d3d11 &iface = self->data->iface;
	iface.interface_type = RETRO_HW_RENDER_INTERFACE_D3D11;
	iface.interface_version = 1;
	iface.handle = self;
	iface.device = device;
	iface.context = context;
	iface.featureLevel = level;
	iface.D3DCompile = nullptr; // Cores that compile shaders import D3DCompile themselves.
	print_line(vformat("libretro: D3D11 device, feature level %x", (int)level));
	return self;
}

LibretroD3D11::~LibretroD3D11() {
	data->release_staging();
	if (data->context) {
		data->context->ClearState();
		data->context->Flush();
		data->context->Release();
	}
	if (data->device) {
		data->device->Release();
	}
	delete data;
}

void *LibretroD3D11::get_interface() {
	return &data->iface;
}

bool LibretroD3D11::read_frame(unsigned p_width, unsigned p_height, PackedByteArray &r_rgba, unsigned &r_width, unsigned &r_height) {
	ID3D11ShaderResourceView *view = nullptr;
	data->context->PSGetShaderResources(0, 1, &view);
	if (!view) {
		return false;
	}
	ID3D11Resource *resource = nullptr;
	view->GetResource(&resource);
	view->Release();
	ID3D11Texture2D *frame = nullptr;
	const HRESULT qi = resource ? resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&frame) : E_FAIL;
	if (resource) {
		resource->Release();
	}
	if (FAILED(qi) || !frame) {
		return false;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	frame->GetDesc(&desc);
	const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
	const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
	if ((!bgra && !rgba) || desc.SampleDesc.Count != 1) {
		if (!data->format_reported) {
			data->format_reported = true;
			print_error(vformat("libretro: D3D11 frame format %d (%d samples) is not supported.", (int)desc.Format, (int)desc.SampleDesc.Count));
		}
		frame->Release();
		return false;
	}
	const unsigned width = MIN(p_width, desc.Width);
	const unsigned height = MIN(p_height, desc.Height);

	D3D11_TEXTURE2D_DESC &sd = data->staging_desc;
	if (!data->staging[0] || sd.Width != desc.Width || sd.Height != desc.Height || sd.Format != desc.Format) {
		data->release_staging();
		sd = desc;
		sd.MipLevels = 1;
		sd.ArraySize = 1;
		sd.Usage = D3D11_USAGE_STAGING;
		sd.BindFlags = 0;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		sd.MiscFlags = 0;
		for (int i = 0; i < STAGING_RING; i++) {
			if (FAILED(data->device->CreateTexture2D(&sd, nullptr, &data->staging[i]))) {
				data->release_staging();
				frame->Release();
				return false;
			}
		}
	}
	const int write = data->next;
	data->next = (write + 1) % STAGING_RING;
	const D3D11_BOX box = { 0, 0, 0, width, height, 1 };
	data->context->CopySubresourceRegion(data->staging[write], 0, 0, 0, 0, frame, 0, &box);
	frame->Release();
	data->staging_width[write] = width;
	data->staging_height[write] = height;
	data->staging_filled[write] = true;

	// The oldest copy in the ring; until the ring has filled up there is nothing to show yet.
	const int read = data->next;
	if (!data->staging_filled[read]) {
		return false;
	}
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	HRESULT hr = data->context->Map(data->staging[read], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
	if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
		// Two frames behind and still not done: the GPU is the bottleneck; wait for this one.
		hr = data->context->Map(data->staging[read], 0, D3D11_MAP_READ, 0, &mapped);
	}
	if (FAILED(hr)) {
		return false;
	}
	const unsigned out_width = data->staging_width[read];
	const unsigned out_height = data->staging_height[read];
	r_rgba.resize(out_width * out_height * 4);
	uint32_t *dst = (uint32_t *)r_rgba.ptrw();
	for (unsigned y = 0; y < out_height; y++) {
		const uint32_t *src = (const uint32_t *)((const uint8_t *)mapped.pData + y * mapped.RowPitch);
		uint32_t *out = dst + y * out_width;
		// PS2 alpha means blending, not coverage: the picture is shown opaque.
		if (bgra) {
			for (unsigned x = 0; x < out_width; x++) {
				const uint32_t c = src[x];
				out[x] = (c & 0x0000FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16) | 0xFF000000u;
			}
		} else {
			for (unsigned x = 0; x < out_width; x++) {
				out[x] = src[x] | 0xFF000000u;
			}
		}
	}
	data->context->Unmap(data->staging[read], 0);
	r_width = out_width;
	r_height = out_height;
	return true;
}

#ifdef UWP_ENABLED

#include <fileapifromapp.h>

#include <string>

namespace {

// The brokered calls want Windows paths: "E:/Roms/x.iso" is refused (4252), "E:\\Roms\\x.iso" works.
struct WindowsPath {
	std::wstring path;
	explicit WindowsPath(LPCWSTR p_name) {
		if (p_name) {
			path = p_name;
			for (wchar_t &c : path) {
				if (c == L'/') {
					c = L'\\';
				}
			}
		}
	}
	LPCWSTR get() const { return path.empty() ? nullptr : path.c_str(); }
};

// Plain access first (the app's own folders, the package), the broker only when that is refused:
// brokered calls reject some flags (.NET's SECURITY_SQOS_PRESENT) even on paths the app owns.
HANDLE WINAPI redirected_CreateFileW(LPCWSTR p_name, DWORD p_access, DWORD p_share, LPSECURITY_ATTRIBUTES p_security, DWORD p_disposition, DWORD p_flags, HANDLE p_template) {
	CREATEFILE2_EXTENDED_PARAMETERS params = {};
	params.dwSize = sizeof(params);
	params.dwFileAttributes = p_flags & 0x0000FFFF;
	params.dwFileFlags = p_flags & 0xFFF00000;
	params.dwSecurityQosFlags = p_flags & 0x000F0000;
	params.lpSecurityAttributes = p_security;
	params.hTemplateFile = p_template;
	HANDLE handle = CreateFile2(p_name, p_access, p_share, p_disposition, &params);
	if (handle != INVALID_HANDLE_VALUE) {
		return handle;
	}
	const DWORD plain_error = GetLastError();
	if (plain_error == ERROR_FILE_NOT_FOUND || plain_error == ERROR_PATH_NOT_FOUND || plain_error == ERROR_FILE_EXISTS) {
		return handle;
	}
	// The broker refuses (4252) what the CRT's _wfopen passes along: security attributes and QoS
	// flags. Godot's own brokered calls pass neither and reach USB drives fine.
	params.dwSecurityQosFlags = 0;
	params.lpSecurityAttributes = nullptr;
	params.hTemplateFile = nullptr;
	handle = CreateFile2FromAppW(WindowsPath(p_name).get(), p_access, p_share, p_disposition, &params);
	if (handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_NOT_SUPPORTED_IN_APPCONTAINER) {
		// Still refused: try the plainest request that can serve it (no attributes or flags).
		CREATEFILE2_EXTENDED_PARAMETERS plain = {};
		plain.dwSize = sizeof(plain);
		handle = CreateFile2FromAppW(WindowsPath(p_name).get(), p_access & (GENERIC_READ | GENERIC_WRITE), p_share, p_disposition, &plain);
		print_line(vformat("uwp-redirect: CreateFileW(%s) retried without flags: %s", String(p_name), handle != INVALID_HANDLE_VALUE ? "ok" : itos(GetLastError())));
	}
	if (handle == INVALID_HANDLE_VALUE) {
		const DWORD error = GetLastError();
		print_line(vformat("uwp-redirect: CreateFileW(%s, access %x, share %x, disposition %d, flags %x) failed: plain %d, brokered %d", String(p_name), (int)p_access, (int)p_share, (int)p_disposition, (int)p_flags, (int)plain_error, (int)error));
		SetLastError(error);
	}
	return handle;
}

HANDLE WINAPI redirected_FindFirstFileW(LPCWSTR p_name, LPWIN32_FIND_DATAW r_data) {
	return FindFirstFileExFromAppW(WindowsPath(p_name).get(), FindExInfoStandard, r_data, FindExSearchNameMatch, nullptr, 0);
}

HANDLE WINAPI redirected_FindFirstFileExW(LPCWSTR p_name, FINDEX_INFO_LEVELS p_level, LPVOID r_data, FINDEX_SEARCH_OPS p_op, LPVOID p_filter, DWORD p_flags) {
	return FindFirstFileExFromAppW(WindowsPath(p_name).get(), p_level, r_data, p_op, p_filter, p_flags);
}

BOOL WINAPI redirected_GetFileAttributesExW(LPCWSTR p_name, GET_FILEEX_INFO_LEVELS p_level, LPVOID r_info) {
	return GetFileAttributesExFromAppW(WindowsPath(p_name).get(), p_level, r_info);
}

BOOL WINAPI redirected_SetFileAttributesW(LPCWSTR p_name, DWORD p_attributes) {
	return SetFileAttributesFromAppW(WindowsPath(p_name).get(), p_attributes);
}

BOOL WINAPI redirected_CreateDirectoryW(LPCWSTR p_name, LPSECURITY_ATTRIBUTES p_security) {
	return CreateDirectoryFromAppW(WindowsPath(p_name).get(), p_security);
}

BOOL WINAPI redirected_RemoveDirectoryW(LPCWSTR p_name) {
	return RemoveDirectoryFromAppW(WindowsPath(p_name).get());
}

BOOL WINAPI redirected_DeleteFileW(LPCWSTR p_name) {
	return DeleteFileFromAppW(WindowsPath(p_name).get());
}

BOOL WINAPI redirected_CopyFileW(LPCWSTR p_from, LPCWSTR p_to, BOOL p_fail_if_exists) {
	return CopyFileFromAppW(WindowsPath(p_from).get(), WindowsPath(p_to).get(), p_fail_if_exists);
}

DWORD WINAPI redirected_GetFileAttributesW(LPCWSTR p_name) {
	WIN32_FILE_ATTRIBUTE_DATA info = {};
	if (!GetFileAttributesExFromAppW(WindowsPath(p_name).get(), GetFileExInfoStandard, &info)) {
		return INVALID_FILE_ATTRIBUTES;
	}
	return info.dwFileAttributes;
}

BOOL WINAPI redirected_MoveFileExW(LPCWSTR p_from, LPCWSTR p_to, DWORD p_flags) {
	if ((p_flags & MOVEFILE_REPLACE_EXISTING) && p_to) {
		DeleteFileFromAppW(WindowsPath(p_to).get());
	}
	return MoveFileFromAppW(WindowsPath(p_from).get(), WindowsPath(p_to).get());
}

// UWP hands out executable memory only through VirtualProtectFromApp (with codeGeneration):
// VirtualAlloc(FromApp) refuses PAGE_EXECUTE_*. JIT cores (PCSX2) ask for RWX blocks directly, so
// those are allocated without execute and then turned executable.
DWORD without_execute(DWORD p_protect) {
	switch (p_protect & 0xFF) {
		case PAGE_EXECUTE:
			return (p_protect & ~0xFF) | PAGE_NOACCESS;
		case PAGE_EXECUTE_READ:
			return (p_protect & ~0xFF) | PAGE_READONLY;
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return (p_protect & ~0xFF) | PAGE_READWRITE;
		default:
			return p_protect;
	}
}

LPVOID WINAPI redirected_VirtualAlloc(LPVOID p_address, SIZE_T p_size, DWORD p_type, DWORD p_protect) {
	const DWORD plain = without_execute(p_protect);
	void *memory = VirtualAllocFromApp(p_address, p_size, p_type, plain);
	if (!memory) {
		const DWORD error = GetLastError();
		print_line(vformat("uwp-redirect: VirtualAlloc(%x, %d MB, type %x, protect %x) failed: %d", (uint64_t)p_address, (int64_t)(p_size >> 20), (int)p_type, (int)p_protect, (int)error));
		SetLastError(error);
		return nullptr;
	}
	if (plain == p_protect || !(p_type & MEM_COMMIT)) {
		return memory;
	}
	ULONG old = 0;
	if (!VirtualProtectFromApp(memory, p_size, p_protect, &old)) {
		const DWORD error = GetLastError();
		print_line(vformat("uwp-redirect: making %d MB executable (%x) failed: %d", (int64_t)(p_size >> 20), (int)p_protect, (int)error));
		if (!p_address || (p_type & MEM_RESERVE)) {
			VirtualFree(memory, 0, MEM_RELEASE);
		}
		SetLastError(error);
		return nullptr;
	}
	return memory;
}

BOOL WINAPI redirected_VirtualProtect(LPVOID p_address, SIZE_T p_size, DWORD p_protect, PDWORD r_old) {
	ULONG old = 0;
	const BOOL ok = VirtualProtectFromApp(p_address, p_size, p_protect, &old);
	if (!ok && p_size >= 1024 * 1024) {
		const DWORD error = GetLastError();
		print_line(vformat("uwp-redirect: VirtualProtect(%d MB, %x) failed: %d", (int64_t)(p_size >> 20), (int)p_protect, (int)error));
		SetLastError(error);
	}
	if (r_old) {
		*r_old = old;
	}
	return ok;
}

// Sections the JIT maps executable (CreateFileMapping + MapViewOfFile with FILE_MAP_EXECUTE)
// need the FromApp variants too.
HANDLE WINAPI redirected_CreateFileMappingW(HANDLE p_file, LPSECURITY_ATTRIBUTES p_security, DWORD p_protect, DWORD p_size_high, DWORD p_size_low, LPCWSTR p_name) {
	HANDLE mapping = CreateFileMappingFromApp(p_file, p_security, p_protect, ((ULONG64)p_size_high << 32) | p_size_low, p_name);
	if (!mapping) {
		const DWORD error = GetLastError();
		print_line(vformat("uwp-redirect: CreateFileMapping(%d MB, %x) failed: %d", (int64_t)((((ULONG64)p_size_high << 32) | p_size_low) >> 20), (int)p_protect, (int)error));
		SetLastError(error);
	}
	return mapping;
}

LPVOID WINAPI redirected_MapViewOfFile(HANDLE p_mapping, DWORD p_access, DWORD p_offset_high, DWORD p_offset_low, SIZE_T p_size) {
	return MapViewOfFileFromApp(p_mapping, p_access, ((ULONG64)p_offset_high << 32) | p_offset_low, p_size);
}

ULONG page_protection_for(DWORD p_access) {
	const bool execute = p_access & FILE_MAP_EXECUTE;
	const bool write = (p_access & FILE_MAP_WRITE) || (p_access & FILE_MAP_ALL_ACCESS) == FILE_MAP_ALL_ACCESS;
	if (p_access & FILE_MAP_COPY) {
		return execute ? PAGE_EXECUTE_WRITECOPY : PAGE_WRITECOPY;
	}
	if (write) {
		return execute ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
	}
	return execute ? PAGE_EXECUTE_READ : PAGE_READONLY;
}

LPVOID WINAPI redirected_MapViewOfFileEx(HANDLE p_mapping, DWORD p_access, DWORD p_offset_high, DWORD p_offset_low, SIZE_T p_size, LPVOID p_base) {
	const ULONG64 offset = ((ULONG64)p_offset_high << 32) | p_offset_low;
	if (!p_base) {
		return MapViewOfFileFromApp(p_mapping, p_access, offset, p_size);
	}
	// The only FromApp call that maps at a chosen address. PCSX2 needs its memory map there: it
	// searches the free space near its own image (the JIT reaches it with 32-bit offsets) and takes
	// the first address where the view lands exactly; anywhere else it aborts on the first game.
	const ULONG protection = page_protection_for(p_access);
	LPVOID view = MapViewOfFile3FromApp(p_mapping, GetCurrentProcess(), p_base, offset, p_size, 0, protection, nullptr, 0);
	if (view) {
		return view;
	}
	const DWORD error = GetLastError();
	// Second way: reserve the address as a placeholder, then put the view in its place.
	LPVOID placeholder = VirtualAlloc2FromApp(GetCurrentProcess(), p_base, p_size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
	if (placeholder == p_base) {
		view = MapViewOfFile3FromApp(p_mapping, GetCurrentProcess(), p_base, offset, p_size, MEM_REPLACE_PLACEHOLDER, protection, nullptr, 0);
		if (!view) {
			const DWORD placeholder_error = GetLastError();
			VirtualFree(placeholder, 0, MEM_RELEASE);
			SetLastError(placeholder_error);
		}
	} else if (placeholder) {
		VirtualFree(placeholder, 0, MEM_RELEASE);
	}
	if (p_size >= 16 * 1024 * 1024) {
		print_line(vformat("uwp-redirect: MapViewOfFileEx(%d MB at %x): direct %d, placeholder %s (%d)", (int64_t)(p_size >> 20), (uint64_t)(uintptr_t)p_base, (int)error, view ? "ok" : (placeholder == p_base ? "view failed" : "no reserve"), view ? 0 : (int)GetLastError()));
	}
	if (!view) {
		SetLastError(error);
	}
	return view;
}

struct Redirect {
	const char *name;
	void *to;
};

const Redirect redirects[] = {
	{ "CreateFileW", (void *)&redirected_CreateFileW },
	{ "FindFirstFileExW", (void *)&redirected_FindFirstFileExW },
	{ "FindFirstFileW", (void *)&redirected_FindFirstFileW },
	{ "GetFileAttributesExW", (void *)&redirected_GetFileAttributesExW },
	{ "GetFileAttributesW", (void *)&redirected_GetFileAttributesW },
	{ "SetFileAttributesW", (void *)&redirected_SetFileAttributesW },
	{ "CreateDirectoryW", (void *)&redirected_CreateDirectoryW },
	{ "RemoveDirectoryW", (void *)&redirected_RemoveDirectoryW },
	{ "DeleteFileW", (void *)&redirected_DeleteFileW },
	{ "CopyFileW", (void *)&redirected_CopyFileW },
	{ "MoveFileExW", (void *)&redirected_MoveFileExW },
	{ "VirtualAlloc", (void *)&redirected_VirtualAlloc },
	{ "VirtualProtect", (void *)&redirected_VirtualProtect },
	{ "CreateFileMappingW", (void *)&redirected_CreateFileMappingW },
	{ "MapViewOfFile", (void *)&redirected_MapViewOfFile },
	{ "MapViewOfFileEx", (void *)&redirected_MapViewOfFileEx },
};

} // namespace

void libretro_redirect_file_api(void *p_module) {
	uint8_t *base = (uint8_t *)p_module;
	if (!base) {
		return;
	}
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
	const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (dir.VirtualAddress == 0) {
		return;
	}
	int patched = 0;
	for (const IMAGE_IMPORT_DESCRIPTOR *desc = (const IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); desc->Name; desc++) {
		if (!desc->OriginalFirstThunk) {
			continue; // Without the name table there is nothing to match by name.
		}
		const IMAGE_THUNK_DATA *names = (const IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
		IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
		for (; names->u1.AddressOfData; names++, slots++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
				continue;
			}
			const char *name = (const char *)((const IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
			for (const Redirect &r : redirects) {
				if (strcmp(name, r.name) != 0) {
					continue;
				}
				ULONG old = 0;
				if (VirtualProtectFromApp(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old)) {
					slots->u1.Function = (ULONGLONG)r.to;
					VirtualProtectFromApp(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
					patched++;
				}
				break;
			}
		}
	}
	if (patched) {
		print_line(vformat("libretro: %d file and memory imports of the core redirected for UWP", patched));
	}
}

#else

void libretro_redirect_file_api(void *p_module) {}

#endif // UWP_ENABLED

#else // !WINDOWS_ENABLED

LibretroD3D11 *LibretroD3D11::create() {
	return nullptr;
}

LibretroD3D11::~LibretroD3D11() {}

void *LibretroD3D11::get_interface() {
	return nullptr;
}

bool LibretroD3D11::read_frame(unsigned p_width, unsigned p_height, PackedByteArray &r_rgba, unsigned &r_width, unsigned &r_height) {
	return false;
}

void libretro_redirect_file_api(void *p_module) {}

#endif // WINDOWS_ENABLED
