/**************************************************************************/
/*  display_server_uwp.cpp                                                */
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

#include "display_server_uwp.h"

#include "key_mapping_windows.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/os/os.h"
#include "scene/gui/control.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#include "servers/rendering/rendering_device.h"

#ifdef D3D12_ENABLED
#include "drivers/d3d12/rendering_context_driver_d3d12.h"
#endif

// Windows.Gaming.Input.GamepadButtons.
enum UwpGamepadButtons : uint32_t {
	UWP_GAMEPAD_MENU = 0x1,
	UWP_GAMEPAD_VIEW = 0x2,
	UWP_GAMEPAD_A = 0x4,
	UWP_GAMEPAD_B = 0x8,
	UWP_GAMEPAD_X = 0x10,
	UWP_GAMEPAD_Y = 0x20,
	UWP_GAMEPAD_DPAD_UP = 0x40,
	UWP_GAMEPAD_DPAD_DOWN = 0x80,
	UWP_GAMEPAD_DPAD_LEFT = 0x100,
	UWP_GAMEPAD_DPAD_RIGHT = 0x200,
	UWP_GAMEPAD_LEFT_SHOULDER = 0x400,
	UWP_GAMEPAD_RIGHT_SHOULDER = 0x800,
	UWP_GAMEPAD_LEFT_THUMBSTICK = 0x1000,
	UWP_GAMEPAD_RIGHT_THUMBSTICK = 0x2000,
};

static const struct {
	uint32_t uwp_button;
	JoyButton button;
} uwp_button_map[] = {
	// Face buttons are reported the way the Android build sees them on the AYN Thor (east/south
	// and north/west swapped), so the game's own "Xbox controller" option is the right one on Xbox.
	{ UWP_GAMEPAD_A, JoyButton::B },
	{ UWP_GAMEPAD_B, JoyButton::A },
	{ UWP_GAMEPAD_X, JoyButton::Y },
	{ UWP_GAMEPAD_Y, JoyButton::X },
	{ UWP_GAMEPAD_VIEW, JoyButton::BACK },
	{ UWP_GAMEPAD_MENU, JoyButton::START },
	{ UWP_GAMEPAD_LEFT_THUMBSTICK, JoyButton::LEFT_STICK },
	{ UWP_GAMEPAD_RIGHT_THUMBSTICK, JoyButton::RIGHT_STICK },
	{ UWP_GAMEPAD_LEFT_SHOULDER, JoyButton::LEFT_SHOULDER },
	{ UWP_GAMEPAD_RIGHT_SHOULDER, JoyButton::RIGHT_SHOULDER },
	{ UWP_GAMEPAD_DPAD_UP, JoyButton::DPAD_UP },
	{ UWP_GAMEPAD_DPAD_DOWN, JoyButton::DPAD_DOWN },
	{ UWP_GAMEPAD_DPAD_LEFT, JoyButton::DPAD_LEFT },
	{ UWP_GAMEPAD_DPAD_RIGHT, JoyButton::DPAD_RIGHT },
};

// Identity mapping: the events below already use the standard Godot layout.
static const char *UWP_GAMEPAD_GUID = "78626f78756e6976657273616c0000";
static const char *UWP_GAMEPAD_MAPPING = "78626f78756e6976657273616c0000,Xbox Controller,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,platform:UWP,";

Vector<String> DisplayServerUWP::get_rendering_drivers_func() {
	Vector<String> drivers;
#ifdef D3D12_ENABLED
	drivers.push_back("d3d12");
#endif
	return drivers;
}

DisplayServer *DisplayServerUWP::create_func(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	DisplayServer *ds = memnew(DisplayServerUWP(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, p_parent_window, r_error));
	if (r_error != OK) {
		OS::get_singleton()->alert("Your video card drivers seem not to support the required Direct3D 12 version.", "Unable to initialize video driver");
	}
	return ds;
}

void DisplayServerUWP::register_uwp_driver() {
	register_create_function("uwp", create_func, get_rendering_drivers_func);
}

void DisplayServerUWP::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	DisplayServerUWP *ds = static_cast<DisplayServerUWP *>(get_singleton());
	if (ds->input_event_callback.is_valid()) {
		ds->input_event_callback.call(p_event);
	}
}

Vector<DisplayServerEnums::WindowID> DisplayServerUWP::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> list;
	list.push_back(DisplayServerEnums::MAIN_WINDOW_ID);
	return list;
}

void DisplayServerUWP::window_set_vsync_mode(DisplayServerEnums::VSyncMode p_vsync_mode, DisplayServerEnums::WindowID p_window) {
	vsync_mode = p_vsync_mode;
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
}

void DisplayServerUWP::_send_window_event(DisplayServerEnums::WindowEvent p_event) {
	if (window_event_callback.is_valid()) {
		window_event_callback.call((int)p_event);
	}
}

