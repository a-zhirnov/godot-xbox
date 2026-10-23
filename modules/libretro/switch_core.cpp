/**************************************************************************/
/*  switch_core.cpp                                                       */
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


#include "switch_core.h"

#include "libretro_d3d11.h"

#include "core/object/class_db.h"
#include "core/os/os.h"

#define LOAD_XBC(m_name)                                                                                                      \
	{                                                                                                                         \
		void *symbol = nullptr;                                                                                               \
		if (OS::get_singleton()->get_dynamic_library_symbol_handle(library, #m_name, symbol) != OK) {                       \
			ERR_FAIL_V_MSG(ERR_CANT_RESOLVE, vformat("Switch core %s does not export %s.", p_path, String(#m_name)));         \
		}                                                                                                                     \
		m_name = reinterpret_cast<decltype(m_name)>(symbol);                                                                  \
	}

Error SwitchCore::load_library(const String &p_path) {
	ERR_FAIL_COND_V_MSG(library != nullptr, ERR_ALREADY_IN_USE, "The Switch core is already loaded.");
	Error err = OS::get_singleton()->open_dynamic_library(p_path, library);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Can't load the Switch core %s.", p_path));
	// The JIT needs executable memory the UWP way, like the libretro cores.
	libretro_redirect_file_api(library);
	LOAD_XBC(xbc_init);
	LOAD_XBC(xbc_load);
	LOAD_XBC(xbc_get_stats);
	LOAD_XBC(xbc_read_log);
	LOAD_XBC(xbc_debug_dump);
	LOAD_XBC(xbc_shutdown);
	return OK;
}

int SwitchCore::init(const String &p_base_dir) {
	ERR_FAIL_NULL_V_MSG(xbc_init, -100, "Load the Switch core first.");
	return xbc_init(p_base_dir.utf8().get_data());
}

int SwitchCore::load_program(const String &p_path, int p_memory_mode) {
	ERR_FAIL_NULL_V_MSG(xbc_load, -100, "Load the Switch core first.");
	const int result = xbc_load(p_path.utf8().get_data(), p_memory_mode);
	started = started || result == 0;
	return result;
}

Dictionary SwitchCore::get_stats() {
	Dictionary d;
	if (!xbc_get_stats) {
		return d;
	}
	Stats s = {};
	xbc_get_stats(&s);
	d["private_mb"] = s.private_bytes >> 20;
	d["working_set_mb"] = s.working_set >> 20;
	d["peak_working_set_mb"] = s.peak_working_set >> 20;
	d["managed_mb"] = s.managed_heap >> 20;
	d["uptime_ms"] = s.uptime_ms;
	d["running"] = s.running != 0;
	d["guest_markers"] = s.guest_markers;
	return d;
}

String SwitchCore::read_log() {
	if (!xbc_read_log) {
		return String();
	}
	const int capacity = 256 * 1024;
	Vector<char> buffer;
	buffer.resize(capacity + 1);
	const int length = CLAMP(xbc_read_log(buffer.ptrw(), capacity), 0, capacity);
	return String::utf8(buffer.ptr(), length);
}

void SwitchCore::debug_dump() {
	if (xbc_debug_dump) {
		xbc_debug_dump();
	}
}

void SwitchCore::shutdown() {
	if (xbc_shutdown && started) {
		xbc_shutdown();
	}
	started = false;
	// The NativeAOT runtime can't be unloaded: the library stays mapped for the life of the process.
}

SwitchCore::~SwitchCore() {
	shutdown();
}

void SwitchCore::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_library", "path"), &SwitchCore::load_library);
	ClassDB::bind_method(D_METHOD("init", "base_dir"), &SwitchCore::init);
	ClassDB::bind_method(D_METHOD("load_program", "path", "memory_mode"), &SwitchCore::load_program);
	ClassDB::bind_method(D_METHOD("get_stats"), &SwitchCore::get_stats);
	ClassDB::bind_method(D_METHOD("read_log"), &SwitchCore::read_log);
	ClassDB::bind_method(D_METHOD("debug_dump"), &SwitchCore::debug_dump);
	ClassDB::bind_method(D_METHOD("shutdown"), &SwitchCore::shutdown);
}
