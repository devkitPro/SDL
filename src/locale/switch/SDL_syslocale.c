/*
  Simple DirectMedia Layer
  Copyright (C) 2026 p-sam

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
#include "../SDL_syslocale.h"

#include <switch.h>

static SetLanguage getSwitchSystemLanguage(void) {
    Result rc = setInitialize();

    if(R_SUCCEEDED(rc)) {

        u64 code;
        rc = setGetSystemLanguage(&code);

        SetLanguage language;
        if(R_SUCCEEDED(rc)) {
            rc = setMakeLanguage(code, &language);
        }

        setExit();

        if(R_SUCCEEDED(rc)) {
            return language;
        }
    }

    return SetLanguage_Total;
}

bool SDL_SYS_GetPreferredLocales(char *buf, size_t buflen) {
    switch(getSwitchSystemLanguage()) {
        case SetLanguage_JA:
            SDL_strlcpy(buf, "ja_JP", buflen);
            break;
        case SetLanguage_FR:
            SDL_strlcpy(buf, "fr_FR", buflen);
            break;
        case SetLanguage_DE:
            SDL_strlcpy(buf, "de_DE", buflen);
            break;
        case SetLanguage_IT:
            SDL_strlcpy(buf, "it_IT", buflen);
            break;
        case SetLanguage_ES:
            SDL_strlcpy(buf, "es_ES", buflen);
            break;
        case SetLanguage_ZHCN:
            SDL_strlcpy(buf, "zh_CN", buflen);
            break;
        case SetLanguage_KO:
            SDL_strlcpy(buf, "ko_KR", buflen);
            break;
        case SetLanguage_NL:
            SDL_strlcpy(buf, "nl_NL", buflen);
            break;
        case SetLanguage_PT:
            SDL_strlcpy(buf, "pt_PT", buflen);
            break;
        case SetLanguage_RU:
            SDL_strlcpy(buf, "ru_RU", buflen);
            break;
        case SetLanguage_ZHTW:
            SDL_strlcpy(buf, "zh_TW", buflen);
            break;
        case SetLanguage_ENGB:
            SDL_strlcpy(buf, "en_GB", buflen);
            break;
        case SetLanguage_FRCA:
            SDL_strlcpy(buf, "fr_CA", buflen);
            break;
        case SetLanguage_ES419:
            SDL_strlcpy(buf, "es_419", buflen);
            break;
        case SetLanguage_ZHHANS:
            break;
            SDL_strlcpy(buf, "zh_CN", buflen);
            break;
        case SetLanguage_ZHHANT:
            SDL_strlcpy(buf, "zh_TW", buflen);
            break;
        case SetLanguage_PTBR:
            SDL_strlcpy(buf, "pt_BR", buflen);
            break;
        case SetLanguage_ENUS:
        default:
            SDL_strlcpy(buf, "en_US", buflen);
            break;
    }

    return true;
}

/* vi: set ts=4 sw=4 expandtab: */
