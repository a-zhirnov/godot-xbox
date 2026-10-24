/**************************************************************************/
/*  xbox_apps.cpp                                                         */
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


#include "xbox_apps.h"

#include "core/object/class_db.h"

#ifdef UWP_ENABLED
#include "platform/windows/uwp_bridge.h"

#include <algorithm>
#include <cwchar>
#endif

TypedArray<Dictionary> XboxApps::list() {
	TypedArray<Dictionary> result;
#ifdef UWP_ENABLED
	std::vector<uwp::AppEntry> apps = uwp::list_apps();
	std::sort(apps.begin(), apps.end(), [](const uwp::AppEntry &a, const uwp::AppEntry &b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
	for (const uwp::AppEntry &app : apps) {
		Dictionary d;
		d["id"] = String::utf16((const char16_t *)app.id.c_str());
		d["name"] = String::utf16((const char16_t *)app.name.c_str());
		PackedByteArray icon;
		icon.resize(app.logo.size());
		if (!app.logo.empty()) {
			memcpy(icon.ptrw(), app.logo.data(), app.logo.size());
		}
		d["icon"] = icon;
		result.push_back(d);
	}
#endif
	return result;
}

bool XboxApps::launch(const String &p_id) {
#ifdef UWP_ENABLED
	return uwp::launch_app((const wchar_t *)p_id.utf16().get_data());
#else
	return false;
#endif
}

void XboxApps::_bind_methods() {
	ClassDB::bind_method(D_METHOD("list"), &XboxApps::list);
	ClassDB::bind_method(D_METHOD("launch", "id"), &XboxApps::launch);
}
