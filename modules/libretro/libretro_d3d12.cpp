/**************************************************************************/
/*  libretro_d3d12.cpp                                                    */
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

#include "libretro_d3d12.h"

#include "core/string/print_string.h"

#ifdef WINDOWS_ENABLED

#include <windows.h>

#include <d3d12.h>

#include "libretro.h"

namespace {

// Version 1 of retro_hw_render_interface_d3d12 (libretro_d3d12.h), laid out the same.
struct InterfaceD3D12 {
	retro_hw_render_interface_type interface_type;
	unsigned interface_version;
	void *handle;
	ID3D12Device *device;
	ID3D12CommandQueue *queue;
	void *D3DCompile; // Cores that compile shaders load the compiler themselves.
	D3D12_RESOURCE_STATES required_state;
	void (*set_texture)(void *p_handle, ID3D12Resource *p_texture, DXGI_FORMAT p_format);
};

// Frames come back through a ring: this frame is copied into one slot, and the slot copied
// RING - 1 frames ago is read - the GPU finished it long ago.
constexpr int RING = 3;

struct Slot {
	ID3D12CommandAllocator *allocator = nullptr;
	ID3D12GraphicsCommandList *list = nullptr;
	ID3D12Resource *readback = nullptr;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT64 readback_size = 0;
	unsigned width = 0;
	unsigned height = 0;
	bool bgra = false;
	UINT64 fence_value = 0;
	bool filled = false;
};

template <typename T>
void release(T *&p_object) {
	if (p_object) {
		p_object->Release();
		p_object = nullptr;
	}
}

} // namespace

struct LibretroD3D12::Data {
	ID3D12Device *device = nullptr;
	ID3D12CommandQueue *queue = nullptr;
	ID3D12Fence *fence = nullptr;
	HANDLE fence_event = nullptr;
	UINT64 fence_value = 0;
	Slot slots[RING];
	int next = 0;
	// The texture set_texture handed over for the frame being refreshed.
	ID3D12Resource *frame = nullptr;
	DXGI_FORMAT frame_format = DXGI_FORMAT_UNKNOWN;
	InterfaceD3D12 iface = {};
	bool format_reported = false;

	void wait(UINT64 p_value) {
		if (fence->GetCompletedValue() < p_value) {
			fence->SetEventOnCompletion(p_value, fence_event);
			WaitForSingleObject(fence_event, INFINITE);
		}
	}

	static void set_texture(void *p_handle, ID3D12Resource *p_texture, DXGI_FORMAT p_format) {
		Data *self = (Data *)p_handle;
		self->frame = p_texture;
		self->frame_format = p_format;
	}
};

LibretroD3D12 *LibretroD3D12::create() {
	ID3D12Device *device = nullptr;
	HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
	if (FAILED(hr)) {
		print_error(vformat("libretro: can't create a D3D12 device (0x%08x).", (uint32_t)hr));
		return nullptr;
	}
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ID3D12CommandQueue *queue = nullptr;
	ID3D12Fence *fence = nullptr;
	if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
		print_error("libretro: can't create the D3D12 queue.");
		release(queue);
		release(device);
		return nullptr;
	}
	LibretroD3D12 *self = new LibretroD3D12();
	self->data = new Data();
	Data *data = self->data;
	data->device = device;
	data->queue = queue;
	data->fence = fence;
	data->fence_event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
	InterfaceD3D12 &iface = data->iface;
	iface.interface_type = RETRO_HW_RENDER_INTERFACE_D3D12;
	iface.interface_version = 1;
	iface.handle = data;
	iface.device = device;
	iface.queue = queue;
	iface.D3DCompile = nullptr;
	// The frame is only ever copied from.
	iface.required_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
	iface.set_texture = &Data::set_texture;
	print_line("libretro: D3D12 device and queue for the core");
	return self;
}

LibretroD3D12::~LibretroD3D12() {
	if (data->fence) {
		data->wait(data->fence_value);
	}
	for (Slot &slot : data->slots) {
		release(slot.list);
		release(slot.allocator);
		release(slot.readback);
	}
	release(data->fence);
	if (data->fence_event) {
		CloseHandle(data->fence_event);
	}
	release(data->queue);
	release(data->device);
	delete data;
}

void *LibretroD3D12::get_interface() {
	return &data->iface;
}

