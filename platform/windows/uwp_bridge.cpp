/**************************************************************************/
/*  uwp_bridge.cpp                                                        */
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

// This translation unit is the only one that talks to C++/WinRT. It must not
// include engine headers (see uwp_bridge.h).

#include "uwp_bridge.h"

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <deque>
#include <string>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Input;
using namespace winrt::Windows::UI::ViewManagement;

namespace {

CoreWindow g_window{ nullptr };
std::deque<uwp::Event> g_events;
int (*g_main)() = nullptr;
int g_exit_code = 0;
std::wstring g_install_dir;
std::wstring g_local_dir;

float _scale() {
	try {
		return (float)DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
	} catch (...) {
		return 1.0f;
	}
}

void _push(const uwp::Event &p_event) {
	g_events.push_back(p_event);
}

int _button_from_kind(PointerUpdateKind p_kind) {
	switch (p_kind) {
		case PointerUpdateKind::RightButtonPressed:
		case PointerUpdateKind::RightButtonReleased:
			return 2;
		case PointerUpdateKind::MiddleButtonPressed:
		case PointerUpdateKind::MiddleButtonReleased:
			return 3;
		default:
			return 1;
	}
}

void _push_pointer(uwp::EventType p_type, PointerEventArgs const &p_args) {
	PointerPoint point = p_args.CurrentPoint();
	const float scale = _scale();
	uwp::Event e;
	e.type = p_type;
	e.x = point.Position().X * scale;
	e.y = point.Position().Y * scale;
	if (p_type == uwp::EventType::POINTER_WHEEL) {
		e.a = point.Properties().MouseWheelDelta();
	} else if (p_type != uwp::EventType::POINTER_MOVE) {
		e.a = _button_from_kind(point.Properties().PointerUpdateKind());
	}
	_push(e);
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
	IFrameworkView CreateView() {
		return *this;
	}

	void Initialize(CoreApplicationView const &p_view) {
		p_view.Activated([](CoreApplicationView const &, auto const &) {
			CoreWindow::GetForCurrentThread().Activate();
		});
	}

	void Load(hstring const &) {}

	void Uninitialize() {}

	void SetWindow(CoreWindow const &p_window) {
		g_window = p_window;

		p_window.PointerPressed([](CoreWindow const &, PointerEventArgs const &a) { _push_pointer(uwp::EventType::POINTER_DOWN, a); });
		p_window.PointerReleased([](CoreWindow const &, PointerEventArgs const &a) { _push_pointer(uwp::EventType::POINTER_UP, a); });
		p_window.PointerMoved([](CoreWindow const &, PointerEventArgs const &a) { _push_pointer(uwp::EventType::POINTER_MOVE, a); });
		p_window.PointerWheelChanged([](CoreWindow const &, PointerEventArgs const &a) { _push_pointer(uwp::EventType::POINTER_WHEEL, a); });

		p_window.KeyDown([](CoreWindow const &, KeyEventArgs const &a) {
			uwp::Event e;
			e.type = uwp::EventType::KEY_DOWN;
			e.a = (int32_t)a.VirtualKey();
			e.b = (int32_t)a.KeyStatus().ScanCode;
			e.c = a.KeyStatus().WasKeyDown ? 1 : 0;
			_push(e);
		});
		p_window.KeyUp([](CoreWindow const &, KeyEventArgs const &a) {
			uwp::Event e;
			e.type = uwp::EventType::KEY_UP;
			e.a = (int32_t)a.VirtualKey();
			e.b = (int32_t)a.KeyStatus().ScanCode;
			_push(e);
		});
		p_window.CharacterReceived([](CoreWindow const &, CharacterReceivedEventArgs const &a) {
			uwp::Event e;
			e.type = uwp::EventType::CHARACTER;
			e.a = (int32_t)a.KeyCode();
			_push(e);
		});
		p_window.Activated([](CoreWindow const &, WindowActivatedEventArgs const &a) {
			uwp::Event e;
			e.type = uwp::EventType::FOCUS;
			e.a = a.WindowActivationState() != CoreWindowActivationState::Deactivated ? 1 : 0;
			_push(e);
		});
		p_window.SizeChanged([](CoreWindow const &, WindowSizeChangedEventArgs const &a) {
			const float scale = _scale();
			uwp::Event e;
			e.type = uwp::EventType::SIZE;
			e.a = (int32_t)(a.Size().Width * scale);
			e.b = (int32_t)(a.Size().Height * scale);
			_push(e);
		});
		p_window.Closed([](CoreWindow const &, CoreWindowEventArgs const &) {
			uwp::Event e;
			e.type = uwp::EventType::CLOSE;
			_push(e);
		});
	}

	void Run() {
		try {
			g_install_dir = std::wstring(Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()) + L"\\";
			g_local_dir = std::wstring(Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str()) + L"\\";
		} catch (...) {
		}

		g_window.Activate();

		// Draw edge-to-edge on TVs instead of keeping the title-safe margins.
		try {
			ApplicationView::GetForCurrentView().SetDesiredBoundsMode(ApplicationViewBoundsMode::UseCoreWindow);
		} catch (...) {
		}
		try {
			ApplicationViewScaling::TrySetDisableLayoutScaling(true);
		} catch (...) {
		}

		g_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
		g_exit_code = g_main();
	}
};

} // namespace

namespace uwp {

int run(int (*p_main)()) {
	g_main = p_main;
	CoreApplication::Run(make<App>());
	return g_exit_code;
}

void *core_window() {
	return winrt::get_unknown(g_window);
}

void process_events() {
	if (g_window) {
		g_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
	}
}

bool poll_event(Event &r_event) {
	if (g_events.empty()) {
		return false;
	}
	r_event = g_events.front();
	g_events.pop_front();
	return true;
}

void get_window_size(int &r_width, int &r_height) {
	const float scale = _scale();
	Rect bounds = g_window.Bounds();
	r_width = (int)(bounds.Width * scale);
	r_height = (int)(bounds.Height * scale);
}

float get_scale() {
	return _scale();
}

int poll_gamepads(GamepadState r_states[MAX_GAMEPADS]) {
	auto pads = Gamepad::Gamepads();
	int count = (int)pads.Size();
	if (count > MAX_GAMEPADS) {
		count = MAX_GAMEPADS;
	}
	for (int i = 0; i < MAX_GAMEPADS; i++) {
		r_states[i] = GamepadState();
	}
	for (int i = 0; i < count; i++) {
		GamepadReading reading = pads.GetAt(i).GetCurrentReading();
		GamepadState &s = r_states[i];
		s.connected = true;
		s.buttons = (uint32_t)reading.Buttons;
		s.left_x = (float)reading.LeftThumbstickX;
		s.left_y = (float)reading.LeftThumbstickY;
		s.right_x = (float)reading.RightThumbstickX;
		s.right_y = (float)reading.RightThumbstickY;
		s.left_trigger = (float)reading.LeftTrigger;
		s.right_trigger = (float)reading.RightTrigger;
	}
	return count;
}

const wchar_t *get_install_dir() {
	return g_install_dir.c_str();
}

const wchar_t *get_local_dir() {
	return g_local_dir.c_str();
}

} // namespace uwp
