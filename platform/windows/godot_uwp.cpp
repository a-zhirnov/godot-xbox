/**************************************************************************/
/*  godot_uwp.cpp                                                         */
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

#include "os_windows.h"
#include "uwp_bridge.h"

#include "core/profiling/profiling.h"
#include "main/main.h"

#include <clocale>
#include <string>
#include <vector>

static std::string wide_to_utf8(const wchar_t *p_wide) {
	int len = WideCharToMultiByte(CP_UTF8, 0, p_wide, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1) {
		return std::string();
	}
	std::string out(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, p_wide, -1, out.data(), len, nullptr, nullptr);
	out.resize(len - 1);
	return out;
}

// Runs the engine on the UI thread once the CoreWindow exists.
static int godot_uwp_main() {
	godot_init_profiler();

	OS_Windows os(GetModuleHandleW(nullptr));

	setlocale(LC_CTYPE, "");

	wchar_t exe_path[1024] = {};
	GetModuleFileNameW(nullptr, exe_path, 1024);

	// The game pack ships next to the executable, named after the executable.
	std::wstring pack_path = uwp::get_install_dir();
	pack_path += L"game.pck";

	std::vector<std::string> args_storage = {
		wide_to_utf8(exe_path),
		"--rendering-driver",
		"d3d12",
		"--main-pack",
		wide_to_utf8(pack_path.c_str()),
	};
	std::vector<char *> argv;
	for (std::string &arg : args_storage) {
		argv.push_back(arg.data());
	}
	const int argc = (int)argv.size();

	Error err = Main::setup(argv[0], argc - 1, &argv[1]);
	if (err != OK) {
		return err == ERR_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();

	godot_cleanup_profiler();
	return os.get_exit_code();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
	return uwp::run(godot_uwp_main);
}