bool LibretroD3D12::read_frame(unsigned p_width, unsigned p_height, PackedByteArray &r_rgba, unsigned &r_width, unsigned &r_height) {
	ID3D12Resource *frame = data->frame;
	data->frame = nullptr;
	if (!frame) {
		return false;
	}
	const D3D12_RESOURCE_DESC desc = frame->GetDesc();
	const DXGI_FORMAT format = desc.Format;
	const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM;
	const bool rgba = format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
	if ((!bgra && !rgba) || desc.SampleDesc.Count != 1 || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
		if (!data->format_reported) {
			data->format_reported = true;
			print_error(vformat("libretro: D3D12 frame format %d (%d samples) is not supported.", (int)format, (int)desc.SampleDesc.Count));
		}
		return false;
	}
	const unsigned width = MIN(p_width, (unsigned)desc.Width);
	const unsigned height = MIN(p_height, (unsigned)desc.Height);

	// Write this frame into the next slot (whose previous copy was read two frames ago).
	Slot &slot = data->slots[data->next];
	data->next = (data->next + 1) % RING;
	data->wait(slot.fence_value);
	if (!slot.allocator) {
		if (FAILED(data->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator))) ||
				FAILED(data->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator, nullptr, IID_PPV_ARGS(&slot.list)))) {
			return false;
		}
		slot.list->Close();
	}
	D3D12_RESOURCE_DESC region = desc;
	region.Width = width;
	region.Height = height;
	region.MipLevels = 1;
	region.DepthOrArraySize = 1;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT64 size = 0;
	data->device->GetCopyableFootprints(&region, 0, 1, 0, &footprint, nullptr, nullptr, &size);
	if (!slot.readback || slot.readback_size < size) {
		release(slot.readback);
		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC buffer = {};
		buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buffer.Width = size;
		buffer.Height = 1;
		buffer.DepthOrArraySize = 1;
		buffer.MipLevels = 1;
		buffer.SampleDesc.Count = 1;
		buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(data->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&slot.readback)))) {
			return false;
		}
		slot.readback_size = size;
	}
	slot.allocator->Reset();
	slot.list->Reset(slot.allocator, nullptr);
	D3D12_TEXTURE_COPY_LOCATION source = {};
	source.pResource = frame;
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION destination = {};
	destination.pResource = slot.readback;
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint = footprint;
	const D3D12_BOX box = { 0, 0, 0, width, height, 1 };
	slot.list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
	slot.list->Close();
	ID3D12CommandList *lists[] = { slot.list };
	// The core has submitted its drawing on this queue already: the copy runs after it.
	data->queue->ExecuteCommandLists(1, lists);
	data->queue->Signal(data->fence, ++data->fence_value);
	slot.fence_value = data->fence_value;
	slot.footprint = footprint;
	slot.width = width;
	slot.height = height;
	slot.bgra = bgra;
	slot.filled = true;

	// The oldest copy in the ring; until the ring has filled up there is nothing to show yet.
	Slot &read = data->slots[data->next];
	if (!read.filled) {
		return false;
	}
	// Two frames behind and still not done: the GPU is the bottleneck; wait for this one.
	data->wait(read.fence_value);
	void *mapped = nullptr;
	const D3D12_RANGE range = { 0, (SIZE_T)read.readback_size };
	if (FAILED(read.readback->Map(0, &range, &mapped))) {
		return false;
	}
	r_rgba.resize(read.width * read.height * 4);
	uint32_t *dst = (uint32_t *)r_rgba.ptrw();
	for (unsigned y = 0; y < read.height; y++) {
		const uint32_t *src = (const uint32_t *)((const uint8_t *)mapped + read.footprint.Offset + (UINT64)y * read.footprint.Footprint.RowPitch);
		uint32_t *out = dst + y * read.width;
		// PS2 alpha means blending, not coverage: the picture is shown opaque.
		if (read.bgra) {
			for (unsigned x = 0; x < read.width; x++) {
				const uint32_t c = src[x];
				out[x] = (c & 0x0000FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16) | 0xFF000000u;
			}
		} else {
			for (unsigned x = 0; x < read.width; x++) {
				out[x] = src[x] | 0xFF000000u;
			}
		}
	}
	const D3D12_RANGE written = { 0, 0 };
	read.readback->Unmap(0, &written);
	r_width = read.width;
	r_height = read.height;
	return true;
}

#else // !WINDOWS_ENABLED

LibretroD3D12 *LibretroD3D12::create() {
	return nullptr;
}

LibretroD3D12::~LibretroD3D12() {
}

void *LibretroD3D12::get_interface() {
	return nullptr;
}

bool LibretroD3D12::read_frame(unsigned p_width, unsigned p_height, PackedByteArray &r_rgba, unsigned &r_width, unsigned &r_height) {
	return false;
}

#endif // WINDOWS_ENABLED
