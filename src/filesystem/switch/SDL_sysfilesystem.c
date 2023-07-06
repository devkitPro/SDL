/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

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
#include "../../SDL_internal.h"

#ifdef SDL_FILESYSTEM_SWITCH

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* System dependent filesystem routines                                */

#include <limits.h>
#include <fcntl.h>
#include <sys/unistd.h>

#include "SDL_error.h"
#include "SDL_stdinc.h"
#include "SDL_filesystem.h"

char *
SDL_GetBasePath(void)
{
    const char *basepath = "romfs:/";
    char *retval = SDL_strdup(basepath);
    return retval;
}

char *
SDL_GetPrefPath(const char *org, const char *app)
{
    char *ret = NULL;
    char buf[PATH_MAX];
    size_t len;

    if (getcwd(buf, PATH_MAX)) {
        len = strlen(buf) + 2;
        ret = (char *) SDL_malloc(len);
        if (!ret) {
            SDL_OutOfMemory();
            return NULL;
        }
        SDL_snprintf(ret, len, "%s/", buf);
        return ret;
    }

    return NULL;
}

#endif /* SDL_FILESYSTEM_SWITCH */

/* vi: set ts=4 sw=4 expandtab: */
