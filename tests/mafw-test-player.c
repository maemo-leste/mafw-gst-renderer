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

/*
 * test.c
 *
 * Test of the playback system
 *
 * Copyright (C) 2007 Nokia Corporation
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>

#include "mafw-gst-renderer.h"
#include <libmafw/mafw-metadata.h>
#include <libmafw/mafw-registry.h>

MafwGstRenderer *gst_renderer;
GMainLoop *loop;
static struct termios tio_orig;
guint seek_delta = 2;
gfloat volume = 0.7;
gboolean muted = FALSE;


/**
 *@time: how long to wait, in microsecs
 *
 */
int kbhit (int time) {
	fd_set rfds;
	struct timeval tv;
	int retval;
	char c;

	FD_ZERO (&rfds);
	FD_SET (0, &rfds);

	/* Wait up to 'time' microseconds. */
	tv.tv_sec=time / 1000;
	tv.tv_usec = (time % 1000)*1000;

	retval=select (1, &rfds, NULL, NULL, &tv);
	if(retval < 1) return -1;
	retval = read (0, &c, 1);
	if (retval < 1) return -1;
	return (int) c;
}


/**
 *
 *
 */
static void raw_kb_enable (void) {
	struct termios tio_new;
	tcgetattr(0, &tio_orig);
	
	tio_new = tio_orig;
	tio_new.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
	tio_new.c_cc[VMIN] = 1;
	tio_new.c_cc[VTIME] = 0;
	tcsetattr (0, TCSANOW, &tio_new);
}

static void raw_kb_disable (void)
{
	tcsetattr (0,TCSANOW, &tio_orig);
}

static void get_position_cb(MafwRenderer* self, gint position, gpointer user_data,
			    const GError* error)
{
	guint* rpos = (guint*) user_data;
	g_return_if_fail(rpos != NULL);

	*rpos = position;
}

/**
 *
 *
 */
