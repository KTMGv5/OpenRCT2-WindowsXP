/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef _WIN32

    // Windows.h needs to be included first
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
// clang-format off
    #include <windows.h>
    #include <datetimeapi.h>
    #include <lmcons.h>
    #include <memory>
    #include <shlobj.h>
    #include <shlwapi.h>
    #include <mmsystem.h>
    // clang-format on
    #undef GetEnvironmentVariable
    #undef small

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    #include <cpuid.h>
    #define OpenRCT2_CPUID_GNUC_X86
#elif defined(_MSC_VER) && (_MSC_VER >= 1500) && (defined(_M_X64) || defined(_M_IX86))
    #include <intrin.h>
    #define OpenRCT2_CPUID_MSVC_X86
#endif

    #include "Platform.h"

    #include "../Date.h"
    #include "../Diagnostic.h"
    #include "../OpenRCT2.h"
    #include "../Version.h"
    #include "../core/File.h"
    #include "../core/Path.hpp"
    #include "../core/String.hpp"
    #include "../drawing/Font.h"
    #include "../localisation/Language.h"

    #include <cassert>
    #include <cstring>
    #include <format>
    #include <iterator>
    #include <locale>

    // Native resource IDs
    #include "../../../resources/resource.h"

    // Enable visual styles
    #pragma comment(                                                                                                           \
        linker,                                                                                                                \
        "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
// The name of the mutex used to prevent multiple instances of the game from running
static constexpr wchar_t kSingleInstanceMutexName[] = L"RollerCoaster Tycoon 2_GSKMUTEX";

    #define SOFTWARE_CLASSES L"Software\\Classes"
    #define MUI_CACHE L"Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache"

namespace OpenRCT2::Platform
{
    static std::string WIN32_GetKnownFolderPath(REFKNOWNFOLDERID rfid);
    static std::wstring WIN32_GetModuleFileNameW(HMODULE hModule);
    static u8string WIN32_GetModuleFileNameUTF8(HMODULE hModule);

    std::string GetEnvironmentVariable(std::string_view name)
    {
        std::wstring result;
        auto wname = String::toWideChar(name);
        wchar_t wvalue[256];
        auto valueSize = GetEnvironmentVariableW(wname.c_str(), wvalue, static_cast<DWORD>(std::size(wvalue)));
        if (valueSize < std::size(wvalue))
        {
            result = wvalue;
        }
        else
        {
            const auto wBuffer = std::make_unique_for_overwrite<wchar_t[]>(valueSize);
            GetEnvironmentVariableW(wname.c_str(), wBuffer.get(), valueSize);
            result = wBuffer.get();
        }
        return String::toUtf8(result);
    }

    static std::string GetHomePathViaEnvironment()
    {
        std::string result;
        auto homedrive = GetEnvironmentVariable("HOMEDRIVE");
        auto homepath = GetEnvironmentVariable("HOMEPATH");
        if (!homedrive.empty() && !homepath.empty())
        {
            result = Path::Combine(homedrive, homepath);
        }
        return result;
    }

    std::string GetFolderPath(SpecialFolder folder)
    {
        switch (folder)
        {
            // We currently store everything under Documents/OpenRCT2
            case SpecialFolder::userCache:
            case SpecialFolder::userConfig:
            case SpecialFolder::userData:
            {
                auto path = WIN32_GetKnownFolderPath(FOLDERID_Documents);
                if (path.empty())
                {
                    path = GetFolderPath(SpecialFolder::userHome);
                }
                return path;
            }
            case SpecialFolder::userHome:
            {
                auto path = WIN32_GetKnownFolderPath(FOLDERID_Profile);
                if (path.empty())
                {
                    path = GetHomePathViaEnvironment();
                    if (path.empty())
                    {
                        path = "C:\\";
                    }
                }
                return path;
            }
            case SpecialFolder::rct2Discord:
            {
                auto path = WIN32_GetKnownFolderPath(FOLDERID_LocalAppData);
                if (!path.empty())
                {
                    path = Path::Combine(path, u8"DiscordGames\\RollerCoaster Tycoon 2 Triple Thrill Pack\\content\\Game");
                }
                return path;
            }
            default:
                return std::string();
        }
    }

    std::string GetCurrentExecutableDirectory()
    {
        auto exePath = GetCurrentExecutablePath();
        auto exeDirectory = Path::GetDirectory(exePath);
        return exeDirectory;
    }

    std::string GetInstallPath()
    {
        auto path = std::string(gCustomOpenRCT2DataPath);
        if (!path.empty())
        {
            path = Path::GetAbsolute(path);
        }
        else
        {
            auto exeDirectory = GetCurrentExecutableDirectory();
            path = Path::Combine(exeDirectory, u8"data");
        }
        return path;
    }

    std::string GetCurrentExecutablePath()
    {
        return WIN32_GetModuleFileNameUTF8(nullptr);
    }

    std::string GetDocsPath()
    {
        return GetCurrentExecutableDirectory();
    }

