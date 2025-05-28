/*
  Simple DirectMedia Layer
  Copyright (C) 2025 Daniel K. O. <dkosmari@gmail.com>

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

#include <array>
#include <memory>
#include <optional>
#include <utility>

#include "SDL_wiiu_swkbd.h"

#include <coreinit/filesystem.h>
#include <coreinit/mcp.h>
#include <coreinit/userconfig.h>
#include <nn/swkbd.h>

#include "SDL_log.h"
#include "SDL_stdinc.h"
#include "SDL_system.h"
#include "SDL_syswm.h"

#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"

using nn::swkbd::LanguageType;
using nn::swkbd::RegionType;

namespace
{

    namespace detail
    {
        template <typename T>
        struct SDL_Deleter
        {
            void
            operator()(T *ptr)
                const
            {
                if (ptr) {
                    std::destroy_at(ptr);
                    SDL_free(ptr);
                }
            }
        };

        template <typename T>
        using unique_ptr = std::unique_ptr<T, SDL_Deleter<T>>;

        template <typename T,
                  typename... Args>
        unique_ptr<T>
        make_unique(Args &&...args)
        {
            T *ptr = reinterpret_cast<T *>(SDL_malloc(sizeof(T)));
            if (!ptr)
                return {};
            std::construct_at(ptr, std::forward<Args>(args)...);
            return unique_ptr<T>{ ptr };
        }

        // Note: we allow null pointers for these.
        using raw_string = unique_ptr<char>;
        using raw_u8string = unique_ptr<char8_t>;
        using raw_u16string = unique_ptr<char16_t>;

        bool
        operator==(const raw_string &a,
                   const char *b)
        {
            if (a.get() == b)
                return true;
            if (!a.get() || !b)
                return false;
            return SDL_strcmp(a.get(), b) == 0;
        }

        // RAII class to keep the FS module initialized.
        struct FSLib
        {
            FSLib()

            {
                FSInit();
            }

            ~FSLib()

            {
                FSShutdown();
            }
        };
        std::optional<FSLib> fsLib;

        struct FSClientWrapper : FSClient
        {
            bool valid;

            FSClientWrapper()
            {
                auto status = FSAddClient(this, FS_ERROR_FLAG_ALL);
                valid = status == FS_STATUS_OK;
            }

            // disallow moving
            FSClientWrapper(FSClientWrapper &&) = delete;

            ~FSClientWrapper()

            {
                FSDelClient(this, FS_ERROR_FLAG_NONE);
            }
        };

        namespace create
        {
            // We cannot call nn::swkbd functions unless a keyboard is created, so we keep
            // track of it here.
            bool created = false;

            const nn::swkbd::CreateArg *customArg = nullptr;
            unique_ptr<FSClientWrapper> fsClient;
            unique_ptr<char> workMemory;
            std::optional<nn::swkbd::RegionType> region; // store region used to create keyboard

            void
            cleanup()
            {
                /*
                 * Free all memory allocated for the swkbd creation.
                 *
                 * Normally we'd reuse the memory in case we need to re-create it in
                 * another region. But if swkbd is manually disabled, we better actually
                 * free up all memory.
                 */
                region.reset();
                fsClient.reset();
                workMemory.reset();
            }

        } // namespace create

        namespace appear
        {

            const nn::swkbd::AppearArg *customArg = nullptr;
            nn::swkbd::AppearArg theArg;
            // Keep track of wich window has the swkbd.
            SDL_Window *window = nullptr;

            // keyboard config options
            nn::swkbd::KeyboardMode keyboardMode = nn::swkbd::KeyboardMode::Full;
            raw_u16string okText;
            bool showWordSuggestions = true;
            // TODO: control disabled inputs, needs to fix nn::swkbd::ConfigArg
            // input form options
            raw_u16string initialText;
            raw_u16string hintText;
            nn::swkbd::PasswordMode passwordMode = nn::swkbd::PasswordMode::Clear;
            bool highlightInitialText = false;
            bool showCopyPasteButtons = false;
            bool drawWiiPointer = true;

            void
            reset()
            {
                // Reset all customization after the keyboard is shown.
                keyboardMode = nn::swkbd::KeyboardMode::Full;
                okText.reset();
                showWordSuggestions = true;
                initialText.reset();
                hintText.reset();
                passwordMode = nn::swkbd::PasswordMode::Clear;
                highlightInitialText = false;
                showCopyPasteButtons = false;
                drawWiiPointer = true;
            }

        } // namespace appear

        bool enabled = true;

        raw_string swkbdLocale;

        nn::swkbd::ControllerInfo controllerInfo;

        VPADStatus vpad;
        std::array<KPADStatus, 4> kpad;

        SDL_SysWMmsg wmMsgStart;
        SDL_SysWMmsg wmMsgFinish;

        // return language, country pair
        std::pair<raw_string, raw_string>
        parse_locale(const raw_string &locale)
        {
            if (!locale)
                return {};
            raw_string language{ reinterpret_cast<char *>(SDL_calloc(3, 1)) };
            raw_string country{ reinterpret_cast<char *>(SDL_calloc(3, 1)) };
            int r = SDL_sscanf(locale.get(),
                               "%2[Ca-z]_%2[A-Z]",
                               language.get(),
                               country.get());
            if (r == 2)
                return { std::move(language), std::move(country) };
            if (r == 1)
                return { std::move(language), {} };
            return {};
        }

        std::optional<LanguageType>
        to_language(const raw_string &language,
                    const raw_string &country)
        {
            // two-letter codes, from ISO 639
            if (language == "ja")
                return LanguageType::Japanese;
            if (language == "en")
                return LanguageType::English;
            if (language == "fr")
                return LanguageType::French;
            if (language == "de")
                return LanguageType::German;
            if (language == "it")
                return LanguageType::Italian;
            if (language == "es")
                return LanguageType::Spanish;
#if 0
            // Chinese and Korean languages seem to crash the swkbd.
            if (language == "zh") {
                if (country == "TW")
                    return LanguageType::TraditionalChinese;
                if (country == "CN")
                    return LanguageType::SimplifiedChinese;
                return LanguageType::TraditionalChinese;
            }
            if (language == "ko")
                return LanguageType::Korean;
#endif
            if (language == "nl")
                return LanguageType::Dutch;
            if (language == "pt")
                return LanguageType::Portuguese;
            if (language == "ru")
                return LanguageType::Russian;
            return {};
        }

        std::optional<RegionType>
        to_region(const raw_string &language,
                  const raw_string &country)
        {
            static const std::array usa_countries = {
                "US",
                "CA",
                "MX",
                "BR",
            };
            static const std::array eur_countries = {
                "DE",
                "ES",
                "FR",
                "GB",
                "IT",
                "NL",
                "PT",
                "RU",
            };
            static const std::array eur_languages = {
                "de",
                "es",
                "fr",
                "it",
                "nl",
                "pt",
                "ru",
            };

            if (country == "JP")
                return RegionType::Japan;

            for (auto c : usa_countries)
                if (country == c)
                    return RegionType::USA;

            for (auto c : eur_countries)
                if (country == c)
                    return RegionType::Europe;

#if 0
            // China, Korea and Taiwan seem to crash the swkbd.
            if (country == "CN")
                return RegionType::China;

            if (country == "KR" || country == "KP")
                return RegionType::Korea;

            if (country == "TW")
                return RegionType::Taiwan;
#endif

            // If country doesn't match, return a compatible region based on language alone
            if (language == "ja")
                return RegionType::Japan;
            if (language == "en")
                return RegionType::USA;
            for (auto lang : eur_languages)
                if (language == lang)
                    return RegionType::Europe;
#if 0
            // China and Korea seem to crash the swkbd.
            if (language == "zh")
                return RegionType::China;
            if (language == "ko")
                return RegionType::Korea;
#endif

            return {};
        }

        Uint32
        to_keyboard_layout(LanguageType language,
                           RegionType region)
        {
            switch (language) {
            case LanguageType::Japanese:
                return 0;

            case LanguageType::English:
                if (region == RegionType::USA)
                    return 1;
                else
                    return 5;

            case LanguageType::French:
                if (region == RegionType::USA)
                    return 2;
                else
                    return 6;

            case LanguageType::German:
                return 7;

            case LanguageType::Italian:
                return 8;

            case LanguageType::Spanish:
                if (region == RegionType::USA)
                    return 3;
                else
                    return 9;

            case LanguageType::Dutch:
                return 10;

            case LanguageType::Portuguese:
                if (region == RegionType::USA)
                    return 4;
                else
                    return 11;

            case LanguageType::Russian:
                return 12;

            default:
                return 19;
            }
        }

        Uint32
        read_system_config_u32(const char *key)
        {
            UCHandle handle = UCOpen();
            if (handle < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                             "UCOpen() returned: %d\n", handle);
                return -1;
            }
            unsigned result;
            alignas(0x40) UCSysConfig arg{};
            SDL_strlcpy(arg.name, key, sizeof arg.name);
            arg.dataType = UC_DATATYPE_UNSIGNED_INT;
            arg.dataSize = sizeof result;
            arg.data = &result;
            auto status = UCReadSysConfig(handle, 1, &arg);
            UCClose(handle);
            if (status == UC_ERROR_OK)
                return result;
            else
                return -1;
        }

        LanguageType
        read_system_language()
        {
            auto lang = read_system_config_u32("cafe.language");
            if (lang <= 11)
                return static_cast<LanguageType>(lang);
            return LanguageType::English;
        }

        std::optional<LanguageType> cached_system_language;

        LanguageType
        get_language_from_system()
        {
            if (!cached_system_language)
                cached_system_language = read_system_language();
            return *cached_system_language;
        }

        RegionType
        read_system_region()
        {
            alignas(64) MCPSysProdSettings settings{};
            MCPError status = 0;
            int handle = MCP_Open();
            if (handle < 0)
                goto error;
            status = MCP_GetSysProdSettings(handle, &settings);
            MCP_Close(handle);
            if (status)
                goto error;

            if (settings.product_area & MCP_REGION_JAPAN)
                return RegionType::Japan;
            if (settings.product_area & MCP_REGION_USA)
                return RegionType::USA;
            if (settings.product_area & MCP_REGION_EUROPE)
                return RegionType::Europe;
            if (settings.product_area & MCP_REGION_CHINA)
                return RegionType::China;
            if (settings.product_area & MCP_REGION_KOREA)
                return RegionType::Korea;
            if (settings.product_area & MCP_REGION_TAIWAN)
                return RegionType::Taiwan;

        error:
            return RegionType::Europe;
        }

        std::optional<RegionType> cached_system_region;

        RegionType
        get_region_from_system()
        {
            if (!cached_system_region)
                cached_system_region = read_system_region();
            return *cached_system_region;
        }

        std::size_t
        strlen_16(const char16_t *s)
        {
            if (!s)
                return 0;
            std::size_t result = 0;
            while (*s++)
                ++result;
            return result;
        }

        raw_u8string
        to_utf8(const char16_t *input)
        {
            if (!input)
                return {};
            auto output = SDL_iconv_string("UTF-8",
                                           "UTF-16BE",
                                           reinterpret_cast<const char *>(input),
                                           2 * (strlen_16(input) + 1));
            return raw_u8string{ reinterpret_cast<char8_t *>(output) };
        }

        raw_u16string
        to_utf16(const char *input)
        {
            if (!input)
                return {};
            auto output = SDL_iconv_string("UTF-16BE",
                                           "UTF-8",
                                           input,
                                           SDL_strlen(input) + 1);
            return raw_u16string{ reinterpret_cast<char16_t *>(output) };
        }

    } // namespace detail
} // namespace

