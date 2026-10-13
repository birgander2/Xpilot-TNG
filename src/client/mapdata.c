/*
 * XPilot NG, a multiplayer space war game.
 *
 * Copyright (C) 2001 Juha Lindström <juhal@users.sourceforge.net>
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

#include "xpclient.h"

/* kps - you should be able to change this without a recompile */
#define DATADIR ".xpilot_data"
#define COPY_BUF_SIZE 8192

typedef struct {
    char *protocol;
    char *host;
    int   port;
    const char *path;
    char *query;
} URL;

static int Mapdata_extract(const char *name, const char *extract_dir);
static int Mapdata_download(const char *urlstr, const URL *url, const char *filePath);
static int Url_parse(const char *urlstr, URL *url);
static void Url_free_parsed(URL *url);

static bool setup_done = false;

static bool Mapdata_all_bitmaps_present(void)
{
    int i, num_pstyles, num_estyles, num_bmaps;
    const char *ptr;
    char fpath[2048];

    if (!Setup || oldServer)
	return true;

    ptr = (const char *)Setup->map_data;
    num_pstyles = *ptr++ & 0xff;
    num_estyles = *ptr++ & 0xff;
    num_bmaps = *ptr++ & 0xff;

    if (num_bmaps == 0)
	return true;

    ptr += num_pstyles * 7;
    ptr += num_estyles * 6;

    for (i = 0; i < num_bmaps; i++) {
	char fname[30];
	strlcpy(fname, ptr, sizeof(fname));
	ptr += strlen(fname) + 1;
	ptr++; /* flags */
	if (!Picture_find_path(fname, fpath, sizeof(fpath)))
	    return false;
    }
    return true;
}

