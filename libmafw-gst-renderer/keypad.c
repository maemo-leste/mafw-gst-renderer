/*
 * This file is a part of MAFW
 *
 * Copyright (C) 2007, 2008, 2009 Nokia Corporation, all rights reserved.
 *
 * Contact: Visa Smolander <visa.smolander@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
 
#include <glib.h>
#include <mce/dbus-names.h>
#include <dbus/dbus.h>
#include "keypad.h"


#define KEYPAD_TIMER_INTERVAL 50

static guint toutid;

void keypadlocking_allow(void)
{
	if (toutid)
	{
		g_source_remove(toutid);
		toutid = 0;
	}
}

static gboolean no_keylock_timeout(gpointer udata)
{
	static DBusMessage *msg = NULL;
	static DBusConnection *sysbus = NULL;

	if (!sysbus)
	{
		DBusError err;

		dbus_error_init(&err);
		sysbus = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
		g_assert(sysbus);
		g_assert(!dbus_error_is_set(&err));
	}
	if (!msg)
	{
		msg = dbus_message_new_method_call(MCE_SERVICE,
				MCE_REQUEST_PATH, MCE_REQUEST_IF,
				MCE_PREVENT_KEYPAD_OFF_REQ);
		g_assert(msg);
	}
	g_assert(dbus_connection_send(sysbus, msg,NULL));
	dbus_connection_flush(sysbus);
	return TRUE;
}

void keypadlocking_prohibit(void)
{
	if (!toutid)
	{
		toutid = g_timeout_add_seconds(KEYPAD_TIMER_INTERVAL,
                                              no_keylock_timeout,
                                              NULL);
		no_keylock_timeout(NULL);
	}
}