void WIIU_SWKBD_Initialize(void)
{
    if (detail::create::created)
        return;

    if (!detail::enabled)
        return;

    if (!detail::fsLib)
        detail::fsLib.emplace();

    nn::swkbd::CreateArg arg;

    if (detail::create::customArg) {
        arg = *detail::create::customArg;
    } else {
        auto [language, country] = detail::parse_locale(detail::swkbdLocale);
        if (auto region = detail::to_region(language, country))
            arg.regionType = *region;
        else
            arg.regionType = detail::get_region_from_system();
    }

    auto local_fsClient = std::move(detail::create::fsClient);
    if (!arg.fsClient) {
        if (!local_fsClient)
            local_fsClient = detail::make_unique<detail::FSClientWrapper>();
        if (!local_fsClient) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Could not create FSClientfor nn::swkbd\n");
            return;
        }
        arg.fsClient = local_fsClient.get();
    } else {
        // user provided their own fsClient, so destroy the internal one
        local_fsClient.reset();
    }

    auto local_workMemory = std::move(detail::create::workMemory);
    if (!arg.workMemory) {
        local_workMemory.reset(reinterpret_cast<char *>(SDL_malloc(nn::swkbd::GetWorkMemorySize(0))));
        if (!local_workMemory) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Could not allocate memory for nn::swkbd\n");
            return;
        }
        arg.workMemory = local_workMemory.get();
    } else {
        // user provided their own workMemory, so destroy the internal one
        local_workMemory.reset();
    }

    if (!nn::swkbd::Create(arg)) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "nn::swkbd::Create() failed\n");
        return;
    }

    detail::create::workMemory = std::move(local_workMemory);
    detail::create::fsClient = std::move(local_fsClient);
    detail::create::region = arg.regionType;
    detail::create::created = true;
}

