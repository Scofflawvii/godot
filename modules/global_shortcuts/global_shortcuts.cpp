/**************************************************************************/
/*  global_shortcuts.cpp                                                  */
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

#include "global_shortcuts.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

#ifdef DBUS_ENABLED
#include "core/crypto/crypto_core.h"

#ifdef SOWRAP_ENABLED
#include "platform/linuxbsd/dbus-so_wrap.h"
#else
#include <dbus/dbus.h>
#endif

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_SHORTCUTS "org.freedesktop.portal.GlobalShortcuts"
#define PORTAL_REQUEST "org.freedesktop.portal.Request"
#define PORTAL_SESSION "org.freedesktop.portal.Session"
#define HOST_REGISTRY "org.freedesktop.host.portal.Registry"

// The dlopen() wrapper is normally loaded by the display server; a headless
// run has none, so load it here too (loading it twice is harmless).
static bool _dbus_loaded() {
#ifdef SOWRAP_ENABLED
	static int result = -1;
	if (result == -1) {
		result = initialize_dbus(0);
	}
	return result == 0;
#else
	return true;
#endif
}

// D-Bus values as Variants: containers become Arrays and Dictionaries,
// variants are unwrapped, object paths and signatures become Strings.
static Variant _read(DBusMessageIter *p_iter) {
	switch (dbus_message_iter_get_arg_type(p_iter)) {
		case DBUS_TYPE_STRING:
		case DBUS_TYPE_OBJECT_PATH:
		case DBUS_TYPE_SIGNATURE: {
			const char *value = nullptr;
			dbus_message_iter_get_basic(p_iter, &value);
			return String::utf8(value);
		}
		case DBUS_TYPE_BOOLEAN: {
			dbus_bool_t value = false;
			dbus_message_iter_get_basic(p_iter, &value);
			return bool(value);
		}
		case DBUS_TYPE_BYTE: {
			unsigned char value = 0;
			dbus_message_iter_get_basic(p_iter, &value);
			return int64_t(value);
		}
		case DBUS_TYPE_INT32: {
			dbus_int32_t value = 0;
			dbus_message_iter_get_basic(p_iter, &value);
			return int64_t(value);
		}
		case DBUS_TYPE_UINT32: {
			dbus_uint32_t value = 0;
			dbus_message_iter_get_basic(p_iter, &value);
			return int64_t(value);
		}
		case DBUS_TYPE_INT64: {
			dbus_int64_t value = 0;
			dbus_message_iter_get_basic(p_iter, &value);
			return int64_t(value);
		}
		case DBUS_TYPE_UINT64: {
			dbus_uint64_t value = 0;
			dbus_message_iter_get_basic(p_iter, &value);
			return int64_t(value);
		}
		case DBUS_TYPE_DOUBLE: {
			double value = 0.0;
			dbus_message_iter_get_basic(p_iter, &value);
			return value;
		}
		case DBUS_TYPE_VARIANT: {
			DBusMessageIter inner;
			dbus_message_iter_recurse(p_iter, &inner);
			return _read(&inner);
		}
		case DBUS_TYPE_STRUCT: {
			Array fields;
			DBusMessageIter inner;
			dbus_message_iter_recurse(p_iter, &inner);
			while (dbus_message_iter_get_arg_type(&inner) != DBUS_TYPE_INVALID) {
				fields.push_back(_read(&inner));
				dbus_message_iter_next(&inner);
			}
			return fields;
		}
		case DBUS_TYPE_ARRAY: {
			DBusMessageIter inner;
			dbus_message_iter_recurse(p_iter, &inner);
			if (dbus_message_iter_get_element_type(p_iter) == DBUS_TYPE_DICT_ENTRY) {
				Dictionary entries;
				while (dbus_message_iter_get_arg_type(&inner) == DBUS_TYPE_DICT_ENTRY) {
					DBusMessageIter entry;
					dbus_message_iter_recurse(&inner, &entry);
					Variant key = _read(&entry);
					dbus_message_iter_next(&entry);
					entries[key] = _read(&entry);
					dbus_message_iter_next(&inner);
				}
				return entries;
			}
			Array items;
			while (dbus_message_iter_get_arg_type(&inner) != DBUS_TYPE_INVALID) {
				items.push_back(_read(&inner));
				dbus_message_iter_next(&inner);
			}
			return items;
		}
		default:
			return Variant();
	}
}

static Array _read_args(DBusMessage *p_message) {
	Array args;
	DBusMessageIter iter;
	if (dbus_message_iter_init(p_message, &iter)) {
		while (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INVALID) {
			args.push_back(_read(&iter));
			dbus_message_iter_next(&iter);
		}
	}
	return args;
}

