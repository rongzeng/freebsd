/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $Id$
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/diskslice.h>
#include <sys/disklabel.h>
#include <err.h>
#include "libdisk.h"

/*
 *  Rule#0:
 *	Chunks of type 'whole' can have max NDOSPART children.
 */
void
Rule_000(struct disk *d, struct chunk *c, char *msg)
{
	int i;
	struct chunk *c1;

	if (c->type != whole)
		return;
	for (i=0, c1=c->part; c1; c1=c1->next)
		if (c1->type != unused)
			i++;
	if (i <= NDOSPART)
		return;
	sprintf(msg+strlen(msg),
		"%d is too many children of the 'whole' chunk.  Max is %d\n",
		i, NDOSPART);
}

/* 
 * Rule#1:
 *	All children of 'whole' must be track-aligned
 */
void
Rule_001(struct disk *d, struct chunk *c, char *msg)
{
	int i;
	struct chunk *c1;

	if (c->type != whole)
		return;
	for (i=0, c1=c->part; c1; c1=c1->next) {
		if (c1->type == reserved)
			continue;
		if (c1->type == unused)
			continue;
		if (!Aligned(d,c1->offset))
			sprintf(msg+strlen(msg),
		    "chunk '%s' [%ld..%ld] does not start on a track boundary\n",
				c1->name,c1->offset,c1->end);
		if (!Aligned(d,c1->end+1))
			sprintf(msg+strlen(msg),
		    "chunk '%s' [%ld..%ld] does not end on a track boundary\n",
				c1->name,c1->offset,c1->end);
	}
}

void
Check_Chunk(struct disk *d, struct chunk *c, char *msg)
{
	Rule_000(d,c,msg);
	Rule_001(d,c,msg);
	if (c->part)
		Check_Chunk(d,c->part,msg);
	if (c->next)
		Check_Chunk(d,c->next,msg);
	return;
}

char *
CheckRules(struct disk *d)
{
	char msg[BUFSIZ];

	*msg = '\0';
	Check_Chunk(d,d->chunks,msg);
	if (*msg)
		return strdup(msg);
	return 0;
}