__attribute__((__destructor__)) void WIIU_SWKBD_Finalize(void)
{
    if (!detail::create::created)
        return;

    nn::swkbd::Destroy();
    detail::appear::window = nullptr;
    detail::create::region.reset();
    detail::create::created = false;
}

void WIIU_SWKBD_Calc(void)
{
    if (!detail::create::created)
        return;

    if (!detail::enabled)
        return;

    nn::swkbd::Calc(detail::controllerInfo);
    detail::controllerInfo = {};

    // TODO: these could go into a background thread
    if (nn::swkbd::IsNeedCalcSubThreadFont())
        nn::swkbd::CalcSubThreadFont();
    if (nn::swkbd::IsNeedCalcSubThreadPredict())
        nn::swkbd::CalcSubThreadPredict();

    if (detail::appear::window) {
        nn::swkbd::State state = nn::swkbd::GetStateInputForm();
        if (state == nn::swkbd::State::Hidden)
            detail::appear::window = nullptr;
    }

    // Check if user confirmed input.
    if (nn::swkbd::IsDecideOkButton(nullptr)) {
        // Send an event before we send out the string.
        SDL_VERSION(&detail::wmMsgStart.version);
        detail::wmMsgStart.subsystem = SDL_SYSWM_WIIU;
        detail::wmMsgStart.msg.wiiu.event = SDL_WIIU_SYSWM_SWKBD_OK_START_EVENT;
        SDL_SendSysWMEvent(&detail::wmMsgStart);

        auto str16 = nn::swkbd::GetInputFormString();
        if (str16) {
            auto str8 = detail::to_utf8(str16);
            if (str8)
                SDL_SendKeyboardText(reinterpret_cast<const char *>(str8.get()));
            else
                SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "could not convert utf-16 to utf-8\n");
        }

        // Send an event after the string.
        SDL_VERSION(&detail::wmMsgFinish.version);
        detail::wmMsgFinish.subsystem = SDL_SYSWM_WIIU;
        detail::wmMsgFinish.msg.wiiu.event = SDL_WIIU_SYSWM_SWKBD_OK_FINISH_EVENT;
        SDL_SendSysWMEvent(&detail::wmMsgFinish);

        // WIIU_SWKBD_HideScreenKeyboard(nullptr, nullptr);
    }

    if (nn::swkbd::IsDecideCancelButton(nullptr)) {
        WIIU_SWKBD_HideScreenKeyboard(nullptr, nullptr);
        SDL_VERSION(&detail::wmMsgFinish.version);
        detail::wmMsgFinish.subsystem = SDL_SYSWM_WIIU;
        detail::wmMsgFinish.msg.wiiu.event = SDL_WIIU_SYSWM_SWKBD_CANCEL_EVENT;
        SDL_SendSysWMEvent(&detail::wmMsgFinish);

        // WIIU_SWKBD_HideScreenKeyboard(nullptr, nullptr);
    }
}