static void _append(DBusMessageIter *p_iter, int p_type, const String &p_value) {
	CharString utf8 = p_value.utf8();
	const char *value = utf8.get_data();
	dbus_message_iter_append_basic(p_iter, p_type, &value);
}

// a{sv} with string values.
static void _append_options(DBusMessageIter *p_iter, const Dictionary &p_options) {
	DBusMessageIter dict;
	dbus_message_iter_open_container(p_iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
	for (const KeyValue<Variant, Variant> &option : p_options) {
		DBusMessageIter entry;
		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
		_append(&entry, DBUS_TYPE_STRING, option.key);
		DBusMessageIter value;
		dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &value);
		_append(&value, DBUS_TYPE_STRING, option.value);
		dbus_message_iter_close_container(&entry, &value);
		dbus_message_iter_close_container(&dict, &entry);
	}
	dbus_message_iter_close_container(p_iter, &dict);
}

static String _token() {
	CryptoCore::RandomGenerator rng;
	uint8_t bytes[16];
	if (rng.init() != OK || rng.get_random_bytes(bytes, 16) != OK) {
		return "godot" + itos(OS::get_singleton()->get_ticks_usec());
	}
	return "godot" + String::hex_encode_buffer(bytes, 16);
}

// [[id, {description, trigger_description}], ...] -> [{id, description, trigger_description}].
static TypedArray<Dictionary> _parse_bindings(const Variant &p_shortcuts) {
	TypedArray<Dictionary> result;
	Array shortcuts = p_shortcuts;
	for (const Variant &item : shortcuts) {
		Array pair = item;
		if (pair.size() < 2) {
			continue;
		}
		Dictionary info = pair[1];
		Dictionary binding;
		binding["id"] = pair[0];
		binding["description"] = info.get("description", "");
		binding["trigger_description"] = info.get("trigger_description", "");
		result.push_back(binding);
	}
	return result;
}

struct ShortcutsConnection {
	DBusConnection *bus = nullptr;
	String session;
};

// A signal outside a pending request: an activation or new bindings.
static void _handle_signal(const ShortcutsConnection &p_connection, DBusMessage *p_message, const Callable &p_activated, const Callable &p_bound) {
	if (dbus_message_is_signal(p_message, PORTAL_SHORTCUTS, "Activated")) {
		Array args = _read_args(p_message);
		if (args.size() >= 2 && String(args[0]) == p_connection.session) {
			p_activated.call_deferred(String(args[1]));
		}
	} else if (dbus_message_is_signal(p_message, PORTAL_SHORTCUTS, "ShortcutsChanged")) {
		Array args = _read_args(p_message);
		if (args.size() >= 2 && String(args[0]) == p_connection.session) {
			p_bound.call_deferred(_parse_bindings(args[1]));
		}
	}
}

// Sends a portal request and waits for its Response (or abort). Returns the
// response code (0 success, 1 cancelled by the user, 2 other), or -1 with
// r_error set if the call itself failed.
static int _request(ShortcutsConnection &p_connection, DBusMessage *p_message, Dictionary &r_results, String &r_error, SafeFlag &p_abort, const Callable &p_activated, const Callable &p_bound) {
	DBusError error;
	dbus_error_init(&error);
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(p_connection.bus, p_message, 10000, &error);
	dbus_message_unref(p_message);
	if (!reply || dbus_error_is_set(&error)) {
		r_error = dbus_error_is_set(&error) ? String::utf8(error.message) : String("no reply");
		dbus_error_free(&error);
		return -1;
	}
	Array reply_args = _read_args(reply);
	dbus_message_unref(reply);
	String request_path = reply_args.is_empty() ? String() : String(reply_args[0]);
	while (!p_abort.is_set()) {
		dbus_connection_read_write(p_connection.bus, 100);
		DBusMessage *message = nullptr;
		while ((message = dbus_connection_pop_message(p_connection.bus))) {
			if (dbus_message_is_signal(message, PORTAL_REQUEST, "Response") && String::utf8(dbus_message_get_path(message)) == request_path) {
				Array args = _read_args(message);
				dbus_message_unref(message);
				if (args.size() >= 2) {
					r_results = args[1];
				}
				return args.is_empty() ? 2 : int(args[0]);
			}
			_handle_signal(p_connection, message, p_activated, p_bound);
			dbus_message_unref(message);
		}
	}
	r_error = "stopped";
	return -1;
}

void GlobalShortcuts::_thread_func(void *p_ud) {
	static_cast<GlobalShortcuts *>(p_ud)->_run();
}