    static SYSTEMTIME TimeToSystemTime(std::time_t timestamp)
    {
        ULARGE_INTEGER time_value;
        time_value.QuadPart = (timestamp * 10000000LL) + 116444736000000000LL;

        FILETIME ft;
        ft.dwLowDateTime = time_value.LowPart;
        ft.dwHighDateTime = time_value.HighPart;

        SYSTEMTIME st;
        FileTimeToSystemTime(&ft, &st);
        return st;
    }

    std::string FormatShortDate(std::time_t timestamp)
    {
        SYSTEMTIME st = TimeToSystemTime(timestamp);
        std::string result;

        wchar_t date[20];
        ptrdiff_t charsWritten = GetDateFormatW(
            LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, static_cast<int>(std::size(date)));
        if (charsWritten != 0)
        {
            result = String::toUtf8(std::wstring_view(date, charsWritten - 1));
        }
        return result;
    }

    std::string FormatTime(std::time_t timestamp)
    {
        SYSTEMTIME st = TimeToSystemTime(timestamp);
        std::string result;

        wchar_t time[20];
        ptrdiff_t charsWritten = GetTimeFormatW(
            LOCALE_USER_DEFAULT, 0, &st, nullptr, time, static_cast<int>(std::size(time)));
        if (charsWritten != 0)
        {
            result = String::toUtf8(std::wstring_view(time, charsWritten - 1));
        }
        return result;
    }

