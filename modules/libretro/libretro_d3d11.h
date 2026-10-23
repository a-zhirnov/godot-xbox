/**************************************************************************/
/*  libretro_d3d11.h                                                      */
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

// The D3D11 context a libretro core with hardware rendering (PCSX2) draws into. A device of its
// own, next to the engine's D3D12 one: the core renders, binds the frame to pixel shader slot 0
// and calls video_refresh (version 1 of libretro_d3d11.h); the frame is then read back to RGBA8
// and shown through the same texture as the software cores.
class LibretroD3D11 {
	struct Data;
	Data *data = nullptr;

	LibretroD3D11() = default;

public:
	static LibretroD3D11 *create();
	~LibretroD3D11();

	// A retro_hw_render_interface_d3d11 for RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE.
	void *get_interface();
	// Copies the frame bound to pixel shader slot 0 (up to p_width x p_height) into r_rgba.
	bool read_frame(unsigned p_width, unsigned p_height, PackedByteArray &r_rgba, unsigned &r_width, unsigned &r_height);
};

// Points a core's file imports (CreateFileW, FindFirstFileExW, ...) at the brokered "FromApp"
// variants, so a core that opens its game itself (need_fullpath) reaches USB drives on Xbox.
// Does nothing outside UWP.
void libretro_redirect_file_api(void *p_module);
