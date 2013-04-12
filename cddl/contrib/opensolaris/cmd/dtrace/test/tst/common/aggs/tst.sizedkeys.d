/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#pragma D option quiet

/*
 * Make sure the sizes of compatible keys doesn't affect the sort order.
 */

BEGIN
{
	@[(int)1, 0] = sum(10);
	@[(uint64_t)2, 0] = sum(20);
	@[(int)3, 0] = sum(30);
	@[(uint64_t)4, 0] = sum(40);
	printa(@);

	exit(0);
}