void WIIU_SWKBD_Draw(SDL_Window *window)
{
    if (window != detail::appear::window)
        return;

    if (!detail::enabled)
        return;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state == nn::swkbd::State::Hidden)
        return;

    if (window->flags & SDL_WINDOW_WIIU_TV_ONLY)
        nn::swkbd::DrawTV();
    else
        nn::swkbd::DrawDRC();
}

SDL_bool WIIU_SWKBD_HasScreenKeyboardSupport(_THIS)
{
    if (!detail::enabled)
        return SDL_FALSE;
    return SDL_TRUE;
}

void WIIU_SWKBD_ShowScreenKeyboard(_THIS, SDL_Window *window)
{
    if (!detail::enabled)
        return;

    WIIU_SWKBD_Initialize();

    if (!detail::appear::window)
        detail::appear::window = window;

    const nn::swkbd::AppearArg *parg;
    if (detail::appear::customArg) {
        parg = detail::appear::customArg;
    } else {
        nn::swkbd::AppearArg &arg = detail::appear::theArg;
        arg = nn::swkbd::AppearArg{}; // reset all values to default
        // Set language
        auto [language, country] = detail::parse_locale(detail::swkbdLocale);
        if (auto lang = detail::to_language(language, country))
            arg.keyboardArg.configArg.languageType = *lang;
        else
            arg.keyboardArg.configArg.languageType = detail::get_language_from_system();

        // set keyboard layout according to language
        // TODO: fix wut: it's the unk_0x10 field
        arg.keyboardArg.configArg.unk_0x10 = detail::to_keyboard_layout(arg.keyboardArg.configArg.languageType,
                                                                        detail::create::region.value_or(nn::swkbd::RegionType::Europe));

        // Set keyboard mode
        arg.keyboardArg.configArg.keyboardMode = detail::appear::keyboardMode;

        // Set OK text
        if (detail::appear::okText)
            arg.keyboardArg.configArg.okString = detail::appear::okText.get();

        // Set word suggestions
        arg.keyboardArg.configArg.showWordSuggestions = detail::appear::showWordSuggestions;

        // Set show Wii pointer
        arg.keyboardArg.configArg.drawSysWiiPointer = detail::appear::drawWiiPointer;

        // Set initial text
        if (detail::appear::initialText)
            arg.inputFormArg.initialText = detail::appear::initialText.get();

        // Set hint text
        if (detail::appear::hintText)
            arg.inputFormArg.hintText = detail::appear::hintText.get();

        // Set password mode
        arg.inputFormArg.passwordMode = detail::appear::passwordMode;

        // Set highlight initial text
        // Note the typo, must fix this after we fix WUT
        arg.inputFormArg.higlightInitialText = detail::appear::highlightInitialText;

        // Set show copy paste buttons
        arg.inputFormArg.showCopyPasteButtons = detail::appear::showCopyPasteButtons;

        if (window->flags & SDL_WINDOW_WIIU_TV_ONLY)
            arg.keyboardArg.configArg.controllerType = nn::swkbd::ControllerType::WiiRemote0;
        else
            arg.keyboardArg.configArg.controllerType = nn::swkbd::ControllerType::DrcGamepad;

        parg = &arg;
    }

    nn::swkbd::AppearInputForm(*parg);
    detail::appear::reset();
}