    bool IsOSVersionAtLeast(uint32_t major, uint32_t minor, uint32_t build)
    {
        bool result = false;
        auto hModule = GetModuleHandleW(L"ntdll.dll");
        if (hModule != nullptr)
        {
            using RtlGetVersionPtr = long(WINAPI*)(PRTL_OSVERSIONINFOW);
    #if defined(__GNUC__) && __GNUC__ >= 8
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wcast-function-type"
    #endif
            auto fn = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(hModule, "RtlGetVersion"));
    #if defined(__GNUC__) && __GNUC__ >= 8
        #pragma GCC diagnostic pop
    #endif
            if (fn != nullptr)
            {
                RTL_OSVERSIONINFOW rovi{};
                rovi.dwOSVersionInfoSize = sizeof(rovi);
                if (fn(&rovi) == 0)
                {
                    if (rovi.dwMajorVersion > major
                        || (rovi.dwMajorVersion == major
                            && (rovi.dwMinorVersion > minor || (rovi.dwMinorVersion == minor && rovi.dwBuildNumber >= build))))
                    {
                        result = true;
                    }
                }
            }
        }
        return result;
    }

    bool IsRunningInWine()
    {
        HMODULE ntdllMod = GetModuleHandleW(L"ntdll.dll");

        if (ntdllMod && GetProcAddress(ntdllMod, "wine_get_version"))
        {
            return true;
        }
        return false;
    }

    /**
     * Checks if the current version of Windows supports ANSI colour codes.
     * From Windows 10, build 10586 ANSI escape colour codes can be used on stdout.
     */
    static bool HasANSIColourSupport()
    {
        return IsOSVersionAtLeast(10, 0, 10586);
    }

    static void EnableANSIConsole()
    {
        if (HasANSIColourSupport())
        {
            auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode;
            GetConsoleMode(handle, &mode);
            if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            {
                mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(handle, mode);
            }
        }
    }

    bool IsColourTerminalSupported()
    {
        static bool hasChecked = false;
        static bool isSupported = false;
        if (!hasChecked)
        {
            if (HasANSIColourSupport())
            {
                EnableANSIConsole();
                isSupported = true;
            }
            else
            {
                isSupported = false;
            }
            hasChecked = true;
        }
        return isSupported;
    }

    static std::string WIN32_GetKnownFolderPath(REFKNOWNFOLDERID rfid)
    {
        int csidl = -1;
        if (rfid == FOLDERID_Documents)
        {
            csidl = CSIDL_MYDOCUMENTS;
        }
        else if (rfid == FOLDERID_Fonts)
        {
            csidl = CSIDL_FONTS;
        }
        else if (rfid == FOLDERID_LocalAppData)
        {
            csidl = CSIDL_LOCAL_APPDATA;
        }
        else if (rfid == FOLDERID_Profile)
        {
            csidl = CSIDL_PROFILE;
        }

        if (csidl != -1)
        {
            wchar_t path[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, path)))
            {
                return String::toUtf8(path);
            }
        }
        return std::string();
    }

    static std::wstring WIN32_GetModuleFileNameW(HMODULE hModule)
    {
        uint32_t wExePathCapacity = 128;
        std::wstring exePath;

        uint32_t size;
        do
        {
            wExePathCapacity *= 2;
            exePath.resize(wExePathCapacity);
            size = GetModuleFileNameW(hModule, exePath.data(), wExePathCapacity);
        } while (size >= wExePathCapacity);
        exePath.resize(size);
        return exePath;
    }

    static u8string WIN32_GetModuleFileNameUTF8(HMODULE hModule)
    {
        return String::toUtf8(WIN32_GetModuleFileNameW(hModule));
    }

    u8string StrDecompToPrecomp(u8string_view input)
    {
        return u8string(input);
    }

    void SetUpFileAssociations()
    {
        // Setup file extensions
        SetUpFileAssociation(".park", "OpenRCT2 park (.park)", "Play", "\"%1\"", 0);
        SetUpFileAssociation(".sc4", "RCT1 Scenario (.sc4)", "Play", "\"%1\"", 0);
        SetUpFileAssociation(".sc6", "RCT2 Scenario (.sc6)", "Play", "\"%1\"", 0);
        SetUpFileAssociation(".sv4", "RCT1 Saved Game (.sc4)", "Play", "\"%1\"", 0);
        SetUpFileAssociation(".sv6", "RCT2 Saved Game (.sv6)", "Play", "\"%1\"", 0);
        SetUpFileAssociation(".sv7", "RCT Modified Saved Game (.sv7)", "Play", "\"%1\"", 0);
        SetUpFileAssociation(".sea", "RCTC Saved Game (.sea)", "Play", "\"%1\"", 0);
        SetUpFileAssociation(".td4", "RCT1 Track Design (.td4)", "Install", "\"%1\"", 0);
        SetUpFileAssociation(".td6", "RCT2 Track Design (.td6)", "Install", "\"%1\"", 0);
        SetUpFileAssociation(".td7", "OpenRCT2 Track Design (.td7)", "Install", "\"%1\"", 0);

        // Refresh explorer
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    static HMODULE _dllModule = nullptr;
    static HMODULE GetDLLModule()
    {
        if (_dllModule == nullptr)
        {
            _dllModule = GetModuleHandle(nullptr);
        }
        return _dllModule;
    }

    static std::wstring GetProdIDName(std::string_view extension)
    {
        auto progIdName = std::string(OPENRCT2_NAME) + std::string(extension);
        auto progIdNameW = String::toWideChar(progIdName);
        return progIdNameW;
    }

    bool SetUpFileAssociation(
        std::string_view extension, std::string_view fileTypeText, std::string_view commandText, std::string_view commandArgs,
        const uint32_t iconIndex)
    {
        const std::wstring& exePathW = WIN32_GetModuleFileNameW(nullptr);
        const std::wstring& dllPathW = WIN32_GetModuleFileNameW(GetDLLModule());

        auto extensionW = String::toWideChar(extension);
        auto fileTypeTextW = String::toWideChar(fileTypeText);
        auto commandTextW = String::toWideChar(commandText);
        auto commandArgsW = String::toWideChar(commandArgs);
        auto progIdNameW = GetProdIDName(extension);

        HKEY hKey = nullptr;
        HKEY hRootKey = nullptr;

        // [HKEY_CURRENT_USER\Software\Classes]
        if (RegOpenKeyW(HKEY_CURRENT_USER, SOFTWARE_CLASSES, &hRootKey) != ERROR_SUCCESS)
        {
            RegCloseKey(hRootKey);
            return false;
        }

        // [hRootKey\.ext]
        if (RegSetValueW(hRootKey, extensionW.c_str(), REG_SZ, progIdNameW.c_str(), 0) != ERROR_SUCCESS)
        {
            RegCloseKey(hRootKey);
            return false;
        }

        if (RegCreateKeyW(hRootKey, progIdNameW.c_str(), &hKey) != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            RegCloseKey(hRootKey);
            return false;
        }

        // [hRootKey\OpenRCT2.ext]
        if (RegSetValueW(hKey, nullptr, REG_SZ, fileTypeTextW.c_str(), 0) != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            RegCloseKey(hRootKey);
            return false;
        }
        // [hRootKey\OpenRCT2.ext\DefaultIcon]
        const std::wstring szIconW = std::format(L"\"{}\",{}", dllPathW, iconIndex);
        if (RegSetValueW(hKey, L"DefaultIcon", REG_SZ, szIconW.c_str(), 0) != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            RegCloseKey(hRootKey);
            return false;
        }

        // [hRootKey\OpenRCT2.sv6\shell]
        if (RegSetValueW(hKey, L"shell", REG_SZ, L"open", 0) != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            RegCloseKey(hRootKey);
            return false;
        }

        // [hRootKey\OpenRCT2.sv6\shell\open]
        if (RegSetValueW(hKey, L"shell\\open", REG_SZ, commandTextW.c_str(), 0) != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            RegCloseKey(hRootKey);
            return false;
        }

        // [hRootKey\OpenRCT2.sv6\shell\open\command]
        const std::wstring szCommandW = std::format(L"\"{}\" {}", exePathW, commandArgsW);
        if (RegSetValueW(hKey, L"shell\\open\\command", REG_SZ, szCommandW.c_str(), 0) != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            RegCloseKey(hRootKey);
            return false;
        }
        return true;
    }

    static void RemoveFileAssociation(const utf8* extension)
    {
        // [HKEY_CURRENT_USER\Software\Classes]
        HKEY hRootKey;
        if (RegOpenKeyW(HKEY_CURRENT_USER, SOFTWARE_CLASSES, &hRootKey) == ERROR_SUCCESS)
        {
            // [hRootKey\.ext]
            SHDeleteKeyW(hRootKey, String::toWideChar(extension).c_str());

            // [hRootKey\OpenRCT2.ext]
            auto progIdName = GetProdIDName(extension);
            SHDeleteKeyW(hRootKey, progIdName.c_str());

            RegCloseKey(hRootKey);
        }
    }

    void RemoveFileAssociations()
    {
        // Remove file extensions
        RemoveFileAssociation(".park");
        RemoveFileAssociation(".sc4");
        RemoveFileAssociation(".sc6");
        RemoveFileAssociation(".sv4");
        RemoveFileAssociation(".sv6");
        RemoveFileAssociation(".sv7");
        RemoveFileAssociation(".sea");
        RemoveFileAssociation(".td4");
        RemoveFileAssociation(".td6");
        RemoveFileAssociation(".td7");

        // Refresh explorer
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }

    bool HandleSpecialCommandLineArgument(const char* argument)
    {
        return false;
    }

    bool FindApp(std::string_view app, std::string* output)
    {
        LOG_WARNING("FindApp() not implemented for Windows!");
        return false;
    }

    int32_t Execute(const char* args[], std::string* output)
    {
        LOG_WARNING("Execute() not implemented for Windows!");
        return -1;
    }

    uint64_t GetLastModified(std::string_view path)
    {
        uint64_t lastModified = 0;
        auto pathW = String::toWideChar(path);
        auto hFile = CreateFileW(pathW.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            FILETIME ftCreate, ftAccess, ftWrite;
            if (GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite))
            {
                lastModified = (static_cast<uint64_t>(ftWrite.dwHighDateTime) << 32uLL)
                    | static_cast<uint64_t>(ftWrite.dwLowDateTime);
            }
            CloseHandle(hFile);
        }
        return lastModified;
    }

    uint64_t GetFileSize(std::string_view path)
    {
        uint64_t size = 0;
        auto pathW = String::toWideChar(path);
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (GetFileAttributesExW(pathW.c_str(), GetFileExInfoStandard, &attributes) != FALSE)
        {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = attributes.nFileSizeLow;
            fileSize.HighPart = attributes.nFileSizeHigh;
            size = fileSize.QuadPart;
        }
        return size;
    }

    bool ShouldIgnoreCase()
    {
        return true;
    }

    bool IsPathSeparator(char c)
    {
        return c == '\\' || c == '/';
    }

    std::string ResolveCasing(std::string_view path, bool fileExists)
    {
        std::string result;
        if (fileExists)
        {
            // Windows is case insensitive so it will exist and that is all that matters
            // for now. We can properly resolve the casing if we ever need to.
            result = std::string(path);
        }
        return result;
    }

    bool RequireNewWindow(bool openGL)
    {
        // Windows is apparently able to switch to hardware rendering on the fly although
        // using the same window in an unaccelerated and accelerated context is unsupported by SDL2
        return openGL;
    }

    std::string GetUsername()
    {
        std::string result;
        wchar_t usernameW[UNLEN + 1]{};
        DWORD usernameLength = UNLEN + 1;
        if (GetUserNameW(usernameW, &usernameLength))
        {
            result = String::toUtf8(usernameW);
        }
        return result;
    }

    uint16_t GetLocaleLanguage()
    {
        wchar_t lang[16] = {};
        wchar_t ctry[16] = {};
        wchar_t langCode[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, lang, static_cast<int>(std::size(lang))) > 0)
        {
            if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, ctry, static_cast<int>(std::size(ctry))) > 0 && ctry[0] != L'\0')
            {
                swprintf(langCode, std::size(langCode), L"%ls-%ls", lang, ctry);
            }
            else
            {
                swprintf(langCode, std::size(langCode), L"%ls", lang);
            }
        }
        else
        {
            return LANGUAGE_ENGLISH_UK;
        }

        const std::pair<std::wstring_view, int16_t> supportedLocales[] = {
            { L"ar", /*LANGUAGE_ARABIC*/ LANGUAGE_UNDEFINED }, // Experimental, don't risk offering it by default yet
            { L"ca", LANGUAGE_CATALAN },
            { L"zh-Hans", LANGUAGE_CHINESE_SIMPLIFIED },  // May not be accurate enough
            { L"zh-Hant", LANGUAGE_CHINESE_TRADITIONAL }, // May not be accurate enough
            { L"cs", LANGUAGE_CZECH },
            { L"da", LANGUAGE_DANISH },
            { L"de", LANGUAGE_GERMAN },
            { L"en-GB", LANGUAGE_ENGLISH_UK },
            { L"en-US", LANGUAGE_ENGLISH_US },
            { L"eo", LANGUAGE_ESPERANTO },
            { L"es", LANGUAGE_SPANISH },
            { L"fr", LANGUAGE_FRENCH },
            { L"fr-CA", LANGUAGE_FRENCH_CA },
            { L"gl", LANGUAGE_GALICIAN },
            { L"it", LANGUAGE_ITALIAN },
            { L"ja", LANGUAGE_JAPANESE },
            { L"ko", LANGUAGE_KOREAN },
            { L"hu", LANGUAGE_HUNGARIAN },
            { L"nl", LANGUAGE_DUTCH },
            { L"no", LANGUAGE_NORWEGIAN },
            { L"pl", LANGUAGE_POLISH },
            { L"pt-BR", LANGUAGE_PORTUGUESE_BR },
            { L"ru", LANGUAGE_RUSSIAN },
            { L"fi", LANGUAGE_FINNISH },
            { L"sv", LANGUAGE_SWEDISH },
            { L"tr", LANGUAGE_TURKISH },
            { L"uk", LANGUAGE_UKRAINIAN },
            { L"vi", LANGUAGE_VIETNAMESE },
        };
        static_assert(
            std::size(supportedLocales) == LANGUAGE_COUNT - 1, "GetLocaleLanguage: List of languages does not match the enum!");

        for (const auto& locale : supportedLocales)
        {
            if (wcsncmp(langCode, locale.first.data(), locale.first.length()) == 0)
            {
                return locale.second;
            }
        }
        return LANGUAGE_ENGLISH_UK;
    }

    CurrencyType GetLocaleCurrency()
    {
        wchar_t currCode[9];
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SINTLSYMBOL, currCode, static_cast<int>(std::size(currCode))) == 0)
        {
            return GetCurrencyValue(nullptr);
        }

        return GetCurrencyValue(String::toUtf8(currCode).c_str());
    }

    MeasurementFormat GetLocaleMeasurementFormat()
    {
        UINT measurement_system;
        if (GetLocaleInfoW(
                LOCALE_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&measurement_system),
                sizeof(measurement_system) / sizeof(wchar_t))
            == 0)
        {
            return MeasurementFormat::metric;
        }

        return measurement_system == 1 ? MeasurementFormat::imperial : MeasurementFormat::metric;
    }

    uint8_t GetLocaleDateFormat()
    {
        // Retrieve short date format, eg "MM/dd/yyyy"
        wchar_t dateFormat[80];
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SSHORTDATE, dateFormat, static_cast<int>(std::size(dateFormat)))
            == 0)
        {
            return DATE_FORMAT_DAY_MONTH_YEAR;
        }

        // The only valid characters for format types are: dgyM
        // We try to find 3 strings of format types, ignore any characters in between.
        // We also ignore 'g', as it represents 'era' and we don't have that concept
        // in our date formats.
        // https://msdn.microsoft.com/en-us/library/windows/desktop/dd317787(v=vs.85).aspx
        //
        wchar_t first[std::size(dateFormat)];
        wchar_t second[std::size(dateFormat)];
        if (swscanf(dateFormat, L"%l[dyM]%*l[^dyM]%l[dyM]%*l[^dyM]%*l[dyM]", first, second) != 2)
        {
            return DATE_FORMAT_DAY_MONTH_YEAR;
        }

        if (first[0] == L'd')
        {
            return DATE_FORMAT_DAY_MONTH_YEAR;
        }
        if (first[0] == L'M')
        {
            return DATE_FORMAT_MONTH_DAY_YEAR;
        }
        if (first[0] == L'y')
        {
            if (second[0] == 'd')
            {
                return DATE_FORMAT_YEAR_DAY_MONTH;
            }

            // Closest possible option
            return DATE_FORMAT_YEAR_MONTH_DAY;
        }

        // Default fallback
        return DATE_FORMAT_DAY_MONTH_YEAR;
    }

    TemperatureUnit GetLocaleTemperatureFormat()
    {
        UINT fahrenheit;

        // GetLocaleInfoW will set fahrenheit to 1 if the locale on this computer
        // uses the United States measurement system or 0 otherwise.
        if (GetLocaleInfoW(
                LOCALE_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&fahrenheit),
                sizeof(fahrenheit) / sizeof(wchar_t))
            == 0)
        {
            // Assume celsius by default if function call fails
            return TemperatureUnit::celsius;
        }

        return fahrenheit == 1 ? TemperatureUnit::fahrenheit : TemperatureUnit::celsius;
    }

    bool ProcessIsElevated()
    {
        BOOL isElevated = FALSE;
        HANDLE hToken = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        {
            TOKEN_ELEVATION Elevation;
            DWORD tokenSize = sizeof(TOKEN_ELEVATION);
            if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &tokenSize))
            {
                isElevated = Elevation.TokenIsElevated;
            }
        }
        if (hToken)
        {
            CloseHandle(hToken);
        }
        return isElevated;
    }

    SteamPaths GetSteamPaths()
    {
        HKEY hKey;
        DWORD type, size;

        if (RegOpenKeyW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", &hKey) != ERROR_SUCCESS)
            return {};

        // Get the size of the path first
        if (RegQueryValueExW(hKey, L"SteamPath", nullptr, &type, nullptr, &size) != ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return {};
        }

        std::string outPath = "";
        const auto wSteamPath = std::make_unique_for_overwrite<wchar_t[]>(size);
        const auto result = RegQueryValueExW(
            hKey, L"SteamPath", nullptr, &type, reinterpret_cast<LPBYTE>(wSteamPath.get()), &size);
        if (result == ERROR_SUCCESS)
        {
            outPath = String::toUtf8(wSteamPath.get());
        }
        RegCloseKey(hKey);

        SteamPaths ret = {};
        ret.roots.emplace_back(outPath);
        ret.nativeFolder = "steamapps/common";
        ret.downloadDepotFolder = "steamapps/content";
        ret.manifests = "steamapps";

        return ret;
    }

    std::string GetFontPath(const TTFFontDescriptor& font)
    {
        auto path = WIN32_GetKnownFolderPath(FOLDERID_Fonts);
        return !path.empty() ? Path::Combine(path, font.filename) : std::string();
    }

    bool LockSingleInstance()
    {
        // Check if operating system mutex exists
        HANDLE mutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
        if (mutex == nullptr)
        {
            LOG_ERROR("unable to create mutex");
            return true;
        }
        else if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            // Already running
            CloseHandle(mutex);
            return false;
        }
        return true;
    }

    int32_t GetDrives()
    {
        return GetLogicalDrives();
    }

    time_t FileGetModifiedTime(u8string_view path)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        auto wPath = String::toWideChar(path);
        auto result = GetFileAttributesExW(wPath.c_str(), GetFileExInfoStandard, &data);
        if (result != FALSE)
        {
            FILETIME localFileTime{};
            result = FileTimeToLocalFileTime(&data.ftLastWriteTime, &localFileTime);
            if (result != FALSE)
            {
                ULARGE_INTEGER ull{};
                ull.LowPart = localFileTime.dwLowDateTime;
                ull.HighPart = localFileTime.dwHighDateTime;
                return ull.QuadPart / 10000000uLL - 11644473600uLL;
            }
        }
        return 0;
    }

    datetime64 GetDatetimeNowUTC()
    {
        // Get file time
        FILETIME fileTime;
        GetSystemTimeAsFileTime(&fileTime);
        uint64_t fileTime64 = (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32uLL)
            | (static_cast<uint64_t>(fileTime.dwLowDateTime));

        // File time starts from: 1601-01-01T00:00:00Z
        // Convert to start from: 0001-01-01T00:00:00Z
        datetime64 utcNow = fileTime64 - 504911232000000000uLL;
        return utcNow;
    }

    bool SetupUriProtocol()
    {
        LOG_VERBOSE("Setting up URI protocol...");

        // [HKEY_CURRENT_USER\Software\Classes]
        HKEY hRootKey;
        if (RegOpenKeyW(HKEY_CURRENT_USER, SOFTWARE_CLASSES, &hRootKey) == ERROR_SUCCESS)
        {
            // [hRootKey\openrct2]
            HKEY hClassKey;
            if (RegCreateKeyW(hRootKey, L"openrct2", &hClassKey) == ERROR_SUCCESS)
            {
                if (RegSetValueW(hClassKey, nullptr, REG_SZ, L"URL:openrct2", 0) == ERROR_SUCCESS)
                {
                    if (RegSetValueExW(hClassKey, L"URL Protocol", 0, REG_SZ, reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t)) == ERROR_SUCCESS)
                    {
                        // [hRootKey\openrct2\shell\open\command]
                        const std::wstring& exePathW = WIN32_GetModuleFileNameW(nullptr);
                        const std::wstring handle_uri_string = std::format(L"\"{}\" handle-uri \"%1\"", exePathW);
                        if (RegSetValueW(hClassKey, L"shell\\open\\command", REG_SZ, handle_uri_string.c_str(), 0)
                            == ERROR_SUCCESS)
                        {
                            // Not compulsory, but gives the application a nicer name
                            // [HKEY_CURRENT_USER\SOFTWARE\Classes\Local Settings\Software\Microsoft\Windows\Shell\MuiCache]
                            HKEY hMuiCacheKey;
                            if (RegCreateKeyW(hRootKey, MUI_CACHE, &hMuiCacheKey) == ERROR_SUCCESS)
                            {
                                const std::wstring friendly_apl_name = std::format(L"{}.FriendlyAppName", exePathW);
                                RegSetValueExW(
                                    hMuiCacheKey, friendly_apl_name.c_str(), 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(L"OpenRCT2"), sizeof(L"OpenRCT2"));
                            }

                            LOG_VERBOSE("URI protocol setup successful");
                            return true;
                        }
                    }
                }
            }
        }

        LOG_VERBOSE("URI protocol setup failed");
        return false;
    }

    std::vector<std::string> GetSearchablePathsRCT1()
    {
        return {
            R"(C:\Program Files\Steam\steamapps\common\Rollercoaster Tycoon Deluxe)",
            R"(C:\Program Files (x86)\Steam\steamapps\common\Rollercoaster Tycoon Deluxe)",
            R"(C:\GOG Games\RollerCoaster Tycoon Deluxe)",
            R"(C:\Program Files\GalaxyClient\Games\RollerCoaster Tycoon Deluxe)",
            R"(C:\Program Files (x86)\GalaxyClient\Games\RollerCoaster Tycoon Deluxe)",
            R"(C:\Program Files\Hasbro Interactive\RollerCoaster Tycoon)",
            R"(C:\Program Files (x86)\Hasbro Interactive\RollerCoaster Tycoon)",
        };
    }

    std::vector<std::string> GetSearchablePathsRCT2()
    {
        return {
            R"(C:\Program Files\Steam\steamapps\common\Rollercoaster Tycoon 2)",
            R"(C:\Program Files (x86)\Steam\steamapps\common\Rollercoaster Tycoon 2)",
            R"(C:\GOG Games\RollerCoaster Tycoon 2 Triple Thrill Pack)",
            R"(C:\Program Files\GalaxyClient\Games\RollerCoaster Tycoon 2 Triple Thrill Pack)",
            R"(C:\Program Files (x86)\GalaxyClient\Games\RollerCoaster Tycoon 2 Triple Thrill Pack)",
            R"(C:\Program Files\Atari\RollerCoaster Tycoon 2)",
            R"(C:\Program Files (x86)\Atari\RollerCoaster Tycoon 2)",
            R"(C:\Program Files\Infogrames\RollerCoaster Tycoon 2)",
            R"(C:\Program Files (x86)\Infogrames\RollerCoaster Tycoon 2)",
            R"(C:\Program Files\Infogrames Interactive\RollerCoaster Tycoon 2)",
            R"(C:\Program Files (x86)\Infogrames Interactive\RollerCoaster Tycoon 2)",
            R"(C:\Program Files\Steam\steamapps\common\RollerCoaster Tycoon Classic)",
            R"(C:\Program Files (x86)\Steam\steamapps\common\RollerCoaster Tycoon Classic)",
        };
    }

