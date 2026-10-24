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

#include "core/io/logger.h"
#include "core/os/thread.h"
#include "core/profiling/profiling.h"
#include "main/main.h"

#include <clocale>
#include <mutex>
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

// Appends a line to <LocalState>\\uwp_boot.log; the only diagnostics available on a console without a debugger.
// One log per run (truncated at start, so it never grows), opened once and written with a
// timestamp: the per-line open/close of earlier builds slowed start-up noticeably.
static void boot_log(const char *p_message) {
	static std::mutex lock;
	static HANDLE file = INVALID_HANDLE_VALUE;
	static ULONGLONG started = GetTickCount64();
	std::lock_guard<std::mutex> guard(lock);
	if (file == INVALID_HANDLE_VALUE) {
		std::wstring path = uwp::get_local_dir();
		path += L"uwp_boot.log";
		// The previous run's log survives one start: after a crash it's the one that matters.
		MoveFileExW(path.c_str(), (std::wstring(uwp::get_local_dir()) + L"uwp_boot.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
		file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
	}
	char stamp[32];
	snprintf(stamp, sizeof(stamp), "[%7.2f] ", (GetTickCount64() - started) / 1000.0);
	std::string line = std::string(stamp) + p_message + "\r\n";
	DWORD written = 0;
	WriteFile(file, line.data(), (DWORD)line.size(), &written, nullptr);
}

static ULONGLONG boot_started_ms = GetTickCount64();

// Crash tracer: the console's full dumps are gigabytes (hours to download). For an exception nobody
// handled - PCSX2's fastmem handler registers first and takes its own faults - this writes the code,
// the faulting module+offset and an unwound call stack to <LocalState>\crash_trace.log.
static HANDLE crash_file = INVALID_HANDLE_VALUE;

static void crash_write(const char *p_text) {
	if (crash_file == INVALID_HANDLE_VALUE) {
		return;
	}
	DWORD written = 0;
	WriteFile(crash_file, p_text, (DWORD)strlen(p_text), &written, nullptr);
}

static void crash_describe(DWORD64 p_address, char *r_out, size_t p_size) {
	void *base = nullptr;
	RtlPcToFileHeader((PVOID)p_address, &base);
	wchar_t module[MAX_PATH] = L"?";
	if (base) {
		GetModuleFileNameW((HMODULE)base, module, MAX_PATH);
	}
	const wchar_t *name = wcsrchr(module, L'\\');
	snprintf(r_out, p_size, "%ls+0x%llx", name ? name + 1 : module, (unsigned long long)(p_address - (DWORD64)base));
}

static LONG CALLBACK crash_tracer(EXCEPTION_POINTERS *p_info) {
	const DWORD code = p_info->ExceptionRecord->ExceptionCode;
	switch (code) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_IN_PAGE_ERROR:
		case 0xC0000409: // stack buffer overrun / fail-fast
			break;
		default:
			return EXCEPTION_CONTINUE_SEARCH;
	}
	static volatile LONG reported = 0;
	if (InterlockedIncrement(&reported) > 8) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	char line[512];
	char where[300];
	crash_describe((DWORD64)p_info->ExceptionRecord->ExceptionAddress, where, sizeof(where));
	snprintf(line, sizeof(line), "\r\n[%7.2f] exception 0x%08lx at %s, thread %lu", (GetTickCount64() - boot_started_ms) / 1000.0, code, where, GetCurrentThreadId());
	crash_write(line);
	if (code == EXCEPTION_ACCESS_VIOLATION && p_info->ExceptionRecord->NumberParameters >= 2) {
		snprintf(line, sizeof(line), ", %s 0x%llx", p_info->ExceptionRecord->ExceptionInformation[0] == 8 ? "execute" : (p_info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read"), (unsigned long long)p_info->ExceptionRecord->ExceptionInformation[1]);
		crash_write(line);
	}
	crash_write("\r\n");
	CONTEXT context = *p_info->ContextRecord;
	for (int frame = 0; frame < 24 && context.Rip; frame++) {
		crash_describe(context.Rip, where, sizeof(where));
		snprintf(line, sizeof(line), "    #%d %s\r\n", frame, where);
		crash_write(line);
		DWORD64 image_base = 0;
		PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
		if (function) {
			void *handler_data = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data, &establisher, nullptr);
		} else {
			// Leaf or JIT code without unwind data: take the return address off the stack.
			ULONG_PTR low = 0, high = 0;
			GetCurrentThreadStackLimits(&low, &high);
			if (context.Rsp < low || context.Rsp + 8 > high) {
				break;
			}
			context.Rip = *(DWORD64 *)context.Rsp;
			context.Rsp += 8;
		}
	}
	FlushFileBuffers(crash_file);
	return EXCEPTION_CONTINUE_SEARCH;
}

static void install_crash_tracer() {
	std::wstring path = std::wstring(uwp::get_local_dir()) + L"crash_trace.log";
	crash_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	LARGE_INTEGER size = {};
	if (crash_file != INVALID_HANDLE_VALUE && GetFileSizeEx(crash_file, &size) && size.QuadPart > 256 * 1024) {
		// Bounded: start over past 256 KB.
		CloseHandle(crash_file);
		crash_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	}
	crash_write("\r\n=== Socket started\r\n");
	// Last in the chain (first argument 0): handlers the cores install themselves run before it.
	AddVectoredExceptionHandler(0, crash_tracer);
}

class UwpBootLogger : public Logger {
public:
	void logv(const char *p_format, va_list p_list, bool p_err) override {
		char buffer[4096];
		vsnprintf(buffer, sizeof(buffer), p_format, p_list);
		size_t len = strlen(buffer);
		while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
			buffer[--len] = '\0';
		}
		if (len > 0) {
			boot_log(buffer);
		}
		// Safety net: Xbox ends a launch whose window is not shown within a few seconds (0x800705B4).
		// A cold shader cache can keep the boot splash away longer than that; show the window anyway.
		if (Thread::is_main_thread() && GetTickCount64() - boot_started_ms > 3500) {
			uwp::activate_window();
		}
	}
};

// Runs the engine on the UI thread once the CoreWindow exists.
static int godot_uwp_main() {
	boot_log("godot_uwp_main: entered");
	install_crash_tracer();
	godot_init_profiler();

	OS_Windows os(GetModuleHandleW(nullptr));
	// Engine output has no console on Xbox; mirror every message into the boot log from the start.
	os.add_logger(memnew(UwpBootLogger));
	boot_log("OS_Windows constructed");

	setlocale(LC_CTYPE, "");

	wchar_t exe_path[1024] = {};
	GetModuleFileNameW(nullptr, exe_path, 1024);

	std::vector<std::string> args_storage = {
		wide_to_utf8(exe_path),
		"--rendering-driver",
		"d3d12",
		// Same renderer as the Android build: identical look and far fewer shaders than Forward+.
		"--rendering-method",
		"mobile",
		// Release templates refuse path overrides (--main-pack, --log-file): the pack is found
		// next to the executable as <exe name>.pck instead.
		"--verbose",
	};
	std::vector<char *> argv;
	for (std::string &arg : args_storage) {
		argv.push_back(arg.data());
	}
	const int argc = (int)argv.size();

	boot_log("calling Main::setup");
	Error err = Main::setup(argv[0], argc - 1, &argv[1]);
	if (err != OK) {
		boot_log("Main::setup failed");
		return err == ERR_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	boot_log("Main::setup ok, calling Main::start");
	// The boot splash has been drawn by now: let the system splash go (same picture, no black gap).
	uwp::activate_window();

	if (Main::start() == EXIT_SUCCESS) {
		boot_log("Main::start ok, running");
		// Verbose output (every resource load, written to the boot log) is for start-up diagnostics
		// and the activation safety net; while running it only costs time.
		OS::get_singleton()->set_stdout_verbose(false);
		os.run();
	} else {
		boot_log("Main::start failed");
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();

	godot_cleanup_profiler();
	return os.get_exit_code();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
	return uwp::run(godot_uwp_main);
}
