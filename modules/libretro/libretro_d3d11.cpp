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

struct LibretroD3D11::Data {
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	ID3D11Texture2D *staging = nullptr;
	D3D11_TEXTURE2D_DESC staging_desc = {};
	retro_hw_render_interface_d3d11 iface = {};
	bool format_reported = false;
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
	if (data->staging) {
		data->staging->Release();
	}
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
	if (!data->staging || sd.Width != desc.Width || sd.Height != desc.Height || sd.Format != desc.Format) {
		if (data->staging) {
			data->staging->Release();
			data->staging = nullptr;
		}
		sd = desc;
		sd.MipLevels = 1;
		sd.ArraySize = 1;
		sd.Usage = D3D11_USAGE_STAGING;
		sd.BindFlags = 0;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		sd.MiscFlags = 0;
		if (FAILED(data->device->CreateTexture2D(&sd, nullptr, &data->staging))) {
			data->staging = nullptr;
			frame->Release();
			return false;
		}
	}
	const D3D11_BOX box = { 0, 0, 0, width, height, 1 };
	data->context->CopySubresourceRegion(data->staging, 0, 0, 0, 0, frame, 0, &box);
	frame->Release();

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(data->context->Map(data->staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		return false;
	}
	r_rgba.resize(width * height * 4);
	uint8_t *dst = r_rgba.ptrw();
	for (unsigned y = 0; y < height; y++) {
		const uint8_t *src = (const uint8_t *)mapped.pData + y * mapped.RowPitch;
		uint8_t *out = dst + y * width * 4;
		for (unsigned x = 0; x < width; x++) {
			// PS2 alpha means blending, not coverage: the picture is shown opaque.
			out[x * 4 + 0] = src[x * 4 + (bgra ? 2 : 0)];
			out[x * 4 + 1] = src[x * 4 + 1];
			out[x * 4 + 2] = src[x * 4 + (bgra ? 0 : 2)];
			out[x * 4 + 3] = 0xFF;
		}
	}
	data->context->Unmap(data->staging, 0);
	r_width = width;
	r_height = height;
	return true;
}

#ifdef UWP_ENABLED

#include <fileapifromapp.h>

namespace {

HANDLE WINAPI redirected_FindFirstFileW(LPCWSTR p_name, LPWIN32_FIND_DATAW r_data) {
	return FindFirstFileExFromAppW(p_name, FindExInfoStandard, r_data, FindExSearchNameMatch, nullptr, 0);
}

DWORD WINAPI redirected_GetFileAttributesW(LPCWSTR p_name) {
	WIN32_FILE_ATTRIBUTE_DATA info = {};
	if (!GetFileAttributesExFromAppW(p_name, GetFileExInfoStandard, &info)) {
		return INVALID_FILE_ATTRIBUTES;
	}
	return info.dwFileAttributes;
}

BOOL WINAPI redirected_MoveFileExW(LPCWSTR p_from, LPCWSTR p_to, DWORD p_flags) {
	if ((p_flags & MOVEFILE_REPLACE_EXISTING) && p_to) {
		DeleteFileFromAppW(p_to);
	}
	return MoveFileFromAppW(p_from, p_to);
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
	if (!memory || plain == p_protect || !(p_type & MEM_COMMIT)) {
		return memory;
	}
	ULONG old = 0;
	if (!VirtualProtectFromApp(memory, p_size, p_protect, &old)) {
		const DWORD error = GetLastError();
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
	if (r_old) {
		*r_old = old;
	}
	return ok;
}

struct Redirect {
	const char *name;
	void *to;
};

const Redirect redirects[] = {
	{ "CreateFileW", (void *)&CreateFileFromAppW },
	{ "FindFirstFileExW", (void *)&FindFirstFileExFromAppW },
	{ "FindFirstFileW", (void *)&redirected_FindFirstFileW },
	{ "GetFileAttributesExW", (void *)&GetFileAttributesExFromAppW },
	{ "GetFileAttributesW", (void *)&redirected_GetFileAttributesW },
	{ "SetFileAttributesW", (void *)&SetFileAttributesFromAppW },
	{ "CreateDirectoryW", (void *)&CreateDirectoryFromAppW },
	{ "RemoveDirectoryW", (void *)&RemoveDirectoryFromAppW },
	{ "DeleteFileW", (void *)&DeleteFileFromAppW },
	{ "CopyFileW", (void *)&CopyFileFromAppW },
	{ "MoveFileExW", (void *)&redirected_MoveFileExW },
	{ "VirtualAlloc", (void *)&redirected_VirtualAlloc },
	{ "VirtualProtect", (void *)&redirected_VirtualProtect },
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
