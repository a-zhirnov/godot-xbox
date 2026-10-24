/**************************************************************************/
/*  libretro_d3d12.h                                                      */
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

#include "core/variant/variant.h"

// The D3D12 context a libretro core with hardware rendering (PCSX2) draws into: a device and a
// direct queue of the module's own, next to the engine's. On Xbox D3D11 runs through d3d11on12,
// and PCSX2's thousands of calls a frame through that layer cost more than the emulation itself
// (NFS Most Wanted ran at 40% speed); its own D3D12 renderer has no such layer.
// Version 1 of libretro_d3d12.h: the core submits the frame on our queue, hands the texture over
// with set_texture and calls video_refresh; the frame is copied on the same queue (so after the
// core's drawing) into a readback buffer and read two frames later, never waiting on the GPU.
class LibretroD3D12 {
	struct Data;
	Data *data = nullptr;

	LibretroD3D12() = default;

public:
	static LibretroD3D12 *create();
	~LibretroD3D12();

	// A retro_hw_render_interface_d3d12 for RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE.
	void *get_interface();
	// The frame handed over by set_texture (up to p_width x p_height), as RGBA8 in r_rgba - the one
	// from two frames ago: false until the ring has filled up.
	bool read_frame(unsigned p_width, unsigned p_height, PackedByteArray &r_rgba, unsigned &r_width, unsigned &r_height);
};