void DisplayServerUWP::_handle_event(const uwp::Event &p_event) {
	Input *input = Input::get_singleton();

	switch (p_event.type) {
		case uwp::EventType::POINTER_DOWN:
		case uwp::EventType::POINTER_UP: {
			const bool pressed = p_event.type == uwp::EventType::POINTER_DOWN;
			MouseButton button = MouseButton::LEFT;
			if (p_event.a == 2) {
				button = MouseButton::RIGHT;
			} else if (p_event.a == 3) {
				button = MouseButton::MIDDLE;
			}
			mouse_position = Point2i((int)p_event.x, (int)p_event.y);
			if (pressed) {
				mouse_button_mask.set_flag(mouse_button_to_mask(button));
			} else {
				mouse_button_mask.clear_flag(mouse_button_to_mask(button));
			}

			Ref<InputEventMouseButton> mb;
			mb.instantiate();
			mb->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
			mb->set_pressed(pressed);
			mb->set_button_index(button);
			mb->set_button_mask(mouse_button_mask);
			mb->set_position(mouse_position);
			mb->set_global_position(mouse_position);
			input->parse_input_event(mb);
		} break;

		case uwp::EventType::POINTER_MOVE: {
			const Point2i new_position((int)p_event.x, (int)p_event.y);

			Ref<InputEventMouseMotion> mm;
			mm.instantiate();
			mm->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
			mm->set_button_mask(mouse_button_mask);
			mm->set_position(new_position);
			mm->set_global_position(new_position);
			mm->set_relative(new_position - mouse_position);
			mm->set_relative_screen_position(new_position - mouse_position);
			mouse_position = new_position;
			input->parse_input_event(mm);
		} break;

		case uwp::EventType::POINTER_WHEEL: {
			const MouseButton button = p_event.a > 0 ? MouseButton::WHEEL_UP : MouseButton::WHEEL_DOWN;
			for (int pressed = 1; pressed >= 0; pressed--) {
				Ref<InputEventMouseButton> mb;
				mb.instantiate();
				mb->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
				mb->set_pressed(pressed == 1);
				mb->set_button_index(button);
				mb->set_factor(Math::abs((float)p_event.a) / 120.0f);
				mb->set_position(mouse_position);
				mb->set_global_position(mouse_position);
				input->parse_input_event(mb);
			}
		} break;

		case uwp::EventType::KEY_DOWN:
		case uwp::EventType::KEY_UP: {
			const bool pressed = p_event.type == uwp::EventType::KEY_DOWN;
			const Key keycode = KeyMappingWindows::get_keysym((unsigned int)p_event.a);

			Ref<InputEventKey> k;
			k.instantiate();
			k->set_window_id(DisplayServerEnums::MAIN_WINDOW_ID);
			k->set_pressed(pressed);
			k->set_echo(pressed && p_event.c == 1);
			k->set_keycode(fix_keycode(0, keycode));
			k->set_physical_keycode(KeyMappingWindows::get_scansym((unsigned int)p_event.b, false));
			k->set_key_label(keycode);
			input->parse_input_event(k);
		} break;

		case uwp::EventType::CHARACTER:
			break;

		case uwp::EventType::FOCUS:
			focused = p_event.a != 0;
			print_verbose(vformat("UWP: window focus %s", focused ? "in" : "out"));
			_send_window_event(focused ? DisplayServerEnums::WINDOW_EVENT_FOCUS_IN : DisplayServerEnums::WINDOW_EVENT_FOCUS_OUT);
			break;

		case uwp::EventType::SIZE: {
			if (p_event.a <= 0 || p_event.b <= 0) {
				break;
			}
			window_size = Size2i(p_event.a, p_event.b);
			if (rendering_context) {
				rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, window_size.x, window_size.y);
			}
			if (rect_changed_callback.is_valid()) {
				rect_changed_callback.call(Rect2i(Point2i(), window_size));
			}
		} break;

		case uwp::EventType::CLOSE:
			_send_window_event(DisplayServerEnums::WINDOW_EVENT_CLOSE_REQUEST);
			break;
	}
}

// Diagnostics: which control holds UI focus after a button release (gamepad-only navigation).
void DisplayServerUWP::_log_focus_owner() {
	SceneTree *tree = SceneTree::get_singleton();
	if (!tree || !tree->get_root()) {
		return;
	}
	Control *focus = tree->get_root()->gui_get_focus_owner();
	print_verbose(vformat("UWP: focus -> %s", focus ? String(focus->get_path()) + " (" + focus->get_class() + ")" : String("<none>")));
}

