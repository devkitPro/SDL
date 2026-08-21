/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#ifdef SDL_FILESYSTEM_OGC

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* System dependent filesystem routines                                */

#include "../SDL_sysfilesystem.h"

#include <dirent.h>
#include <errno.h>
#include <unistd.h>

static inline int create_dir(const char *dirname)
{
    int result = mkdir(dirname, 0666);

    if (result == -1 && errno != EEXIST) {
        return SDL_SetError("Failed to create '%s' (%s)", dirname, strerror(errno));
    }
    return 0;
}

char *SDL_SYS_GetBasePath(void)
{
    char buffer[256];
    size_t len;

    if (!getcwd(buffer, sizeof(buffer) - 1)) {
        return "/";
    }

    len = strlen(buffer);
    if (len > 0 && buffer[len - 1] != '/') {
        buffer[len] = '/';
        buffer[len + 1] = '\0';
    }

    return SDL_strdup(buffer);
}

char *SDL_SYS_GetExeName(void)
{
    return NULL;  // there isn't an "exe name" on this platform.
}

char *SDL_SYS_GetPrefPath(const char *org, const char *app)
{
    char *pref_path = NULL;
    if (!app) {
        SDL_InvalidParamError("app");
        return NULL;
    }

    SDL_asprintf(&pref_path, "/apps/%s/", app);
    if (!pref_path) {
        return NULL;
    }

    if (create_dir(pref_path) < 0) {
        SDL_free(pref_path);
        return NULL;
    }

    return pref_path;
}

// TODO
char *SDL_SYS_GetUserFolder(SDL_Folder folder)
{
    SDL_Unsupported();
    return NULL;
}

#endif /* SDL_FILESYSTEM_OGC */
