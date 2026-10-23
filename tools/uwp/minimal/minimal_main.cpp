// Smallest possible UWP app on top of uwp_bridge: proves the package, manifest and launch path
// work independently of the engine. Writes <LocalState>\minimal.log and idles for a minute.

#include "../../../platform/windows/uwp_bridge.h"

#include <windows.h>

#include <string>

static void log_line(const char *p_message) {
	std::wstring path = uwp::get_local_dir();
	path += L"minimal.log";
	HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}
	std::string line = std::string(p_message) + "\r\n";
	DWORD written = 0;
	WriteFile(file, line.data(), (DWORD)line.size(), &written, nullptr);
	CloseHandle(file);
}

static int app_main() {
	log_line("minimal: app_main entered");
	int w = 0;
	int h = 0;
	uwp::get_window_size(w, h);
	log_line(("minimal: window " + std::to_string(w) + "x" + std::to_string(h)).c_str());

	for (int i = 0; i < 3600; i++) {
		uwp::process_events();
		uwp::Event e;
		while (uwp::poll_event(e)) {
			if (e.type == uwp::EventType::CLOSE) {
				return 0;
			}
		}
		uwp::GamepadState pads[uwp::MAX_GAMEPADS];
		if (i % 60 == 0) {
			log_line(("minimal: tick " + std::to_string(i) + ", gamepads " + std::to_string(uwp::poll_gamepads(pads))).c_str());
		}
		Sleep(16);
	}
	log_line("minimal: done");
	return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	return uwp::run(app_main);
}