void WIIU_SWKBD_HideScreenKeyboard(_THIS, SDL_Window *)
{
    if (!detail::create::created)
        return;
    nn::swkbd::DisappearInputForm();
    // detail::appear::window = nullptr;
}

SDL_bool WIIU_SWKBD_IsScreenKeyboardShown(_THIS, SDL_Window *window)
{
    if (!detail::create::created)
        return SDL_FALSE;

    if (window != detail::appear::window)
        return SDL_FALSE;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state != nn::swkbd::State::Hidden)
        return SDL_TRUE;

    return SDL_FALSE;
}

void SDL_WiiUSetSWKBDEnabled(SDL_bool enabled)
{
    if (detail::enabled != !!enabled) {
        detail::enabled = enabled;
        if (!detail::enabled) {
            // If application is turning swkbd off, we better free up all memory too.
            WIIU_SWKBD_Finalize();
            detail::create::cleanup();
        }
    }
}

void SDL_WiiUSetSWKBDCreateArg(void *arg)
{
    detail::create::customArg = reinterpret_cast<const nn::swkbd::CreateArg *>(arg);
    // force swkbd to be created again next time it's shown
    WIIU_SWKBD_Finalize();
}

void SDL_WiiUSetSWKBDAppearArg(const void *arg)
{
    detail::appear::customArg = reinterpret_cast<const nn::swkbd::AppearArg *>(arg);
}