#if defined(OpenRCT2_CPUID_GNUC_X86) || defined(OpenRCT2_CPUID_MSVC_X86)
    static void CpuIdRaw(uint32_t* out, uint32_t leaf)
    {
#    if defined(OpenRCT2_CPUID_GNUC_X86)
        __cpuid(leaf, out[0], out[1], out[2], out[3]);
#    elif defined(OpenRCT2_CPUID_MSVC_X86)
        __cpuid(reinterpret_cast<int*>(out), static_cast<int>(leaf));
#    else
        out[0] = out[1] = out[2] = out[3] = 0;
#    endif
    }
#endif

    static bool gTimerActive = false;

    void SetHighPrecisionTimer(bool enabled)
    {
        if (enabled && !gTimerActive)
        {
            if (timeBeginPeriod(1) == TIMERR_NOERROR)
            {
                gTimerActive = true;
                LOG_VERBOSE("Windows XP 1ms multimedia timer activated.");
            }
        }
        else if (!enabled && gTimerActive)
        {
            timeEndPeriod(1);
            gTimerActive = false;
            LOG_VERBOSE("Windows XP multimedia timer restored.");
        }
    }

    bool IsHighPrecisionTimerActive()
    {
        return gTimerActive;
    }

    struct PrecisionTimerInit
    {
        PrecisionTimerInit()
        {
            SetHighPrecisionTimer(true);
        }
        ~PrecisionTimerInit()
        {
            SetHighPrecisionTimer(false);
        }
    };
    static PrecisionTimerInit gPrecisionTimerInit;

    std::string GetCpuBrandName()
    {
        static std::string cachedBrand;
        if (!cachedBrand.empty())
            return cachedBrand;

#if defined(OpenRCT2_CPUID_GNUC_X86) || defined(OpenRCT2_CPUID_MSVC_X86)
        uint32_t regs[4] = { 0 };
        CpuIdRaw(regs, 0x80000000);
        if (regs[0] >= 0x80000004)
        {
            char brand[49] = { 0 };
            CpuIdRaw(reinterpret_cast<uint32_t*>(brand), 0x80000002);
            CpuIdRaw(reinterpret_cast<uint32_t*>(brand + 16), 0x80000003);
            CpuIdRaw(reinterpret_cast<uint32_t*>(brand + 32), 0x80000004);
            brand[48] = '\0';

            std::string s = brand;
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start != std::string::npos)
            {
                size_t end = s.find_last_not_of(" \t\r\n");
                cachedBrand = s.substr(start, end - start + 1);
                return cachedBrand;
            }
            cachedBrand = s;
            return cachedBrand;
        }
