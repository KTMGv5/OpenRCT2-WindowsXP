/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../UiStringIds.h"

#include <algorithm>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/Drawing.Screen.h>
#include <openrct2/drawing/Text.h>
#include <openrct2/interface/Screenshot.h>
#include <openrct2/localisation/Language.h>
#include <openrct2/localisation/StringIds.h>
#include <openrct2/platform/Platform.h>
#include <openrct2/ui/UiContext.h>
#include <openrct2/ui/WindowManager.h>
#include <string>
#include <string_view>

namespace OpenRCT2::Ui::Windows
{
    static constexpr StringId kWindowTitle = STR_WINDOWS_XP_TUNING;
    static constexpr ScreenSize kWindowSize = { 430, 248 };

    enum WindowXpTuningWidgetIdx : WidgetIndex
    {
        WIDX_BACKGROUND,
        WIDX_TITLE,
        WIDX_CLOSE,

        WIDX_GROUP_DIAGNOSTICS,

        WIDX_GROUP_ACTIONS,
        WIDX_SET_DESKTOP_WALLPAPER,
        WIDX_RUN_BENCHMARK,
    };

    // clang-format off
    static constexpr auto _windowWidgets = makeWidgets(
        makeWindowShim(kWindowTitle, kWindowSize),

        // Group 1: Diagnostics
        makeWidget({ 6, 20 }, { 418, 108 }, WidgetType::groupbox, WindowColour::secondary, STR_XP_DIAGNOSTICS_GROUP),

        // Group 2: Tools & Desktop Integration
        makeWidget({ 6, 134 }, { 418, 86 }, WidgetType::groupbox, WindowColour::secondary, STR_XP_ACTIONS_GROUP),
        makeWidget({ 14, 150 }, { 402, 22 }, WidgetType::button, WindowColour::secondary, STR_SET_DESKTOP_WALLPAPER, STR_SET_DESKTOP_WALLPAPER_TIP),
        makeWidget({ 14, 178 }, { 402, 22 }, WidgetType::button, WindowColour::secondary, STR_RUN_BENCHMARK_BTN, STR_RUN_BENCHMARK_TIP)
    );
    // clang-format on

    class WindowsXpTuningWindow final : public Window
    {
    private:
        std::string _benchmarkScore = "Not run yet - click benchmark below";
        std::string _actionStatus;

    public:
        void onOpen() override
        {
            WindowSetResize(*this, kWindowSize, kWindowSize);
            setWidgets(_windowWidgets);
        }

        void onPrepareDraw() override
        {
        }

        void onMouseUp(WidgetIndex widgetIndex) override
        {
            switch (widgetIndex)
            {
                case WIDX_CLOSE:
                    close();
                    break;
                case WIDX_SET_DESKTOP_WALLPAPER:
                    ScreenshotSetDesktopWallpaper();
                    _actionStatus = LanguageGetString(STR_DESKTOP_WALLPAPER_UPDATED);
                    invalidate();
                    break;
                case WIDX_RUN_BENCHMARK:
                    RunBenchmark();
                    invalidate();
                    break;
            }
        }

        void onDraw(Drawing::RenderTarget& rt) override
        {
            drawWidgets(rt);

            const auto textColour = colours[1];
            auto drawLine = [&](int32_t x, int32_t y, std::string_view label, std::string_view value) {
                std::string full = std::string(label) + std::string(value);
                drawText(rt, windowPos + ScreenCoordsXY{ static_cast<int16_t>(x), static_cast<int16_t>(y) }, full, { textColour });
            };

            auto osStr = "Windows XP / Win32 (x86 Vintage Native)";
            auto hypervisor = Platform::GetHypervisorName();
            auto cpu = Platform::GetCpuBrandName();
            auto netStr = "256 KiB Buffers | TCP_NODELAY | KeepAlive (10/100M Ready)";

            drawLine(16, 36, "Platform OS:  ", osStr);
            drawLine(16, 52, "Hypervisor:   ", hypervisor);
            drawLine(16, 68, "CPU Detected: ", cpu.empty() ? "x86 Compatible Processor" : cpu);
            drawLine(16, 84, "Network Stack:", netStr);
            drawLine(16, 100, "Benchmark:    ", _benchmarkScore);

            if (!_actionStatus.empty())
            {
                drawText(rt, windowPos + ScreenCoordsXY{ 16, 226 }, _actionStatus, { textColour });
            }
        }

    private:
        void RunBenchmark()
        {
            uint32_t startTicks = Platform::GetTicks();
            volatile uint32_t acc = 0x12345678;
            for (uint32_t i = 0; i < 10000000; i++)
            {
                acc = (acc * 1664525u + 1013904223u) ^ (i & 0xFF);
            }
            uint32_t elapsed = Platform::GetTicks() - startTicks;
            if (elapsed == 0)
            {
                elapsed = 1;
            }

            std::string rating;
            if (elapsed < 15)
                rating = "Ultra-Fast (Modern Host VM / Fast Core i7/Ryzen)";
            else if (elapsed < 45)
                rating = "Fast (Core 2 Duo / Phenom II class)";
            else if (elapsed < 120)
                rating = "Good (Pentium 4 Extreme / Athlon 64 class)";
            else if (elapsed < 300)
                rating = "Standard (Pentium III / Athlon XP class)";
            else
                rating = "Vintage (Pentium II 300 MHz / 86Box Emulated PC)";

            _benchmarkScore = std::to_string(elapsed) + " ms - " + rating;
        }
    };

    WindowBase* WindowsXpTuningOpen()
    {
        auto* windowMgr = GetWindowManager();
        return windowMgr->FocusOrCreate<WindowsXpTuningWindow>(
            WindowClass::windowsXpTuning, kWindowSize, WindowFlag::centreScreen);
    }
} // namespace OpenRCT2::Ui::Windows
