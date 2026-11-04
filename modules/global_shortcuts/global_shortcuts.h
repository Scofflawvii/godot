/**************************************************************************/
/*  global_shortcuts.h                                                    */
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

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/typed_array.h"

// System-wide keyboard shortcuts an application asks the desktop for, which
// work while another window has focus. On Linux this is the XDG desktop
// portal's GlobalShortcuts interface (KDE, GNOME 48+, Hyprland...): the
// desktop may show the user the suggested keys to approve, and the user can
// change them in the desktop's settings later. Elsewhere it's unsupported.
class GlobalShortcuts : public Object {
	GDCLASS(GlobalShortcuts, Object);

	static GlobalShortcuts *singleton;

	Mutex mutex;
	String status = "unbound";
	TypedArray<Dictionary> bindings;

#ifdef DBUS_ENABLED
	Thread thread;
	SafeFlag thread_abort;
	String app_id;
	TypedArray<Dictionary> requested;
	static void _thread_func(void *p_ud);
	void _run();
#endif

	void _set_status(const String &p_status);
	void _emit_bound(const TypedArray<Dictionary> &p_bindings);
	void _emit_activated(const String &p_id);
	void _emit_failed(const String &p_message);

protected:
	static void _bind_methods();

public:
	static GlobalShortcuts *get_singleton() { return singleton; }

	bool is_supported() const;
	Error bind_shortcuts(const String &p_app_id, const TypedArray<Dictionary> &p_shortcuts);
	void unbind_shortcuts();
	TypedArray<Dictionary> get_bindings();
	String get_status();

	GlobalShortcuts();
	~GlobalShortcuts();
};
