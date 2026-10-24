/**************************************************************************/
/*  libretro_core.cpp                                                     */
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

#include "libretro_core.h"

#include "libretro_d3d11.h"
#include "libretro_d3d12.h"

#include "core/io/dir_access.h"
#include "core/object/class_db.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include <cstdarg>
#include <cstdio>

LibretroCore *LibretroCore::active = nullptr;

#define LOAD_SYMBOL(m_name)                                                                                                      \
	{                                                                                                                            \
		void *symbol = nullptr;                                                                                                  \
		if (OS::get_singleton()->get_dynamic_library_symbol_handle(library, #m_name, symbol) != OK) {                          \
			_close();                                                                                                            \
			ERR_FAIL_V_MSG(ERR_CANT_RESOLVE, vformat("libretro core %s does not export %s.", p_core_path, String(#m_name))); \
		}                                                                                                                        \
		m_name = reinterpret_cast<decltype(m_name)>(symbol);                                                                     \
	}

Error LibretroCore::load_core(const String &p_core_path) {
	ERR_FAIL_COND_V_MSG(active != nullptr && active != this, ERR_BUSY, "Only one libretro core can run at a time.");
	unload();

	Error err = OS::get_singleton()->open_dynamic_library(p_core_path, library);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Can't load libretro core %s.", p_core_path));
	libretro_redirect_file_api(library);

	LOAD_SYMBOL(retro_init);
	LOAD_SYMBOL(retro_deinit);
	LOAD_SYMBOL(retro_get_system_info);
	LOAD_SYMBOL(retro_get_system_av_info);
	LOAD_SYMBOL(retro_set_environment);
	LOAD_SYMBOL(retro_set_video_refresh);
	LOAD_SYMBOL(retro_set_audio_sample);
	LOAD_SYMBOL(retro_set_audio_sample_batch);
	LOAD_SYMBOL(retro_set_input_poll);
	LOAD_SYMBOL(retro_set_input_state);
	LOAD_SYMBOL(retro_set_controller_port_device);
	LOAD_SYMBOL(retro_reset);
	LOAD_SYMBOL(retro_run);
	LOAD_SYMBOL(retro_load_game);
	LOAD_SYMBOL(retro_unload_game);
	LOAD_SYMBOL(retro_get_memory_size);
	LOAD_SYMBOL(retro_get_memory_data);
	LOAD_SYMBOL(retro_serialize_size);
	LOAD_SYMBOL(retro_serialize);
	LOAD_SYMBOL(retro_unserialize);

	active = this;
	retro_set_environment(_environment);
	retro_set_video_refresh(_video_refresh);
	retro_set_audio_sample(_audio_sample);
	retro_set_audio_sample_batch(_audio_sample_batch);
	retro_set_input_poll(_input_poll);
	retro_set_input_state(_input_state);
	retro_init();
	core_initialized = true;

	retro_system_info info = {};
	retro_get_system_info(&info);
	core_name = String::utf8(info.library_name ? info.library_name : "") + " " + String::utf8(info.library_version ? info.library_version : "");
	print_line(vformat("libretro: loaded core %s", core_name));
	return OK;
}

void LibretroCore::set_directories(const String &p_system_dir, const String &p_save_dir) {
	system_dir = p_system_dir.utf8();
	save_dir = p_save_dir.utf8();
	DirAccess::make_dir_recursive_absolute(p_save_dir);
}

Error LibretroCore::load_game(const String &p_game_path, const String &p_system_dir, const String &p_save_dir) {
	ERR_FAIL_COND_V_MSG(!core_initialized, ERR_UNCONFIGURED, "Load a libretro core first.");
	ERR_FAIL_COND_V_MSG(game_loaded, ERR_ALREADY_IN_USE, "A game is already loaded.");

	set_directories(p_system_dir, p_save_dir);
	game_path = p_game_path.utf8();

	retro_system_info info = {};
	retro_get_system_info(&info);

	retro_game_info game = {};
	game.path = game_path.get_data();
	if (!info.need_fullpath) {
		// Read the ROM through the engine: on Xbox only the engine's brokered file access reaches USB drives.
		Error read_err = OK;
		game_data = FileAccess::get_file_as_bytes(p_game_path, &read_err);
		ERR_FAIL_COND_V_MSG(read_err != OK, read_err, vformat("Can't read game %s.", p_game_path));
		game.data = game_data.ptr();
		game.size = game_data.size();
	}

	ERR_FAIL_COND_V_MSG(!retro_load_game(&game), ERR_FILE_UNRECOGNIZED, vformat("%s refused to load %s.", core_name, p_game_path));
	game_loaded = true;
	if ((d3d11 || d3d12) && hw_render.context_reset) {
		// The context is ready once the game is loaded, as in RetroArch: the core opens its renderer here.
		hw_context_live = true;
		hw_render.context_reset();
	}

	retro_system_av_info av = {};
	retro_get_system_av_info(&av);
	_apply_av_info(av);

	retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	sram_path = p_save_dir.path_join(p_game_path.get_file().get_basename() + ".srm");
	_load_sram();
	print_line(vformat("libretro: running %s at %.2f fps, %d Hz", p_game_path.get_file(), fps, (int)sample_rate));
	return OK;
}

void LibretroCore::_apply_av_info(const retro_system_av_info &p_info) {
	fps = p_info.timing.fps > 0 ? p_info.timing.fps : 60.0;
	sample_rate = p_info.timing.sample_rate > 0 ? p_info.timing.sample_rate : 44100.0;
	const retro_game_geometry &g = p_info.geometry;
	aspect_ratio = g.aspect_ratio > 0 ? g.aspect_ratio : (g.base_height ? (float)g.base_width / (float)g.base_height : 4.0f / 3.0f);
}

void LibretroCore::_load_sram() {
	const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	if (size == 0 || !data || !FileAccess::exists(sram_path)) {
		return;
	}
	PackedByteArray saved = FileAccess::get_file_as_bytes(sram_path);
	memcpy(data, saved.ptr(), MIN((size_t)saved.size(), size));
}

void LibretroCore::save_sram() {
	MutexLock lock(run_mutex);
	if (!game_loaded) {
		return;
	}
	const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	const uint8_t *data = (const uint8_t *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	if (size == 0 || !data) {
		return;
	}
	Ref<FileAccess> f = FileAccess::open(sram_path, FileAccess::WRITE);
	ERR_FAIL_COND_MSG(f.is_null(), vformat("Can't write save %s.", sram_path));
	f->store_buffer(data, size);
}

Error LibretroCore::save_state(const String &p_path) {
	MutexLock lock(run_mutex);
	ERR_FAIL_COND_V(!game_loaded, ERR_UNCONFIGURED);
	const size_t size = retro_serialize_size();
	ERR_FAIL_COND_V_MSG(size == 0, ERR_UNAVAILABLE, vformat("%s has no save states.", core_name));
	PackedByteArray data;
	data.resize(size);
	ERR_FAIL_COND_V_MSG(!retro_serialize(data.ptrw(), size), FAILED, "The core could not save its state.");
	DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	// Written next to the old one and swapped in, so a crash mid-write never loses the last state.
	const String temp = p_path + ".tmp";
	{
		Ref<FileAccess> f = FileAccess::open(temp, FileAccess::WRITE);
		ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, vformat("Can't write %s.", temp));
		f->store_buffer(data.ptr(), size);
	}
	if (FileAccess::exists(p_path)) {
		DirAccess::remove_absolute(p_path);
	}
	return DirAccess::rename_absolute(temp, p_path);
}

Error LibretroCore::load_state(const String &p_path) {
	MutexLock lock(run_mutex);
	ERR_FAIL_COND_V(!game_loaded, ERR_UNCONFIGURED);
	Error err = OK;
	PackedByteArray data = FileAccess::get_file_as_bytes(p_path, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Can't read %s.", p_path));
	ERR_FAIL_COND_V_MSG(!retro_unserialize(data.ptr(), data.size()), FAILED, "The core refused the saved state.");
	return OK;
}

void LibretroCore::run_frame() {
	if (game_loaded) {
		{
			MutexLock lock(run_mutex);
			_run_once();
		}
		present();
	}
}

void LibretroCore::_run_once() {
	if (!game_loaded) {
		return;
	}
	active = this;
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	retro_run();
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	frames_run.increment();
	perf_run_usec += now - started;
	perf_frames++;
	if (perf_since == 0) {
		perf_since = now;
	} else if (now - perf_since >= 5000000) {
		const double frames = (double)MAX<uint64_t>(1, perf_frames);
		print_line(vformat("libretro perf: %.1f frames/s, core %.1f ms, GPU readback %.1f ms, texture upload %.1f ms per frame",
				perf_frames / ((now - perf_since) / 1000000.0), (perf_run_usec - perf_readback_usec) / 1000.0 / frames, perf_readback_usec / 1000.0 / frames, perf_upload_usec / 1000.0 / frames));
		perf_run_usec = perf_readback_usec = perf_upload_usec = perf_frames = 0;
		perf_since = now;
	}
}

void LibretroCore::present() {
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	_upload_frame();
	perf_upload_usec += OS::get_singleton()->get_ticks_usec() - started;
}

void LibretroCore::_run_thread(void *p_self) {
	LibretroCore *self = (LibretroCore *)p_self;
	const uint64_t period = (uint64_t)(1000000.0 / MAX(1.0, self->fps));
	uint64_t next = OS::get_singleton()->get_ticks_usec();
	while (!self->run_thread_quit.is_set()) {
		if (self->run_paused.is_set()) {
			OS::get_singleton()->delay_usec(5000);
			next = OS::get_singleton()->get_ticks_usec();
			continue;
		}
		{
			MutexLock lock(self->run_mutex);
			self->_run_once();
		}
		next += period;
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		if (now < next) {
			OS::get_singleton()->delay_usec(next - now);
		} else if (now - next > period * 4) {
			// Far behind (a load, a stall): carry on from here rather than race to catch up.
			next = now;
		}
	}
}

void LibretroCore::start_thread() {
	if (!game_loaded || run_thread.is_started()) {
		return;
	}
	run_thread_quit.clear();
	Thread::Settings settings;
	settings.priority = Thread::PRIORITY_HIGH;
	run_thread.start(&LibretroCore::_run_thread, this, settings);
}

void LibretroCore::stop_thread() {
	if (run_thread.is_started()) {
		run_thread_quit.set();
		run_thread.wait_to_finish();
	}
}

void LibretroCore::set_paused(bool p_paused) {
	if (p_paused) {
		run_paused.set();
	} else {
		run_paused.clear();
	}
}

void LibretroCore::_upload_frame() {
	MutexLock lock(frame_mutex);
	if (!frame_dirty || frame_width == 0 || frame_height == 0) {
		return;
	}
	frame_dirty = false;
	if (frame_image.is_null()) {
		frame_image.instantiate();
	}
	frame_image->set_data(frame_width, frame_height, false, Image::FORMAT_RGBA8, frame_rgba);
	if (texture.is_valid() && (texture->get_width() != frame_width || texture->get_height() != frame_height)) {
		texture.unref();
	}
	if (texture.is_null()) {
		texture = ImageTexture::create_from_image(frame_image);
	} else {
		texture->update(frame_image);
	}
}

void LibretroCore::set_option(const String &p_key, const String &p_value) {
	option_overrides[p_key] = p_value.utf8();
}

void LibretroCore::_set_variables(const retro_variable *p_vars) {
	// "Description; first|second|third": the first value is the default.
	for (const retro_variable *v = p_vars; v && v->key; v++) {
		const String value = String::utf8(v->value ? v->value : "");
		const int sep = value.find("; ");
		const String choices = sep >= 0 ? value.substr(sep + 2) : value;
		option_values[String::utf8(v->key)] = choices.get_slice("|", 0).utf8();
	}
}

void LibretroCore::reset() {
	MutexLock lock(run_mutex);
	if (game_loaded) {
		retro_reset();
	}
}

void LibretroCore::set_joypad(int p_port, int p_mask) {
	ERR_FAIL_INDEX(p_port, 2);
	joypad[p_port] = (uint32_t)p_mask;
}

void LibretroCore::set_analog(int p_port, int p_stick, float p_x, float p_y) {
	ERR_FAIL_INDEX(p_port, 2);
	ERR_FAIL_INDEX(p_stick, 2);
	analog[p_port][p_stick * 2] = (int16_t)CLAMP(p_x * 32767.0f, -32768.0f, 32767.0f);
	analog[p_port][p_stick * 2 + 1] = (int16_t)CLAMP(p_y * 32767.0f, -32768.0f, 32767.0f);
}

PackedVector2Array LibretroCore::drain_audio() {
	MutexLock lock(audio_mutex);
	PackedVector2Array out = audio_frames;
	audio_frames.clear();
	return out;
}

void LibretroCore::_close() {
	if (core_initialized && retro_deinit) {
		retro_deinit();
	}
	core_initialized = false;
	if (library) {
		OS::get_singleton()->close_dynamic_library(library);
		library = nullptr;
	}
	if (active == this) {
		active = nullptr;
	}
}

void LibretroCore::unload() {
	stop_thread();
	MutexLock lock(run_mutex);
	if (game_loaded) {
		save_sram();
		if (hw_context_live && hw_render.context_destroy) {
			hw_render.context_destroy();
		}
		hw_context_live = false;
		retro_unload_game();
		game_loaded = false;
	}
	_close();
	if (d3d11) {
		delete d3d11;
		d3d11 = nullptr;
	}
	if (d3d12) {
		delete d3d12;
		d3d12 = nullptr;
	}
	hw_render = {};
	option_values.clear();
	game_data.clear();
	texture.unref();
	frame_image.unref();
	audio_frames.clear();
}

LibretroCore::~LibretroCore() {
	unload();
}

// --- libretro callbacks ---------------------------------------------------------------------------

bool LibretroCore::_environment(unsigned p_cmd, void *p_data) {
	LibretroCore *self = active;
	if (!self) {
		return false;
	}
	switch (p_cmd) {
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
			const retro_pixel_format format = *(const retro_pixel_format *)p_data;
			if (format != RETRO_PIXEL_FORMAT_0RGB1555 && format != RETRO_PIXEL_FORMAT_XRGB8888 && format != RETRO_PIXEL_FORMAT_RGB565) {
				return false;
			}
			self->pixel_format = format;
			return true;
		}
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
			*(const char **)p_data = self->system_dir.get_data();
			return true;
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
			*(const char **)p_data = self->save_dir.get_data();
			return true;
		case RETRO_ENVIRONMENT_GET_CAN_DUPE:
			*(bool *)p_data = true;
			return true;
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
			((retro_log_callback *)p_data)->log = _log;
			return true;
		case RETRO_ENVIRONMENT_GET_LANGUAGE:
			*(unsigned *)p_data = RETRO_LANGUAGE_ENGLISH;
			return true;
		case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
			return true;
		case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
			*(unsigned *)p_data = 0; // Cores fall back to SET_VARIABLES, and their defaults.
			return true;
		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
			*(bool *)p_data = false;
			return true;
		case RETRO_ENVIRONMENT_SET_GEOMETRY: {
			const retro_game_geometry *g = (const retro_game_geometry *)p_data;
			self->aspect_ratio = g->aspect_ratio > 0 ? g->aspect_ratio : (g->base_height ? (float)g->base_width / (float)g->base_height : self->aspect_ratio);
			return true;
		}
		case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
			self->_apply_av_info(*(const retro_system_av_info *)p_data);
			return true;
		case RETRO_ENVIRONMENT_SET_VARIABLES:
			self->_set_variables((const retro_variable *)p_data);
			return true;
		case RETRO_ENVIRONMENT_GET_VARIABLE: {
			retro_variable *var = (retro_variable *)p_data;
			const String key = String::utf8(var->key ? var->key : "");
			if (const CharString *value = self->option_overrides.getptr(key)) {
				var->value = value->get_data();
				return true;
			}
			if (const CharString *value = self->option_values.getptr(key)) {
				var->value = value->get_data();
				return true;
			}
			var->value = nullptr;
			return false;
		}
		case RETRO_ENVIRONMENT_SET_MESSAGE:
		case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
			// Both structs start with the text.
			const char *msg = *(const char *const *)p_data;
			if (msg) {
				self->last_message = String::utf8(msg);
				print_line("[core message] " + self->last_message);
			}
			return true;
		}
		case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
#ifdef WINDOWS_ENABLED
			// D3D12: on Xbox D3D11 runs through d3d11on12, a layer PCSX2's draw calls can't afford.
			*(retro_hw_context_type *)p_data = RETRO_HW_CONTEXT_D3D12;
			return true;
#else
			return false;
#endif
		case RETRO_ENVIRONMENT_SET_HW_RENDER: {
			retro_hw_render_callback *cb = (retro_hw_render_callback *)p_data;
			if (cb->context_type == RETRO_HW_CONTEXT_NONE) {
				return true;
			}
			if (cb->context_type == RETRO_HW_CONTEXT_D3D12) {
				if (!self->d3d12) {
					self->d3d12 = LibretroD3D12::create();
				}
				if (!self->d3d12) {
					return false;
				}
			} else if (cb->context_type == RETRO_HW_CONTEXT_D3D11) {
				if (!self->d3d11) {
					self->d3d11 = LibretroD3D11::create();
				}
				if (!self->d3d11) {
					return false;
				}
			} else {
				return false;
			}
			self->hw_render = *cb;
			return true;
		}
		case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
			if (self->d3d12 && self->hw_render.context_type == RETRO_HW_CONTEXT_D3D12) {
				*(const void **)p_data = self->d3d12->get_interface();
				return true;
			}
			if (!self->d3d11) {
				return false;
			}
			*(const void **)p_data = self->d3d11->get_interface();
			return true;
		case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
		case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
		case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
		case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
		case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
			return true;
		default:
			return false;
	}
}

void LibretroCore::_video_refresh(const void *p_data, unsigned p_width, unsigned p_height, size_t p_pitch) {
	LibretroCore *self = active;
	if (!self || !p_data || p_width == 0 || p_height == 0) {
		return; // nullptr means "same frame as before" (GET_CAN_DUPE).
	}
	MutexLock lock(self->frame_mutex);
	if (p_data == RETRO_HW_FRAME_BUFFER_VALID) {
		unsigned width = 0, height = 0;
		const uint64_t started = OS::get_singleton()->get_ticks_usec();
		const bool read = self->d3d12 ? self->d3d12->read_frame(p_width, p_height, self->frame_rgba, width, height)
										: self->d3d11 && self->d3d11->read_frame(p_width, p_height, self->frame_rgba, width, height);
		self->perf_readback_usec += OS::get_singleton()->get_ticks_usec() - started;
		if (read) {
			self->frame_width = width;
			self->frame_height = height;
			self->frame_dirty = true;
		}
		return;
	}
	if ((int)p_width != self->frame_width || (int)p_height != self->frame_height) {
		self->frame_width = p_width;
		self->frame_height = p_height;
		self->frame_rgba.resize(p_width * p_height * 4);
	}
	uint8_t *dst = self->frame_rgba.ptrw();
	const uint8_t *src = (const uint8_t *)p_data;
	for (unsigned y = 0; y < p_height; y++) {
		const uint8_t *row = src + y * p_pitch;
		uint8_t *out = dst + y * p_width * 4;
		switch (self->pixel_format) {
			case RETRO_PIXEL_FORMAT_XRGB8888: {
				const uint32_t *px = (const uint32_t *)row;
				for (unsigned x = 0; x < p_width; x++) {
					const uint32_t c = px[x];
					out[x * 4 + 0] = (c >> 16) & 0xFF;
					out[x * 4 + 1] = (c >> 8) & 0xFF;
					out[x * 4 + 2] = c & 0xFF;
					out[x * 4 + 3] = 0xFF;
				}
			} break;
			case RETRO_PIXEL_FORMAT_RGB565: {
				const uint16_t *px = (const uint16_t *)row;
				for (unsigned x = 0; x < p_width; x++) {
					const uint16_t c = px[x];
					out[x * 4 + 0] = ((c >> 11) & 0x1F) * 255 / 31;
					out[x * 4 + 1] = ((c >> 5) & 0x3F) * 255 / 63;
					out[x * 4 + 2] = (c & 0x1F) * 255 / 31;
					out[x * 4 + 3] = 0xFF;
				}
			} break;
			default: { // 0RGB1555
				const uint16_t *px = (const uint16_t *)row;
				for (unsigned x = 0; x < p_width; x++) {
					const uint16_t c = px[x];
					out[x * 4 + 0] = ((c >> 10) & 0x1F) * 255 / 31;
					out[x * 4 + 1] = ((c >> 5) & 0x1F) * 255 / 31;
					out[x * 4 + 2] = (c & 0x1F) * 255 / 31;
					out[x * 4 + 3] = 0xFF;
				}
			} break;
		}
	}
	self->frame_dirty = true;
}

void LibretroCore::_audio_sample(int16_t p_left, int16_t p_right) {
	if (active) {
		MutexLock lock(active->audio_mutex);
		active->audio_frames.push_back(Vector2(p_left / 32768.0f, p_right / 32768.0f));
	}
}

size_t LibretroCore::_audio_sample_batch(const int16_t *p_data, size_t p_frames) {
	LibretroCore *self = active;
	if (!self) {
		return p_frames;
	}
	MutexLock lock(self->audio_mutex);
	const int start = self->audio_frames.size();
	self->audio_frames.resize(start + p_frames);
	Vector2 *out = self->audio_frames.ptrw() + start;
	for (size_t i = 0; i < p_frames; i++) {
		out[i] = Vector2(p_data[i * 2] / 32768.0f, p_data[i * 2 + 1] / 32768.0f);
	}
	return p_frames;
}

void LibretroCore::_input_poll() {
	// Input is pushed by the script with set_joypad()/set_analog() before every run_frame().
}

int16_t LibretroCore::_input_state(unsigned p_port, unsigned p_device, unsigned p_index, unsigned p_id) {
	LibretroCore *self = active;
	if (!self || p_port >= 2) {
		return 0;
	}
	switch (p_device & RETRO_DEVICE_MASK) {
		case RETRO_DEVICE_JOYPAD:
			if (p_id == RETRO_DEVICE_ID_JOYPAD_MASK) {
				return (int16_t)self->joypad[p_port];
			}
			return (self->joypad[p_port] >> p_id) & 1;
		case RETRO_DEVICE_ANALOG:
			if (p_index < 2 && p_id < 2) {
				return self->analog[p_port][p_index * 2 + p_id];
			}
			return 0;
		default:
			return 0;
	}
}

void LibretroCore::_log(enum retro_log_level p_level, const char *p_format, ...) {
	char buffer[1024];
	va_list args;
	va_start(args, p_format);
	vsnprintf(buffer, sizeof(buffer), p_format, args);
	va_end(args);
	const String message = String::utf8(buffer).strip_edges();
	if (p_level >= RETRO_LOG_ERROR) {
		ERR_PRINT("[core] " + message);
	} else if (p_level >= RETRO_LOG_INFO) {
		print_line("[core] " + message);
	}
}

void LibretroCore::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_directories", "system_dir", "save_dir"), &LibretroCore::set_directories);
	ClassDB::bind_method(D_METHOD("load_core", "core_path"), &LibretroCore::load_core);
	ClassDB::bind_method(D_METHOD("load_game", "game_path", "system_dir", "save_dir"), &LibretroCore::load_game);
	ClassDB::bind_method(D_METHOD("run_frame"), &LibretroCore::run_frame);
	ClassDB::bind_method(D_METHOD("start_thread"), &LibretroCore::start_thread);
	ClassDB::bind_method(D_METHOD("stop_thread"), &LibretroCore::stop_thread);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &LibretroCore::set_paused);
	ClassDB::bind_method(D_METHOD("present"), &LibretroCore::present);
	ClassDB::bind_method(D_METHOD("get_frames_run"), &LibretroCore::get_frames_run);
	ClassDB::bind_method(D_METHOD("reset"), &LibretroCore::reset);
	ClassDB::bind_method(D_METHOD("save_sram"), &LibretroCore::save_sram);
	ClassDB::bind_method(D_METHOD("save_state", "path"), &LibretroCore::save_state);
	ClassDB::bind_method(D_METHOD("load_state", "path"), &LibretroCore::load_state);
	ClassDB::bind_method(D_METHOD("unload"), &LibretroCore::unload);
	ClassDB::bind_method(D_METHOD("set_option", "key", "value"), &LibretroCore::set_option);
	ClassDB::bind_method(D_METHOD("get_last_message"), &LibretroCore::get_last_message);
	ClassDB::bind_method(D_METHOD("set_joypad", "port", "mask"), &LibretroCore::set_joypad);
	ClassDB::bind_method(D_METHOD("set_analog", "port", "stick", "x", "y"), &LibretroCore::set_analog);
	ClassDB::bind_method(D_METHOD("get_texture"), &LibretroCore::get_texture);
	ClassDB::bind_method(D_METHOD("drain_audio"), &LibretroCore::drain_audio);
	ClassDB::bind_method(D_METHOD("get_fps"), &LibretroCore::get_fps);
	ClassDB::bind_method(D_METHOD("get_sample_rate"), &LibretroCore::get_sample_rate);
	ClassDB::bind_method(D_METHOD("get_aspect_ratio"), &LibretroCore::get_aspect_ratio);
	ClassDB::bind_method(D_METHOD("get_core_name"), &LibretroCore::get_core_name);
	ClassDB::bind_method(D_METHOD("is_running"), &LibretroCore::is_running);
}