void DisplayServerUWP::_process_gamepads() {
	Input *input = Input::get_singleton();

	uwp::GamepadState current[uwp::MAX_GAMEPADS];
	uwp::poll_gamepads(current);

	for (int i = 0; i < uwp::MAX_GAMEPADS; i++) {
		const uwp::GamepadState &prev = gamepads[i];
		const uwp::GamepadState &cur = current[i];

		if (cur.connected != prev.connected) {
			if (cur.connected) {
				print_verbose(vformat("UWP: gamepad %d connected", i));
				input->joy_connection_changed(i, true, "Xbox Controller", UWP_GAMEPAD_GUID);
			} else {
				input->joy_connection_changed(i, false, "");
			}
		}
		if (!cur.connected) {
			gamepads[i] = cur;
			continue;
		}

		for (const auto &m : uwp_button_map) {
			const bool was = (prev.buttons & m.uwp_button) != 0 && prev.connected;
			const bool is = (cur.buttons & m.uwp_button) != 0;
			if (was != is || !prev.connected) {
				if (was != is) {
					print_verbose(vformat("UWP: gamepad %d button %d %s", i, (int)m.button, is ? "down" : "up"));
					if (!is) {
						_log_focus_owner();
					}
				}
				input->joy_button(i, m.button, is);
			}
		}

		// Windows.Gaming.Input reports +1 as "up" on Y axes; Godot expects +1 as "down".
		// Sticks rest a little off zero: apply a dead zone and only send real changes, otherwise the
		// constant noise makes the game think the stick is always moving.
		const float values[6] = { cur.left_x, -cur.left_y, cur.right_x, -cur.right_y, cur.left_trigger, cur.right_trigger };
		const JoyAxis axes[6] = { JoyAxis::LEFT_X, JoyAxis::LEFT_Y, JoyAxis::RIGHT_X, JoyAxis::RIGHT_Y, JoyAxis::TRIGGER_LEFT, JoyAxis::TRIGGER_RIGHT };
		for (int a = 0; a < 6; a++) {
			float v = values[a];
			if (Math::abs(v) < (a < 4 ? 0.15f : 0.05f)) {
				v = 0.0f;
			}
			if (!prev.connected || Math::abs(v - last_axis[i][a]) > 0.01f) {
				last_axis[i][a] = v;
				input->joy_axis(i, axes[a], v);
			}
		}

		gamepads[i] = cur;
	}
}

void DisplayServerUWP::process_events() {
	uwp::process_events();

	uwp::Event event;
	while (uwp::poll_event(event)) {
		_handle_event(event);
	}

	_process_gamepads();

	Input::get_singleton()->flush_buffered_events();
}

DisplayServerUWP::DisplayServerUWP(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	r_error = OK;
	rendering_driver = p_rendering_driver;

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);
	Input::get_singleton()->add_joy_mapping(UWP_GAMEPAD_MAPPING, true);

	int width = 0;
	int height = 0;
	uwp::get_window_size(width, height);
	window_size = Size2i(width, height);

#if defined(RD_ENABLED) && defined(D3D12_ENABLED)
	rendering_context = memnew(RenderingContextDriverD3D12);
	if (rendering_context->initialize() != OK) {
		memdelete(rendering_context);
		rendering_context = nullptr;
		r_error = ERR_UNAVAILABLE;
		ERR_FAIL_MSG("Could not initialize Direct3D 12.");
	}

	RenderingContextDriverD3D12::WindowPlatformData wpd;
	wpd.core_window = (IUnknown *)uwp::core_window();
	if (rendering_context->window_create(DisplayServerEnums::MAIN_WINDOW_ID, &wpd) != OK) {
		memdelete(rendering_context);
		rendering_context = nullptr;
		r_error = ERR_UNAVAILABLE;
		ERR_FAIL_MSG("Failed to create the Direct3D 12 window surface.");
	}
	rendering_context->window_set_size(DisplayServerEnums::MAIN_WINDOW_ID, window_size.x, window_size.y);

	rendering_device = memnew(RenderingDevice);
	if (rendering_device->initialize(rendering_context, DisplayServerEnums::MAIN_WINDOW_ID) != OK) {
		memdelete(rendering_device);
		rendering_device = nullptr;
		rendering_context->window_destroy(DisplayServerEnums::MAIN_WINDOW_ID);
		memdelete(rendering_context);
		rendering_context = nullptr;
		r_error = ERR_UNAVAILABLE;
		ERR_FAIL_MSG("Failed to initialize the Direct3D 12 rendering device.");
	}

	rendering_driver = "d3d12";
	OS::get_singleton()->set_current_rendering_driver_name(rendering_driver, OS::get_singleton()->get_current_rendering_driver_name_source());

	window_set_vsync_mode(p_vsync_mode, DisplayServerEnums::MAIN_WINDOW_ID);

	rendering_device->screen_create(DisplayServerEnums::MAIN_WINDOW_ID);
	RendererCompositorRD::make_current();
#else
	r_error = ERR_UNAVAILABLE;
	ERR_FAIL_MSG("This build has no Direct3D 12 support.");
#endif
}

DisplayServerUWP::~DisplayServerUWP() {
	if (rendering_device) {
		rendering_device->screen_free(DisplayServerEnums::MAIN_WINDOW_ID);
	}
	if (rendering_context) {
		rendering_context->window_destroy(DisplayServerEnums::MAIN_WINDOW_ID);
	}
	if (rendering_device) {
		memdelete(rendering_device);
		rendering_device = nullptr;
	}
	if (rendering_context) {
		memdelete(rendering_context);
		rendering_context = nullptr;
	}
}