void GlobalShortcuts::_run() {
	Callable activated = callable_mp(this, &GlobalShortcuts::_emit_activated);
	Callable bound = callable_mp(this, &GlobalShortcuts::_emit_bound);
	Callable failed = callable_mp(this, &GlobalShortcuts::_emit_failed);
	ShortcutsConnection connection;
	DBusError error;
	dbus_error_init(&error);
	// A private connection: portal sessions live as long as the connection,
	// and the app ID must be registered before any other portal call on it.
	connection.bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
	if (!connection.bus || dbus_error_is_set(&error)) {
		failed.call_deferred(String("No D-Bus session bus."));
		dbus_error_free(&error);
		return;
	}
	dbus_connection_set_exit_on_disconnect(connection.bus, false);

	// Unsandboxed apps identify themselves (needed by GlobalShortcuts); older
	// portals without the registry derive the ID themselves.
	DBusMessage *message = dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, HOST_REGISTRY, "Register");
	DBusMessageIter iter;
	dbus_message_iter_init_append(message, &iter);
	_append(&iter, DBUS_TYPE_STRING, app_id);
	_append_options(&iter, Dictionary());
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection.bus, message, 5000, &error);
	dbus_message_unref(message);
	if (reply) {
		dbus_message_unref(reply);
	}
	if (dbus_error_is_set(&error)) {
		print_verbose(vformat("GlobalShortcuts: app ID not registered: %s", String::utf8(error.message)));
		dbus_error_free(&error);
	}

	dbus_bus_add_match(connection.bus, "type='signal',sender='" PORTAL_NAME "',interface='" PORTAL_REQUEST "',member='Response'", nullptr);
	dbus_bus_add_match(connection.bus, "type='signal',sender='" PORTAL_NAME "',interface='" PORTAL_SHORTCUTS "'", nullptr);

	String problem;
	Dictionary results;
	message = dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, PORTAL_SHORTCUTS, "CreateSession");
	dbus_message_iter_init_append(message, &iter);
	Dictionary options;
	options["handle_token"] = _token();
	options["session_handle_token"] = _token();
	_append_options(&iter, options);
	int code = _request(connection, message, results, problem, thread_abort, activated, bound);
	if (code == 0) {
		connection.session = results.get("session_handle", "");
	}
	if (connection.session.is_empty()) {
		if (!thread_abort.is_set()) {
			failed.call_deferred(problem.is_empty() ? String("The desktop refused a shortcut session.") : problem);
		}
	} else {
		message = dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, PORTAL_SHORTCUTS, "BindShortcuts");
		dbus_message_iter_init_append(message, &iter);
		_append(&iter, DBUS_TYPE_OBJECT_PATH, connection.session);
		DBusMessageIter list;
		dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(sa{sv})", &list);
		for (const Variant &item : requested) {
			Dictionary shortcut = item;
			DBusMessageIter entry;
			dbus_message_iter_open_container(&list, DBUS_TYPE_STRUCT, nullptr, &entry);
			_append(&entry, DBUS_TYPE_STRING, shortcut.get("id", ""));
			Dictionary info;
			info["description"] = shortcut.get("description", "");
			if (!String(shortcut.get("preferred_trigger", "")).is_empty()) {
				info["preferred_trigger"] = shortcut["preferred_trigger"];
			}
			_append_options(&entry, info);
			dbus_message_iter_close_container(&list, &entry);
		}
		dbus_message_iter_close_container(&iter, &list);
		_append(&iter, DBUS_TYPE_STRING, String());
		options.clear();
		options["handle_token"] = _token();
		_append_options(&iter, options);
		results.clear();
		code = _request(connection, message, results, problem, thread_abort, activated, bound);
		if (code == 0) {
			bound.call_deferred(_parse_bindings(results.get("shortcuts", Array())));
		} else if (!thread_abort.is_set()) {
			failed.call_deferred(code == 1 ? String("The shortcuts were declined.") : (problem.is_empty() ? String("The desktop didn't bind the shortcuts.") : problem));
		}
		// Listen for presses until unbound or quitting.
		while (code == 0 && !thread_abort.is_set()) {
			dbus_connection_read_write(connection.bus, 100);
			DBusMessage *incoming = nullptr;
			while ((incoming = dbus_connection_pop_message(connection.bus))) {
				_handle_signal(connection, incoming, activated, bound);
				dbus_message_unref(incoming);
			}
		}
		message = dbus_message_new_method_call(PORTAL_NAME, connection.session.utf8().get_data(), PORTAL_SESSION, "Close");
		reply = dbus_connection_send_with_reply_and_block(connection.bus, message, 1000, nullptr);
		dbus_message_unref(message);
		if (reply) {
			dbus_message_unref(reply);
		}
	}
	dbus_connection_close(connection.bus);
	dbus_connection_unref(connection.bus);
}
#endif // DBUS_ENABLED

