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

#include <stdio.h>
#include <stdlib.h>
#include <check.h>


/* This must be provided by the test suite implementation */
SRunner *configure_tests(void);

int main(void)
{
	int nf = 0;

	/* Configure test suites to be executed */
	SRunner *sr = configure_tests();

	/* Run tests */
	srunner_run_all(sr, CK_ENV);

	/* Retrieve number of failed tests */
	nf = srunner_ntests_failed(sr);

	/* Free resouces */
	srunner_free(sr);

	/* Return global success or failure */
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
