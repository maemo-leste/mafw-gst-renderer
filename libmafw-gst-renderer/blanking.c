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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <libosso.h>
#include "blanking.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-blanking"

#define VIDEO_BLANKING_TIMER_INTERVAL	45000

static guint blanking_timeout_id = 0;
static osso_context_t *osso_ctx = NULL;
static gboolean can_control_blanking = TRUE;
static gboolean is_blanking_prohibited = FALSE;

static void remove_blanking_timeout(void)
{
	if (blanking_timeout_id) {
		g_source_remove(blanking_timeout_id);
		blanking_timeout_id = 0;
	}
}

/*
 * Re-enables screen blanking.
 */
void blanking_allow(void)
{
        is_blanking_prohibited = FALSE;
        remove_blanking_timeout();
}

static gboolean no_blanking_timeout(void)
{
	/* Stop trying if it fails. */
	return osso_display_blanking_pause(osso_ctx) == OSSO_OK;
}

/*
 * Adds a timeout to periodically disable screen blanking.
 */
void blanking_prohibit(void)
{
        is_blanking_prohibited = TRUE;
	if ((!osso_ctx) || (!can_control_blanking))
		return;
	osso_display_state_on(osso_ctx);
	osso_display_blanking_pause(osso_ctx);
	if (blanking_timeout_id == 0) {
		blanking_timeout_id =
			g_timeout_add(VIDEO_BLANKING_TIMER_INTERVAL,
				      (gpointer)no_blanking_timeout,
				      NULL);
	}
}

void blanking_init(void)
{
	/* It's enough to initialize it once for a process. */
	if (osso_ctx)
		return;
	osso_ctx = osso_initialize(PACKAGE, VERSION, 0, NULL);
	if (!osso_ctx)
		g_warning("osso_initialize failed, screen may go black");
        is_blanking_prohibited = FALSE;
        /* Default policy is to allow user to control blanking */
        blanking_control(TRUE);
}

void blanking_deinit(void)
{
	if (!osso_ctx)
		return;
	blanking_control(FALSE);
	osso_deinitialize(osso_ctx);
	osso_ctx = NULL;
}

void blanking_control(gboolean activate)
{
        can_control_blanking = activate;
        if (!can_control_blanking) {
                remove_blanking_timeout();
        } else {
                /* Restore the last state */
                if (is_blanking_prohibited) {
                        blanking_prohibit();
                } else {
                        blanking_allow();
                }
        }
}