void SDL_WiiUSetSWKBDKeyboardMode(SDL_WiiUSWKBDKeyboardMode mode)
{
    switch (mode) {
    case SDL_WIIU_SWKBD_KEYBOARD_MODE_FULL:
        detail::appear::keyboardMode = nn::swkbd::KeyboardMode::Full;
        break;
    case SDL_WIIU_SWKBD_KEYBOARD_MODE_NUMPAD:
        detail::appear::keyboardMode = nn::swkbd::KeyboardMode::Numpad;
        break;
    case SDL_WIIU_SWKBD_KEYBOARD_MODE_RESTRICTED:
        detail::appear::keyboardMode = nn::swkbd::KeyboardMode::Utf8;
        break;
    case SDL_WIIU_SWKBD_KEYBOARD_MODE_NNID:
        detail::appear::keyboardMode = nn::swkbd::KeyboardMode::NNID;
        break;
    default:
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                     "set swkbd keyboard mode failed: invalid mode %d\n", mode);
    }
}

void SDL_WiiUSetSWKBDOKLabel(const char *label)
{
    detail::appear::okText = detail::to_utf16(label);
}

void SDL_WiiUSetSWKBDShowWordSuggestions(SDL_bool show)
{
    detail::appear::showWordSuggestions = show;
}

void SDL_WiiUSetSWKBDInitialText(const char *text)
{
    detail::appear::initialText = detail::to_utf16(text);
}

void SDL_WiiUSetSWKBDHintText(const char *text)
{
    detail::appear::hintText = detail::to_utf16(text);
}

void SDL_WiiUSetSWKBDPasswordMode(SDL_WiiUSWKBDPasswordMode mode)
{
    switch (mode) {
    case SDL_WIIU_SWKBD_PASSWORD_MODE_SHOW:
        detail::appear::passwordMode = nn::swkbd::PasswordMode::Clear;
        break;
    case SDL_WIIU_SWKBD_PASSWORD_MODE_HIDE:
        detail::appear::passwordMode = nn::swkbd::PasswordMode::Hide;
        break;
    case SDL_WIIU_SWKBD_PASSWORD_MODE_FADE:
        detail::appear::passwordMode = nn::swkbd::PasswordMode::Fade;
        break;
    default:
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                     "set swkbd password failed: invalid mode %d\n", mode);
    }
}

void SDL_WiiUSetSWKBDHighlightInitialText(SDL_bool highlight)
{
    detail::appear::highlightInitialText = highlight;
}

void SDL_WiiUSetSWKBDShowCopyPasteButtons(SDL_bool show)
{
    detail::appear::showCopyPasteButtons = show;
}

void SDL_WiiUSetSWKBDDrawWiiPointer(SDL_bool draw)
{
    detail::appear::drawWiiPointer = draw;
}

void SDL_WiiUSetSWKBDLocale(const char *locale)
{
    // Don't do anything if the locale didn't change.
    if (detail::swkbdLocale == locale)
        return;
    WIIU_SWKBD_Finalize();
    if (locale)
        detail::swkbdLocale.reset(SDL_strdup(locale));
    else
        detail::swkbdLocale.reset();
}

SDL_bool SDL_WiiUSetSWKBDVPAD(const void *vpad)
{
    if (!detail::create::created)
        return SDL_FALSE;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state != nn::swkbd::State::Visible)
        return SDL_FALSE;

    // printf("swkbd is consuming vpad input, window=%p\n", detail::appear::window);
    detail::vpad = *reinterpret_cast<const VPADStatus *>(vpad);
    detail::controllerInfo.vpad = &detail::vpad;
    return SDL_TRUE;
}

SDL_bool SDL_WiiUSetSWKBDKPAD(int channel, const void *kpad)
{
    if (!detail::create::created)
        return SDL_FALSE;

    if (channel < 0 || channel > 3)
        return SDL_FALSE;

    nn::swkbd::State state = nn::swkbd::GetStateInputForm();
    if (state != nn::swkbd::State::Visible)
        return SDL_FALSE;

    // printf("swkbd is consuming kpad input\n");
    detail::kpad[channel] = *reinterpret_cast<const KPADStatus *>(kpad);
    detail::controllerInfo.kpad[channel] = &detail::kpad[channel];
    return SDL_TRUE;
}