GlobalShortcuts *GlobalShortcuts::singleton = nullptr;

void GlobalShortcuts::_set_status(const String &p_status) {
	MutexLock lock(mutex);
	status = p_status;
}

void GlobalShortcuts::_emit_bound(const TypedArray<Dictionary> &p_bindings) {
	{
		MutexLock lock(mutex);
		bindings = p_bindings;
		status = "bound";
	}
	emit_signal(SNAME("bound"), p_bindings);
}

void GlobalShortcuts::_emit_activated(const String &p_id) {
	emit_signal(SNAME("activated"), p_id);
}

void GlobalShortcuts::_emit_failed(const String &p_message) {
	_set_status("failed: " + p_message);
	emit_signal(SNAME("failed"), p_message);
}

bool GlobalShortcuts::is_supported() const {
#ifdef DBUS_ENABLED
	if (!_dbus_loaded()) {
		return false;
	}
	DBusError error;
	dbus_error_init(&error);
	DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
	if (!bus || dbus_error_is_set(&error)) {
		dbus_error_free(&error);
		return false;
	}
	dbus_connection_set_exit_on_disconnect(bus, false);
	DBusMessage *message = dbus_message_new_method_call(PORTAL_NAME, PORTAL_PATH, "org.freedesktop.DBus.Properties", "Get");
	DBusMessageIter iter;
	dbus_message_iter_init_append(message, &iter);
	_append(&iter, DBUS_TYPE_STRING, PORTAL_SHORTCUTS);
	_append(&iter, DBUS_TYPE_STRING, "version");
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, message, 2000, &error);
	dbus_message_unref(message);
	bool supported = false;
	if (reply && !dbus_error_is_set(&error)) {
		Array args = _read_args(reply);
		supported = !args.is_empty() && int(args[0]) >= 1;
	}
	if (reply) {
		dbus_message_unref(reply);
	}
	dbus_error_free(&error);
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
	return supported;
#else
	return false;
#endif
}

Error GlobalShortcuts::bind_shortcuts(const String &p_app_id, const TypedArray<Dictionary> &p_shortcuts) {
#ifdef DBUS_ENABLED
	ERR_FAIL_COND_V_MSG(p_app_id.is_empty(), ERR_INVALID_PARAMETER, "An app ID is required (the name of the app's .desktop file, without .desktop).");
	if (!_dbus_loaded()) {
		return ERR_UNAVAILABLE;
	}
	unbind_shortcuts();
	app_id = p_app_id;
	requested = p_shortcuts.duplicate(true);
	_set_status("binding");
	thread_abort.clear();
	thread.start(_thread_func, this);
	return OK;
#else
	return ERR_UNAVAILABLE;
#endif
}

void GlobalShortcuts::unbind_shortcuts() {
#ifdef DBUS_ENABLED
	if (thread.is_started()) {
		thread_abort.set();
		thread.wait_to_finish();
	}
#endif
	MutexLock lock(mutex);
	bindings.clear();
	status = "unbound";
}

TypedArray<Dictionary> GlobalShortcuts::get_bindings() {
	MutexLock lock(mutex);
	return bindings.duplicate(true);
}

String GlobalShortcuts::get_status() {
	MutexLock lock(mutex);
	return status;
}

void GlobalShortcuts::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_supported"), &GlobalShortcuts::is_supported);
	ClassDB::bind_method(D_METHOD("bind_shortcuts", "app_id", "shortcuts"), &GlobalShortcuts::bind_shortcuts);
	ClassDB::bind_method(D_METHOD("unbind_shortcuts"), &GlobalShortcuts::unbind_shortcuts);
	ClassDB::bind_method(D_METHOD("get_bindings"), &GlobalShortcuts::get_bindings);
	ClassDB::bind_method(D_METHOD("get_status"), &GlobalShortcuts::get_status);

	ADD_SIGNAL(MethodInfo("bound", PropertyInfo(Variant::ARRAY, "bindings", PROPERTY_HINT_ARRAY_TYPE, "Dictionary")));
	ADD_SIGNAL(MethodInfo("activated", PropertyInfo(Variant::STRING, "id")));
	ADD_SIGNAL(MethodInfo("failed", PropertyInfo(Variant::STRING, "message")));
}

GlobalShortcuts::GlobalShortcuts() {
	singleton = this;
}

GlobalShortcuts::~GlobalShortcuts() {
	unbind_shortcuts();
	singleton = nullptr;
}