static gboolean idle_cb (gpointer data)
{
	gboolean ret = TRUE;
	GError *error = NULL;

	int c = kbhit (0);
	if (c == -1) {
		usleep (10 * 1000);
		return TRUE;
	}

	printf ("c = %d\n", c);
	/* '.' key */
	if (c == 46) {
		printf ("Seeking %d seconds forward\n", seek_delta);
		gint pos = 0;

		mafw_gst_renderer_get_position(MAFW_RENDERER(gst_renderer),
					     get_position_cb, &pos);

		printf ("  Position before seek: %d\n", pos);
		pos += seek_delta;
		mafw_gst_renderer_set_position(MAFW_RENDERER(gst_renderer), pos,
					     NULL, NULL);

		mafw_gst_renderer_get_position(MAFW_RENDERER(gst_renderer),
					     get_position_cb, &pos);

		printf ("  Position after seek: %d\n", pos);
	}
	/* ',' key */
	else if (c == 44) {
		printf ("Seeking %d seconds backwards\n", seek_delta);
		gint pos = 0;

		mafw_gst_renderer_get_position(MAFW_RENDERER(gst_renderer),
					     get_position_cb, &pos);

		printf ("  Position before seek: %d\n", pos);
		pos -= seek_delta;
		mafw_gst_renderer_set_position(MAFW_RENDERER(gst_renderer), pos,
					     NULL, NULL);

		mafw_gst_renderer_get_position(MAFW_RENDERER(gst_renderer),
					     get_position_cb, &pos);

		printf ("  Position after seek: %d\n", pos);
	}
	/* '' (space) key */
	else if (c == 32) {
		if (gst_renderer->current_state == Playing) {
			printf ("Pausing...\n");
			mafw_gst_renderer_pause(MAFW_RENDERER (gst_renderer), NULL, NULL);
		}
		else if (gst_renderer->current_state == Paused) {
			printf ("Resuming...\n");
			mafw_gst_renderer_resume(MAFW_RENDERER (gst_renderer), NULL, NULL);
		}
	}
	/* 'p' key */
	else if (c == 112) {
		printf ("Playing...\n");
		mafw_gst_renderer_play (MAFW_RENDERER (gst_renderer), NULL, NULL);
	}
	/* 's' key */
	else if (c == 115) {
		printf ("Stopping\n");
		mafw_gst_renderer_stop (MAFW_RENDERER (gst_renderer), NULL, NULL);
	}
	/* 'g' key */
	else if (c == 103) {
		printf ("Getting position\n");
		gint pos = 0;

		mafw_gst_renderer_get_position(MAFW_RENDERER(gst_renderer),
					     get_position_cb, &pos);

		printf ("Current position: %d\n", pos);
	}
	/* '+' key */
	else if (c == 43) {
		volume += 0.1;
		printf ("Increasing volume to %lf\n", volume);
		mafw_extension_set_property_float(MAFW_EXTENSION(gst_renderer),
					     "volume", volume);
	}
	/* '-' key */
	else if (c == 45) {
		volume -= 0.1;
		printf ("Decreasing volume to %lf\n", volume);
		mafw_extension_set_property_float(MAFW_EXTENSION(gst_renderer),
					     "volume", volume);
	}
	/* 'm' key */
	else if (c == 109) {
		muted = !muted;
		printf ("(Un)Muting...\n");
		mafw_extension_set_property_boolean(MAFW_EXTENSION(gst_renderer),
					       "mute", muted);
	}
	/* '?' key */
	else if (c == 63) {
		printf ("COMMANDS:\n" \
			"    s\t\tStop\n" \
			"    p\t\tPlay\n" \
			"    space\tPause/Resume\n" \
			"    +\t\tVolume up\n" \
			"    -\t\tVolume down\n" \
			"    m\t\tMute/Unmute\n" \
			"    .\t\tSeek forward 2 sec\n" \
			"    ,\t\tSeek backwards 2 sec\n" \
			"    q\t\tQuit\n");
	}
	/* 'q' key */
	else if (c == 113) {
		printf ("QUIT\n");
		mafw_gst_renderer_stop (MAFW_RENDERER (gst_renderer), NULL, NULL);
		raw_kb_disable ();
		g_main_loop_quit (loop);
		ret = FALSE;
	}
	if (error) {
		printf ("Error occured during the operation\n");
		g_error_free (error);
	}
	return ret;
}


/**
 *
 *
 */
static void metadata_changed (MafwGstRenderer *gst_renderer,
			      GHashTable *metadata,
			      gpointer user_data)
{
	g_print("Metadata changed:\n");
	mafw_metadata_print (metadata, NULL);
}


/**
 *
 *
 */
static void buffering_cb (MafwGstRenderer *gst_renderer,
			  gfloat percentage,
     			  gpointer user_data)
{
	g_print("Buffering: %f\n", percentage);
}

static void play_uri_cb(MafwRenderer* renderer, gpointer user_data, const GError* error)
{
	if (error != NULL) {
		printf("Unable to play: %s\n", error->message);
		exit(1);
	}
}

/**
 *
 *
 */
gint main(gint argc, gchar ** argv)
{
	MafwRegistry *registry;
	
	g_type_init();
	gst_init (&argc, &argv);

	if (argc != 2) {
		g_print("Usage: mafw-test-player <media-uri>\n");
		exit(1);
	}

	raw_kb_enable();
	
	registry = MAFW_REGISTRY(mafw_registry_get_instance());
	gst_renderer = MAFW_GST_RENDERER(mafw_gst_renderer_new(registry));
	g_signal_connect (G_OBJECT (gst_renderer),
			  "metadata_changed",
			  G_CALLBACK (metadata_changed),
			  gst_renderer);

	g_signal_connect (G_OBJECT (gst_renderer),
			  "buffering_info",
			  G_CALLBACK (buffering_cb),
			  gst_renderer);

	mafw_renderer_play_uri(MAFW_RENDERER (gst_renderer), argv[1], play_uri_cb,
			    NULL);
	
	loop = mafw_gst_renderer_get_loop(gst_renderer);

	g_idle_add (idle_cb, NULL);
	g_main_loop_run (loop);

	g_object_unref (G_OBJECT (gst_renderer));
	return 0;
}
