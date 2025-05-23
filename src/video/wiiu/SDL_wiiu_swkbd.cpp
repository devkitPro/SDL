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
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

#include "SDL_cpp_allocator.h"

using nn::swkbd::LanguageType;
using nn::swkbd::RegionType;

namespace
{

    namespace detail
    {

        // Make sure strings are allocated with SDL_malloc()
        template <typename T>
        using basic_string = std::basic_string<T, std::char_traits<T>, sdl::allocator<T>>;

        using string = basic_string<char>;
        using u8string = basic_string<char8_t>;
        using u16string = basic_string<char16_t>;

        // We use vector to store the work memory
        template <typename T>
        using vector = std::vector<T, sdl::allocator<T>>;

        template <typename T>
        struct SDL_Deleter
        {
            void
            operator()(T *ptr)
                const
            {
                if (ptr) {
                    ptr->~T();
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
                throw std::bad_alloc{};
            try {
                new (ptr) T(std::forward<Args>(args)...);
            }
            catch (...) {
                SDL_free(ptr);
                throw;
            }
            return unique_ptr<T>{ ptr };
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

            FSClientWrapper()
            {
                auto status = FSAddClient(this, FS_ERROR_FLAG_ALL);
                if (status != FS_STATUS_OK)
                    throw std::runtime_error{ "FSAddClient() failed" };
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
            vector<char> workMemory;
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
                 *
                 * Only call this after WIIU_SWKBD_Finalize().
                 */
                region.reset();
                fsClient.reset();
                workMemory.clear();
                workMemory.shrink_to_fit();
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
            u16string okText;
            bool showWordSuggestions = true;
            // TODO: control disabled inputs, needs to fix nn::swkbd::ConfigArg
            // input form options
            u16string initialText;
            u16string hintText;
            nn::swkbd::PasswordMode passwordMode = nn::swkbd::PasswordMode::Clear;
            bool highlightInitialText = false;
            bool showCopyPasteButtons = false;
            bool drawWiiPointer = true;

            void
            reset()
            {
                // Reset all customization after the keyboard is shown.
                keyboardMode = nn::swkbd::KeyboardMode::Full;
                okText.clear();
                showWordSuggestions = true;
                initialText.clear();
                hintText.clear();
                passwordMode = nn::swkbd::PasswordMode::Clear;
                highlightInitialText = false;
                showCopyPasteButtons = false;
                drawWiiPointer = true;
            }

        } // namespace appear

        bool enabled = true;

        string swkbdLocale;

        nn::swkbd::ControllerInfo controllerInfo;

        VPADStatus vpad;
        std::array<KPADStatus, 4> kpad;

        SDL_SysWMmsg wmMsgStart;
        SDL_SysWMmsg wmMsgFinish;

        // return language, country pair
        std::pair<string, string>
        parse_locale(const string &locale)
        {
            if (locale.empty())
                return {};
            char language[3];
            char country[3];
            int r = SDL_sscanf(locale.data(), "%2[Ca-z]_%2[A-Z]", language, country);
            if (r == 2)
                return { language, country };
            if (r == 1)
                return { language, {} };
            return {};
        }

        std::optional<LanguageType>
        to_language(const string &language,
                    const string &country)
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
        to_region(const string &language,
                  const string &country)
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
        read_system_config_u32(const char *key) noexcept
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
        read_system_language() noexcept
        {
            auto lang = read_system_config_u32("cafe.language");
            if (lang <= 11)
                return static_cast<LanguageType>(lang);
            return LanguageType::English;
        }

        LanguageType
        get_language_from_system() noexcept
        {
            static LanguageType cached = read_system_language();
            return cached;
        }

        RegionType
        read_system_region() noexcept
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

        RegionType
        get_region_from_system() noexcept
        {
            static RegionType cached = read_system_region();
            return cached;
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

        u8string
        to_utf8(const char16_t *input)
        {
            auto output = SDL_iconv_string("UTF-8",
                                           "UTF-16BE",
                                           reinterpret_cast<const char *>(input),
                                           2 * (strlen_16(input) + 1));
            if (!output)
                throw std::runtime_error{ "SDL_iconv_string() failed" };

            try {
                u8string result(reinterpret_cast<char8_t *>(output));
                SDL_free(output);
                return result;
            }
            catch (...) {
                SDL_free(output);
                throw;
            }
        }

        u16string
        to_utf16(const char *input)
        {
            auto output = SDL_iconv_string("UTF-16BE",
                                           "UTF-8",
                                           input,
                                           SDL_strlen(input) + 1);
            if (!output)
                throw std::runtime_error{ "SDL_iconv_string() failed" };
            try {
                u16string result(reinterpret_cast<char16_t *>(output));
                SDL_free(output);
                return result;
            }
            catch (...) {
                SDL_free(output);
                throw;
            }
        }

    } // namespace detail
} // namespace

void WIIU_SWKBD_Initialize(void)
{
    if (detail::create::created)
        return;

    if (!detail::enabled)
        return;

    try {
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

        detail::unique_ptr<detail::FSClientWrapper> local_fsClient = std::move(detail::create::fsClient);
        if (!arg.fsClient) {
            if (!local_fsClient)
                local_fsClient = detail::make_unique<detail::FSClientWrapper>();
            arg.fsClient = local_fsClient.get();
        } else {
            // user provided their own fsClient, so destroy the internal one
            local_fsClient.reset();
        }

        detail::vector<char> local_workMemory = std::move(detail::create::workMemory);
        if (!arg.workMemory) {
            local_workMemory.resize(nn::swkbd::GetWorkMemorySize(0));
            local_workMemory.shrink_to_fit();
            arg.workMemory = local_workMemory.data();
        } else {
            // user provided their own workMemory, so destroy the internal one
            local_workMemory.clear();
            local_workMemory.shrink_to_fit();
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
    catch (std::exception &e) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "WIIU_SWKBD_Initialize() failed: %s\n", e.what());
    }
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
            try {
                auto str8 = detail::to_utf8(str16);
                SDL_SendKeyboardText(reinterpret_cast<const char *>(str8.data()));
            }
            catch (std::exception &e) {
                SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
                             "could not convert utf-16 to utf-8: %s\n", e.what());
            }
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
        if (!detail::appear::okText.empty())
            arg.keyboardArg.configArg.okString = detail::appear::okText.data();

        // Set word suggestions
        arg.keyboardArg.configArg.showWordSuggestions = detail::appear::showWordSuggestions;

        // Set show Wii pointer
        arg.keyboardArg.configArg.drawSysWiiPointer = detail::appear::drawWiiPointer;

        // Set initial text
        if (!detail::appear::initialText.empty())
            arg.inputFormArg.initialText = detail::appear::initialText.data();

        // Set hint text
        if (!detail::appear::hintText.empty())
            arg.inputFormArg.hintText = detail::appear::hintText.data();

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
    try {
        if (label)
            detail::appear::okText = detail::to_utf16(label);
        else
            detail::appear::okText.clear();
    }
    catch (std::exception &e) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "set swkbd OK label failed: %s\n", e.what());
    }
}

void SDL_WiiUSetSWKBDShowWordSuggestions(SDL_bool show)
{
    detail::appear::showWordSuggestions = show;
}

void SDL_WiiUSetSWKBDInitialText(const char *text)
{
    try {
        if (text)
            detail::appear::initialText = detail::to_utf16(text);
        else
            detail::appear::initialText.clear();
    }
    catch (std::exception &e) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "set swkbd initial text failed: %s\n", e.what());
    }
}

void SDL_WiiUSetSWKBDHintText(const char *text)
{
    try {
        if (text)
            detail::appear::hintText = detail::to_utf16(text);
        else
            detail::appear::hintText.clear();
    }
    catch (std::exception &e) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "set swkbd hint text failed: %s\n", e.what());
    }
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
    if (locale) {
        if (locale == detail::swkbdLocale)
            return;
    } else {
        if (!locale && detail::swkbdLocale.empty())
            return;
    }
    WIIU_SWKBD_Finalize();
    if (locale)
        detail::swkbdLocale = locale;
    else
        detail::swkbdLocale.clear();
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
