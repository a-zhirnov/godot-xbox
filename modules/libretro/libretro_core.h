/**************************************************************************/
/*  libretro_core.h                                                       */
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

#include "core/io/image.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "scene/resources/image_texture.h"

#include "libretro.h"

class LibretroD3D11;

// Runs one libretro emulator core in-process: the frontend side of the libretro API.
// One core at a time (libretro callbacks are global). Drive it from a script every frame:
// load_core() -> load_game() -> run_frame() per frame -> get_texture() / drain_audio().
class LibretroCore : public RefCounted {
	GDCLASS(LibretroCore, RefCounted);

	static LibretroCore *active;

	void *library = nullptr;

	void (*retro_init)() = nullptr;
	void (*retro_deinit)() = nullptr;
	void (*retro_get_system_info)(retro_system_info *) = nullptr;
	void (*retro_get_system_av_info)(retro_system_av_info *) = nullptr;
	void (*retro_set_environment)(retro_environment_t) = nullptr;
	void (*retro_set_video_refresh)(retro_video_refresh_t) = nullptr;
	void (*retro_set_audio_sample)(retro_audio_sample_t) = nullptr;
	void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
	void (*retro_set_input_poll)(retro_input_poll_t) = nullptr;
	void (*retro_set_input_state)(retro_input_state_t) = nullptr;
	void (*retro_set_controller_port_device)(unsigned, unsigned) = nullptr;
	void (*retro_reset)() = nullptr;
	void (*retro_run)() = nullptr;
	bool (*retro_load_game)(const retro_game_info *) = nullptr;
	void (*retro_unload_game)() = nullptr;
	size_t (*retro_get_memory_size)(unsigned) = nullptr;
	void *(*retro_get_memory_data)(unsigned) = nullptr;
	size_t (*retro_serialize_size)() = nullptr;
	bool (*retro_serialize)(void *, size_t) = nullptr;
	bool (*retro_unserialize)(const void *, size_t) = nullptr;

	bool core_initialized = false;
	bool game_loaded = false;
	String core_name;
	String sram_path;

	CharString system_dir;
	CharString save_dir;
	CharString game_path;
	PackedByteArray game_data;

	// Core options: defaults from SET_VARIABLES, values set from the script win.
	HashMap<String, CharString> option_values;
	HashMap<String, CharString> option_overrides;
	String last_message;

	// Hardware rendering (PCSX2): the core draws through a D3D11 device of ours.
	retro_hw_render_callback hw_render = {};
	LibretroD3D11 *d3d11 = nullptr;
	bool hw_context_live = false;

	// video_refresh can come from the core's own GS thread: the frame is kept here and turned
	// into the texture on the main thread after retro_run().
	Mutex frame_mutex;
	bool frame_dirty = false;

	retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
	PackedByteArray frame_rgba;
	Ref<Image> frame_image;
	Ref<ImageTexture> texture;
	int frame_width = 0;
	int frame_height = 0;

	PackedVector2Array audio_frames;

	uint32_t joypad[2] = {};
	int16_t analog[2][4] = {};

	double fps = 60.0;
	double sample_rate = 44100.0;
	float aspect_ratio = 4.0f / 3.0f;

	static bool _environment(unsigned p_cmd, void *p_data);
	static void _video_refresh(const void *p_data, unsigned p_width, unsigned p_height, size_t p_pitch);
	static void _audio_sample(int16_t p_left, int16_t p_right);
	static size_t _audio_sample_batch(const int16_t *p_data, size_t p_frames);
	static void _input_poll();
	static int16_t _input_state(unsigned p_port, unsigned p_device, unsigned p_index, unsigned p_id);
	static void _log(enum retro_log_level p_level, const char *p_format, ...);

	void _apply_av_info(const retro_system_av_info &p_info);
	void _set_variables(const retro_variable *p_vars);
	void _upload_frame();
	void _load_sram();
	void _close();

protected:
	static void _bind_methods();

public:
	// Where the core finds its BIOS files and keeps its saves; cores look for BIOS files as soon as
	// they are loaded, so set these before load_core().
	void set_directories(const String &p_system_dir, const String &p_save_dir);
	Error load_core(const String &p_core_path);
	Error load_game(const String &p_game_path, const String &p_system_dir, const String &p_save_dir);
	void run_frame();
	void reset();
	void save_sram();
	// Save states: the core's whole machine state in one file.
	Error save_state(const String &p_path);
	Error load_state(const String &p_path);
	void unload();

	void set_option(const String &p_key, const String &p_value);
	String get_last_message() const { return last_message; }

	void set_joypad(int p_port, int p_mask);
	void set_analog(int p_port, int p_stick, float p_x, float p_y);

	Ref<ImageTexture> get_texture() const { return texture; }
	PackedVector2Array drain_audio();
	double get_fps() const { return fps; }
	double get_sample_rate() const { return sample_rate; }
	float get_aspect_ratio() const { return aspect_ratio; }
	String get_core_name() const { return core_name; }
	bool is_running() const { return game_loaded; }

	~LibretroCore();
};
