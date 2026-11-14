/* 
 * XPilot NG, a multiplayer space war game.
 *
 * Copyright (C) 1991-2001 by
 *
 *      Bjørn Stabell        <bjoern@xpilot.org>
 *      Ken Ronny Schouten   <ken@xpilot.org>
 *      Bert Gijsbers        <bert@xpilot.org>
 *      Dick Balaska         <dick@xpilot.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef	METASERVER_H
#define	METASERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define META_PORT	5500	/* for server-metaserver communication */
#define META_HOST	"meta.xpilot.org"
#define META_HOST_TWO	"meta2.xpilot.org"
#define META_IP		"45.55.104.252"
#define META_IP_TWO	"45.55.104.252"

/*
 * Meta server list, read from lib/metaserver.cfg: one "hostname ip" pair per
 * line, the IP is the fallback if the hostname cannot be resolved; '#' starts
 * a comment.  Shared by server, client and launcher (C and C++).
 */
#define META_MAX_SERVERS	8

struct MetaEntry {
    char name[64];
    char addr[16];
};

/*
 * Fills out[] (at most META_MAX_SERVERS entries) and returns the count.
 * Uses the built-in defaults if the file is missing or has no entries.
 */
static inline int Meta_read_config(const char *path, struct MetaEntry *out)
{
    char line[256], name[64], addr[16];
    int n = 0;
    FILE *fp = path ? fopen(path, "r") : NULL;

    if (fp) {
	while (n < META_MAX_SERVERS && fgets(line, sizeof(line), fp)) {
	    char *hash = strchr(line, '#');
	    if (hash)
		*hash = '\0';
	    if (sscanf(line, "%63s %15s", name, addr) == 2) {
		strcpy(out[n].name, name);
		strcpy(out[n].addr, addr);
		n++;
	    }
	}
	fclose(fp);
    }
    if (n == 0) {
	strcpy(out[0].name, META_HOST);
	strcpy(out[0].addr, META_IP);
	strcpy(out[1].name, META_HOST_TWO);
	strcpy(out[1].addr, META_IP_TWO);
	n = 2;
    }
    return n;
}

#endif
