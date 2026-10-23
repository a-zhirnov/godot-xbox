// A UWP exe with no C runtime and no WinRT: writes one line to <TempState>\nocrt.log and idles.
// Tells apart "the package/launch path is broken" from "the C runtime or WinRT startup crashes".

#include <windows.h>

extern "C" void NoCrtEntry() {
	wchar_t path[MAX_PATH + 16];
	DWORD len = GetTempPathW(MAX_PATH, path);
	if (len != 0 && len < MAX_PATH) {
		const wchar_t name[] = L"nocrt.log";
		for (int i = 0; i < 10; i++) {
			path[len + i] = name[i];
		}
		HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			const char text[] = "nocrt: started\r\n";
			DWORD written = 0;
			WriteFile(file, text, sizeof(text) - 1, &written, nullptr);
			CloseHandle(file);
		}
	}
	Sleep(20000);
	ExitProcess(0);
}