int Mapdata_setup(const char *urlstr)
{
    URL url;
    const char *name, *dir = NULL;
    char path[2048], extract_dir[2048], local_xpd[2048], buf[1024], *ptr;
    int rv = false;

    if (setup_done)
	return true;

    if (Mapdata_all_bitmaps_present()) {
	setup_done = true;
	return true;
    }

    memset(path, 0, sizeof(path));
    memset(extract_dir, 0, sizeof(extract_dir));
    memset(buf, 0, sizeof(buf));

    if (!Url_parse(urlstr, &url)) {
	warn("malformed URL: %s", urlstr);
	return false;
    }

    for (name = url.path + strlen(url.path) - 1; name > url.path; name--) {
	if (*(name - 1) == '/')
	    break;
    }

    if (*name == '\0') {
	warn("no file name in URL: %s", urlstr);
	goto end;
    }

    if (realTexturePath != NULL) {
	char *rtp_copy = xp_safe_strdup(realTexturePath);
	for (dir = strtok(rtp_copy, ":"); dir; dir = strtok(NULL, ":")) {
	    if (access(dir, R_OK | W_OK | X_OK) == 0) {
		snprintf(buf, sizeof(buf), "%s", dir);
		break;
	    }
	}
	free(rtp_copy);
	if (buf[0] != '\0')
	    dir = buf;
	else
	    dir = NULL;
    }
	
    if (dir == NULL) {

	/* realTexturePath hasn't got a directory with proper access rights */
	/* so lets create one into users home dir */

	char *home = getenv("HOME");
	if (home == NULL) {
	    error("couldn't access any dir in %s and HOME is unset", path);
	    goto end;
	}

	if (strlen(home) == 0)
	    sprintf(buf, "%s", DATADIR);
	else if (home[strlen(home) - 1] == PATHNAME_SEP)
	    sprintf(buf, "%s%s", home, DATADIR);
	else
	    sprintf(buf, "%s%c%s", home, PATHNAME_SEP, DATADIR);

	if (access(buf, F_OK) != 0) {
	    if (mkdir(buf, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
		error("failed to create directory %s", dir);
		goto end;
	    }
	}

	dir = buf;
    }

    if (strlen(dir) == 0)
	snprintf(path, sizeof(path), "%s", name);
    else if (dir[strlen(dir) - 1] == PATHNAME_SEP)
	snprintf(path, sizeof(path), "%s%s", dir, name);
    else
	snprintf(path, sizeof(path), "%s%c%s", dir, PATHNAME_SEP, name);

    if (strrchr(path, '.') == NULL) {
	error("no extension in file name %s.", name);
	goto end;
    }

    /* make extract_dir point to the directory name without .xpd */
    strlcpy(extract_dir, path, sizeof(extract_dir));
    ptr = strrchr(extract_dir, '.');
    if (ptr)
	*ptr = '\0';

    /* add this new texture directory to realTexturePath */
    if (realTexturePath == NULL) {
	realTexturePath = strdup(extract_dir);
    } else {
	char *temp = XMALLOC(char, strlen(realTexturePath) + strlen(extract_dir) + 2);
	if (temp == NULL) {
	    error("not enough memory to new realTexturePath");
	    goto end;
	}
	sprintf(temp, "%s:%s", realTexturePath, extract_dir);
	free(realTexturePath);
	realTexturePath = temp;
    }

    if (access(extract_dir, F_OK) == 0 || Mapdata_all_bitmaps_present()) {
	rv = true;
	setup_done = true;
	goto end;
    }

    /* Check if the .xpd archive already exists locally (e.g. in lib/maps/ or ~/.xpilot_data/) */
    snprintf(local_xpd, sizeof(local_xpd), "%s%s", Conf_mapdir(), name);
    if (access(local_xpd, R_OK) == 0) {
	if (Mapdata_extract(local_xpd, extract_dir)) {
	    rv = true;
	    setup_done = true;
	    goto end;
	}
    }

    if (access(path, R_OK) == 0) {
	if (Mapdata_extract(path, extract_dir)) {
	    rv = true;
	    setup_done = true;
	    goto end;
	}
    }

    warn("Downloading map data from %s to %s.", urlstr, path);

    if (!Mapdata_download(urlstr, &url, path)) {
	warn("downloading map data failed");
	goto end;
    }

    if (!Mapdata_extract(path, extract_dir)) {
	warn("extracting map data failed");
	goto end;
    }

    rv = true;
    setup_done = true;

 end:
    Url_free_parsed(&url);
    return rv;
}


static int Mapdata_extract(const char *name, const char *extract_dir)
{
    gzFile in;
    FILE *out;
    int retval;
    size_t rlen, wlen;
    char dir[2048], buf[COPY_BUF_SIZE], entry_name[512], fname[2600];
    const char *base;
    long int size;
    int count, i;

    strlcpy(dir, extract_dir, sizeof(dir));

    if (access(dir, F_OK) != 0) {
	if (mkdir(dir, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
	    error("failed to create directory %s", dir);
	    return 0;
	}
    }

    if ((in = gzopen(name, "rb")) == NULL) {
	error("failed to open %s for reading", name);
	return 0;
    }

    if (gzgets(in, buf, COPY_BUF_SIZE) == Z_NULL) {
	error("failed to read header from %s", name);
	gzclose(in);
	return 0;
    }

    if (sscanf(buf, "XPD %d\n", &count) != 1) {
	error("invalid header in %s", name);
	gzclose(in);
	return 0;
    }

    for (i = 0; i < count; i++) {

	if (gzgets(in, buf, COPY_BUF_SIZE) == Z_NULL) {
	    error("failed to read file info from %s", name);
	    gzclose(in);
	    return 0;
	}

	if (sscanf(buf, "%511s %ld\n", entry_name, &size) != 2) {
	    error("failed to parse file info %s", buf);
	    gzclose(in);
	    return 0;
	}

	/* security check: reject parent traversal, strip subdirectory prefix */
	if (strstr(entry_name, "..") != NULL) {
	    error("file name %s is illegal", entry_name);
	    gzclose(in);
	    return 0;
	}
	base = strrchr(entry_name, PATHNAME_SEP);
	base = (base != NULL) ? (base + 1) : entry_name;
	if (*base == '\0') {
	    error("file name %s is illegal", entry_name);
	    gzclose(in);
	    return 0;
	}

	snprintf(fname, sizeof(fname), "%s%c%s", dir, PATHNAME_SEP, base);

	warn("Extracting %s (%ld)", fname, size);

	if ((out = fopen(fname, "wb")) == NULL) {
	    error("failed to open %s for writing", fname);
	    gzclose(in);
	    return 0;
	}

	while (size > 0) {
	    retval = gzread(in, buf, MIN(COPY_BUF_SIZE, (unsigned)size));
	    if (retval == -1) {
		error("error when reading %s", name);
		gzclose(in);
		fclose(out);
		return 0;
	    }
	    if (retval == 0) {
		error("unexpected end of file %s", name);
		gzclose(in);
		fclose(out);
		return 0;
	    }

	    rlen = retval;
	    wlen = fwrite(buf, 1, rlen, out);
	    if (wlen != rlen) {
		error("failed to write to %s", fname);
		gzclose(in);
		fclose(out);
		return 0;
	    }

	    size -= rlen;
	}

	fclose(out);
    }

    gzclose(in);
    return 1;
}


static int Mapdata_download(const char *urlstr, const URL *url, const char *filePath)
{
    char buf[1024];
    int rv, header, c, len, i;
    sock_t s;
    FILE *f = NULL;
    size_t n;

    if (strncmp("http", url->protocol, 4) != 0) {
	error("unsupported protocol %s", url->protocol);
	return false;
    }

    /* Try curl first so HTTP->HTTPS redirects (e.g. sourceforge.net) work */
    if (strpbrk(urlstr, "'\"`$;|&<>") == NULL &&
	strpbrk(filePath, "'\"`$;|&<>") == NULL) {
	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
		 "curl -fsSL --connect-timeout 5 --max-time 15 -o \"%s\" \"%s\" 2>/dev/null",
		 filePath, urlstr);
	if (system(cmd) == 0 && access(filePath, R_OK) == 0) {
	    return true;
	}
    }

    if (sock_open_tcp(&s) == SOCK_IS_ERROR) {
	error("failed to create a socket");
	return false;
    }
    if (sock_connect(&s, url->host, url->port) == SOCK_IS_ERROR) {
	error("couldn't connect to download address");
	sock_close(&s);
	return false;
    }

    if (url->query) {
	if (snprintf(buf, sizeof buf,
	     "GET %s?%s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
	     url->path, url->query, url->host, url->port) == -1) {
	    error("too long URL");
	    sock_close(&s);
	    return false;
	}

    } else {
	if (snprintf(buf, sizeof buf,
	     "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
	     url->path, url->host, url->port) == -1) {

	    error("too long URL");
	    sock_close(&s);
	    return false;
	}
    }

    if (sock_write(&s, buf, (int)strlen(buf)) == -1) {
	error("socket write failed");
	sock_close(&s);
	return false;
    }

    header = 2;
    c = 0;

    for(;;) {
	len = 0;
	while (len < 100) {
	    if ((i = sock_read(&s, buf + len, sizeof(buf) - len)) == -1) {
		error("socket read failed");
		rv = false;
		goto done;
	    }
	    if (i == 0)
		break;
	    len += i;
	}

	if (len == 0) {
	    rv = !header;
	    break;
	}

	if (header == 2) {
	    if (strncmp(buf, "HTTP", 4)) {
		rv = false;
		break;
	    }
	    i = 0;
	    while (buf[i] != ' ') {
		i++;
		if (i >= len - 1) {
		    rv = false;
		    goto done;
		}
	    }
	    i++;
	    if (buf[i] != '2') {   /* HTTP status code starts with 2 */
		rv = false;
		break;
	    }
	    header = 1;
	}

	printf("#");
	fflush(stdout);

	if (header) {
	    for (i = 0; i < len; i++) {
		if (c % 2 == 0 && buf[i] == '\r')
		    c++;
		else if (c % 2 == 1 && buf[i] == '\n')
		    c++;
		else
		    c = 0;

		if (c == 4) {
		    header = 0;
		    if ((f = fopen(filePath, "wb")) == NULL) {
			error("failed to open %s", filePath);
			rv = false;
			goto done;
		    }
		    if (i < len - 1) {
			n = len - i - 1;
			memmove(buf, buf + i + 1, n);
			len = len - i - 1;
		    } else if (i == len - 1) {
			len = 0;
		    }
		}
	    }
	}

	if (!header && len) {
	    n = len;
	    if (fwrite(buf, 1, n, f) < n) {
		error("file write failed");
		rv =  false;
		break;
	    }
	}
    }
 done:
    printf("\n");
    if (f)
	if (fclose(f) != 0)
	    error("Error closing texture file %s", filePath);
    sock_close(&s);
    return rv;
}


static int Url_parse(const char *urlstr, URL *url)
{
    int len, i, beg, doPort;
    char *buf;

    memset(url, 0, sizeof(URL));
    url->port = 80;
    url->path = "/";

    len = strlen(urlstr);
    buf = strdup(urlstr);
    if (buf == NULL) {
	error("no memory for URL");
	return false;
    }

    for (i = 0; i < len; i++) {
	if (buf[i] == ':') {
	    buf[i] = '\0';
	    url->protocol = buf;
	    break;
	}
    }

    beg = i + 3;
    if (beg >= len || buf[i + 1] != '/' || buf[i + 2] != '/') {
	free(buf);
	return false;
    }

    doPort = 0;
    for (i = beg; i < len; i++) {
	if (buf[i] == ':' || buf[i] == '/') {
	    if (buf[i] == ':') doPort = 1;
	    buf[i] = '\0';
	    break;
	}
    }

    url->host = buf + beg;
    beg = i + 1;
    if (beg >= len) return true;

    if (doPort) {
	for (i = beg; i < len; i++) {
	    if (buf[i] == '/') {
		buf[i] = '\0';
		break;
	    }
	}
	url->port = atoi(buf + beg);
	/* error detection should be added */

	beg = i + 1;
	if (beg >= len)
	    return true;
    }

    /* make space for / in the beginning of path */
    memmove(url->host - 1, url->host, strlen(url->host) + 1);
    url->host--;
    buf[beg - 1] = '/';

    for (i = beg; i < len; i++) {
	if (buf[i] == '?') {
	    buf[i] = '\0';
	    break;
	}
    }
    url->path = buf + beg - 1;

    beg = i + 1;
    if (beg >= len) return true;

    url->query = buf + beg;
    return true;
}


static void Url_free_parsed(URL *url)
{
    free(url->protocol);
}