#endif
        cachedBrand = "x86 Compatible Processor";
        return cachedBrand;
    }

    std::string GetHypervisorName()
    {
        static std::string cachedHv;
        if (!cachedHv.empty())
            return cachedHv;

        if (IsRunningInWine())
        {
            cachedHv = "Wine / Proton";
            return cachedHv;
        }

#if defined(OpenRCT2_CPUID_GNUC_X86) || defined(OpenRCT2_CPUID_MSVC_X86)
        uint32_t regs[4] = { 0 };
        CpuIdRaw(regs, 1);
        if ((regs[2] & (1U << 31)) != 0)
        {
            CpuIdRaw(regs, 0x40000000);
            char sig[13] = { 0 };
            std::memcpy(sig + 0, &regs[1], 4);
            std::memcpy(sig + 4, &regs[2], 4);
            std::memcpy(sig + 8, &regs[3], 4);
            sig[12] = '\0';
            std::string_view sv(sig);

            if (sv.find("VBox") != std::string_view::npos)
                cachedHv = "Oracle VM VirtualBox";
            else if (sv.find("VMware") != std::string_view::npos)
                cachedHv = "VMware Workstation/ESXi";
            else if (sv.find("KVM") != std::string_view::npos)
                cachedHv = "QEMU / KVM";
            else if (sv.find("Microsoft") != std::string_view::npos || sv.find("Hv") != std::string_view::npos)
                cachedHv = "Microsoft Hyper-V / Virtual PC";
            else if (sv.find("Xen") != std::string_view::npos)
                cachedHv = "Xen Hypervisor";
            else if (sv.find("prl") != std::string_view::npos || sv.find("lrpe") != std::string_view::npos)
                cachedHv = "Parallels Desktop";
            else if (sv.find("bhyve") != std::string_view::npos)
                cachedHv = "bhyve";
            else if (sv.find("tcg") != std::string_view::npos)
                cachedHv = "QEMU (TCG Emulated)";
            else
                cachedHv = std::string("Hypervisor (") + sig + ")";

            return cachedHv;
        }
#endif

        DISPLAY_DEVICEA dd{};
        dd.cb = sizeof(dd);
        for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i)
        {
            std::string dev = dd.DeviceString;
            std::string devLower = dev;
            std::transform(devLower.begin(), devLower.end(), devLower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (devLower.find("virtualbox") != std::string::npos || devLower.find("vbox") != std::string::npos)
            {
                cachedHv = "Oracle VM VirtualBox";
                return cachedHv;
            }
            if (devLower.find("vmware") != std::string::npos || devLower.find("svga") != std::string::npos)
            {
                cachedHv = "VMware Workstation/ESXi";
                return cachedHv;
            }
            if (devLower.find("qemu") != std::string::npos || devLower.find("red hat") != std::string::npos || devLower.find("bochs") != std::string::npos)
            {
                cachedHv = "QEMU / KVM";
                return cachedHv;
            }
            if (devLower.find("86box") != std::string::npos)
            {
                cachedHv = "86Box PC Emulator";
                return cachedHv;
            }
            if (devLower.find("pcem") != std::string::npos)
            {
                cachedHv = "PCem PC Emulator";
                return cachedHv;
            }
        }

        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            char biosVal[256] = { 0 };
            DWORD sz = sizeof(biosVal);
            if (RegQueryValueExA(hKey, "SystemManufacturer", nullptr, nullptr, reinterpret_cast<LPBYTE>(biosVal), &sz) == ERROR_SUCCESS)
            {
                std::string s = biosVal;
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
                if (s.find("vmware") != std::string::npos) { RegCloseKey(hKey); cachedHv = "VMware Workstation/ESXi"; return cachedHv; }
                if (s.find("innotek") != std::string::npos || s.find("virtualbox") != std::string::npos) { RegCloseKey(hKey); cachedHv = "Oracle VM VirtualBox"; return cachedHv; }
                if (s.find("qemu") != std::string::npos) { RegCloseKey(hKey); cachedHv = "QEMU / KVM"; return cachedHv; }
            }

            sz = sizeof(biosVal);
            if (RegQueryValueExA(hKey, "SystemProductName", nullptr, nullptr, reinterpret_cast<LPBYTE>(biosVal), &sz) == ERROR_SUCCESS)
            {
                std::string s = biosVal;
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
                if (s.find("virtualbox") != std::string::npos) { RegCloseKey(hKey); cachedHv = "Oracle VM VirtualBox"; return cachedHv; }
                if (s.find("vmware") != std::string::npos) { RegCloseKey(hKey); cachedHv = "VMware Workstation/ESXi"; return cachedHv; }
                if (s.find("86box") != std::string::npos) { RegCloseKey(hKey); cachedHv = "86Box PC Emulator"; return cachedHv; }
                if (s.find("pcem") != std::string::npos) { RegCloseKey(hKey); cachedHv = "PCem PC Emulator"; return cachedHv; }
                if (s.find("virtual machine") != std::string::npos) { RegCloseKey(hKey); cachedHv = "Microsoft Virtual PC"; return cachedHv; }
            }
            RegCloseKey(hKey);
        }

        cachedHv = "Bare-Metal PC";
        return cachedHv;
    }

    bool IsVirtualMachine()
    {
        auto hv = GetHypervisorName();
        return hv != "Bare-Metal PC" && !hv.empty();
    }

    bool SetDesktopWallpaper(const std::string& path)
    {
        if (path.empty() || GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return false;

        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
        {
            RegSetValueExA(hKey, "WallpaperStyle", 0, REG_SZ, reinterpret_cast<const BYTE*>("2"), 2); // 2 = Stretched
            RegSetValueExA(hKey, "TileWallpaper", 0, REG_SZ, reinterpret_cast<const BYTE*>("0"), 2);
            RegCloseKey(hKey);
        }

        auto ret = SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, const_cast<char*>(path.c_str()), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        return ret != 0;
    }

} // namespace OpenRCT2::Platform


#endif
