/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifdef _WIN32

// clang-format off
    // windows.h needs to be included first
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <shellapi.h>
    #include <commdlg.h>
    #include <shlobj.h>
    #undef CreateWindow
// clang-format on

    // Then the rest
    #include "UiContext.h"

    #include <SDL_syswm.h>
    #include <cstring>
    #include <openrct2/Diagnostic.h>
    #include <openrct2/core/Path.hpp>
    #include <openrct2/core/String.hpp>
    #include <openrct2/ui/UiContext.h>

    // Native resource IDs
    #include "../../resources/resource.h"

namespace OpenRCT2::Ui
{
    class Win32Context : public IPlatformUiContext
    {
    private:
        HMODULE _win32module;

    public:
        Win32Context()
        {
            _win32module = GetModuleHandle(nullptr);
        }

        void SetWindowIcon(SDL_Window* window) override
        {
            if (_win32module != nullptr)
            {
                HICON icon = LoadIcon(_win32module, MAKEINTRESOURCE(IDI_ICON));
                if (icon != nullptr)
                {
                    HWND hwnd = GetHWND(window);
                    if (hwnd != nullptr)
                    {
                        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
                        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
                    }
                }
            }
        }

        bool IsSteamOverlayAttached() override
        {
            return (GetModuleHandleW(L"GameOverlayRenderer.dll") != nullptr);
        }

        void ShowMessageBox(SDL_Window* window, const std::string& message) override
        {
            HWND hwnd = GetHWND(window);
            std::wstring messageW = String::toWideChar(message);
            MessageBoxW(hwnd, messageW.c_str(), L"OpenRCT2", MB_OK);
        }

        void ShowNotification(SDL_Window* window, const std::string& title, const std::string& message) override
        {
            HWND hwnd = GetHWND(window);
            NOTIFYICONDATAA nid{};
            nid.cbSize = sizeof(NOTIFYICONDATAA);
            nid.hWnd = hwnd;
            nid.uID = 1001;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
            nid.hIcon = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));
            if (!nid.hIcon)
            {
                nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            }
            strncpy(nid.szTip, "OpenRCT2", sizeof(nid.szTip) - 1);
            strncpy(nid.szInfoTitle, title.c_str(), sizeof(nid.szInfoTitle) - 1);
            strncpy(nid.szInfo, message.c_str(), sizeof(nid.szInfo) - 1);
            nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconA(NIM_ADD, &nid);
        }

        bool HasMenuSupport() override
        {
            return false;
        }

        int32_t ShowMenuDialog(
            const std::vector<std::string>& options, const std::string& title, const std::string& text) override
        {
            return -1;
        }

        void OpenFolder(const std::string& path) override
        {
            std::wstring pathW = String::toWideChar(path);
            ShellExecuteW(NULL, L"open", pathW.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }

        void OpenURL(const std::string& url) override
        {
            std::wstring urlW = String::toWideChar(url);
            ShellExecuteW(NULL, L"open", urlW.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }

        std::string ShowFileDialogInternal(SDL_Window* window, const FileDialogDesc& desc, bool isFolder)
        {
            std::string resultFilename;
            WCHAR path[MAX_PATH] = {};
            if (isFolder)
            {
                BROWSEINFOW bi = {};
                std::wstring wtitle = String::toWideChar(desc.Title);
                bi.hwndOwner = GetHWND(window);
                bi.pidlRoot = nullptr;
                bi.pszDisplayName = path;
                bi.lpszTitle = wtitle.c_str();
                bi.ulFlags = BIF_USENEWUI;
                bi.lpfn = nullptr;
                bi.lParam = 0;
                bi.iImage = 0;

                PIDLIST_ABSOLUTE list = SHBrowseForFolderW(&bi);
                if (list != NULL)
                {
                    SHGetPathFromIDListW(list, path);
                    resultFilename = String::toUtf8(path);
                    CoTaskMemFree(list);
                }
            }
            else
            {
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = GetHWND(window);
                ofn.lpstrFile = path;
                ofn.nMaxFile = MAX_PATH;
                std::wstring wtitle = String::toWideChar(desc.Title);
                ofn.lpstrTitle = wtitle.c_str();
                std::wstring wInitDir = String::toWideChar(desc.InitialDirectory);
                if (!wInitDir.empty())
                {
                    ofn.lpstrInitialDir = wInitDir.c_str();
                }

                std::wstring filterStr;
                for (const auto& filter : desc.Filters)
                {
                    filterStr += String::toWideChar(filter.Name);
                    filterStr.push_back(L'\0');
                    filterStr += String::toWideChar(filter.Pattern);
                    filterStr.push_back(L'\0');
                }
                filterStr.push_back(L'\0');
                if (!desc.Filters.empty())
                {
                    ofn.lpstrFilter = filterStr.c_str();
                }

                if (desc.Type == FileDialogType::save)
                {
                    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                    if (GetSaveFileNameW(&ofn))
                    {
                        resultFilename = String::toUtf8(ofn.lpstrFile);
                    }
                }
                else
                {
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn))
                    {
                        resultFilename = String::toUtf8(ofn.lpstrFile);
                    }
                }
            }
            return resultFilename;
        }

        std::string ShowFileDialog(SDL_Window* window, const FileDialogDesc& desc) override
        {
            return ShowFileDialogInternal(window, desc, false);
        }

        std::string ShowDirectoryDialog(SDL_Window* window, const std::string& title) override
        {
            FileDialogDesc desc;
            desc.Title = title;
            return ShowFileDialogInternal(window, desc, true);
        }

        bool HasFilePicker() const override
        {
            return true;
        }

    private:
        HWND GetHWND(SDL_Window* window)
        {
            HWND result = nullptr;
            if (window != nullptr)
            {
                SDL_SysWMinfo wmInfo;
                SDL_VERSION(&wmInfo.version);
                if (SDL_GetWindowWMInfo(window, &wmInfo) != SDL_TRUE)
                {
                    LOG_ERROR("SDL_GetWindowWMInfo failed %s", SDL_GetError());
                    exit(-1);
                }

                result = wmInfo.info.win.window;
            }
            return result;
        }
    };

    std::unique_ptr<IPlatformUiContext> CreatePlatformUiContext()
    {
        return std::make_unique<Win32Context>();
    }
} // namespace OpenRCT2::Ui

#endif // _WIN32
