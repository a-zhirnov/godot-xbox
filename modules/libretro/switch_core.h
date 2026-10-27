/**************************************************************************/
/*  switch_core.h                                                         */
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

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

// Hosts the Switch emulator core (Ryujinx.Xbox.Core, a NativeAOT build of Ryujinx with a C ABI:
// xbc_init/xbc_load/xbc_get_stats/xbc_read_log/xbc_debug_dump/xbc_shutdown) inside Socket.
// The core runs the guest on its own threads; this class only starts, watches and stops it.
class SwitchCore : public RefCounted {
	GDCLASS(SwitchCore, RefCounted);

	struct Stats {
		int64_t private_bytes;
		int64_t working_set;
		int64_t peak_working_set;
		int64_t managed_heap;
		int64_t uptime_ms;
		int32_t running;
		int32_t guest_markers;
		int64_t frames_processed;
		int64_t frames_presented;
	};

	void *library = nullptr;
	int (*xbc_init)(const char *) = nullptr;
	int (*xbc_load)(const char *, int) = nullptr;
	int (*xbc_install_firmware)(const char *) = nullptr;
	void (*xbc_get_stats)(Stats *) = nullptr;
	int (*xbc_read_log)(char *, int) = nullptr;
	void (*xbc_debug_dump)() = nullptr;
	void (*xbc_shutdown)() = nullptr;
	bool started = false;

protected:
	static void _bind_methods();

public:
	Error load_library(const String &p_path);
	// 0 on success, negative on failure (see the core's log).
	int init(const String &p_base_dir);
	// p_memory_mode: 0 software page table, 1 host mapped, 2 host mapped unsafe.
	int load_program(const String &p_path, int p_memory_mode);
	// The system firmware from a dump of the user's own Switch (.zip or folder); 0 on success.
	int install_firmware(const String &p_path);
	Dictionary get_stats();
	String read_log();
	void debug_dump();
	void shutdown();

	~SwitchCore();
};
