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

#include "xpcommon.h"
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

static char datadir[PATH_MAX] = "";

/*
 * Relocatable bundle support: if "lib/maps" sits next to the executable,
 * make that directory the CWD so all "lib/..." relative paths resolve.
 */
void Conf_enter_bundle_dir(void)
{
    char exe[PATH_MAX], probe[PATH_MAX + 16];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0)
        return;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash)
        return;
    *slash = '\0';
    snprintf(probe, sizeof(probe), "%s/lib/maps", exe);
    if (access(probe, R_OK) == 0 && chdir(exe) != 0)
        perror("chdir");
}
static char path_bufs[16][PATH_MAX];
static int path_buf_idx = 0;

char *Conf_datadir(void)
{
    if (datadir[0] == '\0') {
        const char *env = getenv("XPILOT_DATADIR");
        if (env && access(env, R_OK) == 0) {
            snprintf(datadir, sizeof(datadir), "%s", env);
        } else if (access("lib/maps", R_OK) == 0) {
            snprintf(datadir, sizeof(datadir), "lib/");
        } else if (access("../lib/maps", R_OK) == 0) {
            snprintf(datadir, sizeof(datadir), "../lib/");
        } else {
            snprintf(datadir, sizeof(datadir), "%s", CONF_DATADIR);
        }
        size_t len = strlen(datadir);
        if (len > 0 && datadir[len - 1] != '/' && len < sizeof(datadir) - 1) {
            datadir[len] = '/';
            datadir[len + 1] = '\0';
        }
    }
    return datadir;
}

static char *build_path(const char *sub)
{
    path_buf_idx = (path_buf_idx + 1) % 16;
    snprintf(path_bufs[path_buf_idx], PATH_MAX, "%s%s", Conf_datadir(), sub);
    return path_bufs[path_buf_idx];
}

char *Conf_defaults_file_name(void)
{
    return build_path("defaults.txt");
}

char *Conf_password_file_name(void)
{
    return build_path("password.txt");
}

#if 0
char *Conf_player_passwords_file_name(void)
{
    return build_path("player_passwords.txt");
}
#endif

char *Conf_mapdir(void)
{
    return build_path("maps/");
}

char *Conf_fontdir(void)
{
    return build_path("fonts/");
}

char *Conf_default_map(void)
{
    static char conf[] = CONF_DEFAULT_MAP;

    return conf;
}

char *Conf_servermotdfile(void)
{
    static char env[] = "XPILOTSERVERMOTD";
    char *filename;

    filename = getenv(env);
    if (filename == NULL)
        filename = build_path("servermotd.txt");

    return filename;
}

char *Conf_localmotdfile(void)
{
    return build_path("localmotd.txt");
}

char conf_logfile_string[] = CONF_LOGFILE;

char *Conf_logfile(void)
{
    return conf_logfile_string;
}

char *Conf_ship_file(void)
{
    return build_path("shipshapes.txt");
}

char *Conf_texturedir(void)
{
    return build_path("textures/");
}

char *Conf_localguru(void)
{
    static char conf[] = CONF_LOCALGURU;

    return conf;
}

char *Conf_robotfile(void)
{
    return build_path("robots.txt");
}

char *Conf_zcat_ext(void)
{
    static char conf[] = CONF_ZCAT_EXT;

    return conf;
}

char *Conf_zcat_format(void)
{
    static char conf[] = CONF_ZCAT_FORMAT;

    return conf;
}

char *Conf_sounddir(void)
{
    return build_path("sound/");
}

char *Conf_soundfile(void)
{
    return build_path("sound/sounds.txt");
}


void Conf_print(void)
{
    warn("============================================================");
    warn("VERSION                   = %s", VERSION);
    warn("PACKAGE                   = %s", PACKAGE);

#ifdef DBE
    warn("DBE");
#endif
#ifdef MBX
    warn("MBX");
#endif
#ifdef PLOCKSERVER
    warn("PLOCKSERVER");
#endif
#ifdef DEVELOPMENT
    warn("DEVELOPMENT");
#endif

    warn("Conf_localguru()          = %s", Conf_localguru());
    warn("Conf_datadir()            = %s", Conf_datadir());
    warn("Conf_defaults_file_name() = %s", Conf_defaults_file_name());
    warn("Conf_password_file_name() = %s", Conf_password_file_name());
    warn("Conf_mapdir()             = %s", Conf_mapdir());
    warn("Conf_default_map()        = %s", Conf_default_map());
    warn("Conf_servermotdfile()     = %s", Conf_servermotdfile());
    warn("Conf_robotfile()          = %s", Conf_robotfile());
    warn("Conf_logfile()            = %s", Conf_logfile());
    warn("Conf_localmotdfile()      = %s", Conf_localmotdfile());
    warn("Conf_ship_file()          = %s", Conf_ship_file());
    warn("Conf_texturedir()         = %s", Conf_texturedir());
    warn("Conf_fontdir()            = %s", Conf_fontdir());
    warn("Conf_sounddir()           = %s", Conf_sounddir());
    warn("Conf_soundfile()          = %s", Conf_soundfile());
    warn("Conf_zcat_ext()           = %s", Conf_zcat_ext());
    warn("Conf_zcat_format()        = %s", Conf_zcat_format());
    warn("============================================================");
}
