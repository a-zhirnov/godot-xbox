/**************************************************************************/
/*  uwp_bridge.h                                                          */
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

// Thin boundary between the engine and C++/WinRT. This header must not include
// any engine or WinRT headers, so that the WinRT translation unit and the
// engine translation units never see each other's macros and types.

#include <cstdint>

namespace uwp {

enum class EventType : int32_t {
	POINTER_DOWN, // a = button (1 left, 2 right, 3 middle), x/y = position in pixels.
	POINTER_UP, // Same as above.
	POINTER_MOVE, // x/y = position in pixels.
	POINTER_WHEEL, // a = wheel delta (+/-120 per notch).
	KEY_DOWN, // a = virtual key, b = scan code, c = repeat (1/0).
	KEY_UP, // a = virtual key, b = scan code.
	CHARACTER, // a = UTF-16 code unit.
	FOCUS, // a = 1 gained, 0 lost.
	SIZE, // a/b = new size in pixels.
	CLOSE,
	PICKED, // a = 1 if something was picked, 0 if cancelled; path via picked_path().
};

struct Event {
	EventType type = EventType::CLOSE;
	int32_t a = 0;
	int32_t b = 0;
	int32_t c = 0;
	float x = 0;
	float y = 0;
};

struct GamepadState {
	bool connected = false;
	uint32_t buttons = 0; // Windows.Gaming.Input.GamepadButtons bit mask.
	float left_x = 0;
	float left_y = 0;
	float right_x = 0;
	float right_y = 0;
	float left_trigger = 0;
	float right_trigger = 0;
};

constexpr int MAX_GAMEPADS = 8;

// Starts the CoreApplication and runs p_main on the UI thread once the main
// window exists. Returns when p_main returns.
int run(int (*p_main)());

// The CoreWindow as an IUnknown pointer (not AddRef'ed).
void *core_window();

// Dispatches pending window events into the queue returned by poll_event().
void process_events();
bool poll_event(Event &r_event);

// Current client size in physical pixels.
void get_window_size(int &r_width, int &r_height);
// Physical pixels per view pixel (DIP).
float get_scale();

// Fills r_states and returns the number of gamepad slots in use.
int poll_gamepads(GamepadState r_states[MAX_GAMEPADS]);

// Directory the package is installed to (with trailing backslash), UTF-16.
const wchar_t *get_install_dir();
// Writable local-state directory (with trailing backslash), UTF-16.
const wchar_t *get_local_dir();

// System pickers (full-screen, gamepad-friendly on Xbox). The result arrives as a PICKED event.
void pick_folder();
void pick_file();
// Path of the last PICKED event, UTF-16 (empty when cancelled).
const wchar_t *picked_path();

// Opens a URI (e.g. an emulator protocol link such as "xbsx2:?cmd=..."). Returns false if it
// could not be started.
bool launch_uri(const wchar_t *p_uri);

// Shows the window, which takes down the system splash screen. Called once the engine's own boot
// splash (the same picture) is on screen, so the start-up shows no black frames in between.
void activate_window();

} // namespace uwp
