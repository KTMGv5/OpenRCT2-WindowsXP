/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <openrct2-ui/UiStringIds.h>
#include <openrct2-ui/interface/Widget.h>
#include <openrct2-ui/interface/Window.h>
#include <openrct2-ui/windows/Windows.h>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/config/Config.h>
#include <openrct2/core/UnitConversion.h>
#include <openrct2/drawing/ColourMap.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/PaletteIndex.h>
#include <openrct2/drawing/Rectangle.h>
#include <openrct2/drawing/Text.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/localisation/Formatting.h>
#include <openrct2/ride/Ride.h>
#include <openrct2/ride/RideManager.hpp>
#include <openrct2/ride/Vehicle.h>
#include <openrct2/ui/WindowManager.h>
#include <openrct2/world/Map.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace OpenRCT2::Ui::Windows
{
    using namespace OpenRCT2::Drawing;
    enum WindowViewportWidgetIdx : WidgetIndex
    {
        WIDX_BACKGROUND,
        WIDX_TITLE,
        WIDX_CLOSE,
        WIDX_CONTENT_PANEL,
        WIDX_VIEWPORT,
        WIDX_ZOOM_IN,
        WIDX_ZOOM_OUT,
        WIDX_LOCATE,
        WIDX_ROTATE,
        WIDX_NEXT_RIDE,
        WIDX_NEXT_TRAIN,
        WIDX_TOGGLE_HUD,
        WIDX_PIP_DOCK,
    };

#pragma region MEASUREMENTS

    static constexpr StringId kWindowTitle = kStringIdNone;
    static constexpr ScreenSize kWindowSize = { 320, 240 };
    static constexpr ScreenSize kButtonSize = { 24, 24 };

#pragma endregion

    // clang-format off
    static constexpr auto _viewportWidgets = makeWidgets(
        makeWindowShim(kWindowTitle, kWindowSize),
        makeWidget({      0, 14}, kWindowSize - ScreenSize( 1, 1),  WidgetType::resize,   WindowColour::secondary                                                    ), // resize
        makeWidget({      3, 17}, kWindowSize - ScreenSize(29, 3),  WidgetType::viewport, WindowColour::primary                                                      ), // viewport
        makeWidget({kWindowSize.width - 25,  17}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_G2_ZOOM_IN),     STR_ZOOM_IN_TIP          ), // zoom in
        makeWidget({kWindowSize.width - 25,  41}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_G2_ZOOM_OUT),    STR_ZOOM_OUT_TIP         ), // zoom out
        makeWidget({kWindowSize.width - 25,  65}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_LOCATE),         STR_LOCATE_SUBJECT_TIP   ), // locate
        makeWidget({kWindowSize.width - 25,  89}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_ROTATE_ARROW),   STR_LOCATE_SUBJECT_TIP   ), // rotate
        makeWidget({kWindowSize.width - 25, 113}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_NEXT),           STR_COASTER_CAM_NEXT_RIDE_TIP ), // next ride
        makeWidget({kWindowSize.width - 25, 137}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_RIDE),           STR_COASTER_CAM_NEXT_TRAIN_TIP), // next train
        makeWidget({kWindowSize.width - 25, 161}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_TRACK_PEEP),     STR_COASTER_CAM_HUD_TIP  ), // toggle HUD
        makeWidget({kWindowSize.width - 25, 185}, kButtonSize,      WidgetType::flatBtn,  WindowColour::primary  , ImageId(SPR_TAB),            STR_COASTER_CAM_PIP_TIP  )  // toggle PIP dock
    );
    // clang-format on

    static int32_t GetTrainPeepCount(const Ride& ride, uint8_t trainIndex)
    {
        int32_t count = 0;
        const auto* v = TryGetVehicle(ride.vehicles[trainIndex]);
        while (v != nullptr)
        {
            count += v->num_peeps;
            v = TryGetVehicle(v->next_vehicle_on_train);
        }
        return count;
    }

    static uint8_t GetRideTrainCount(const Ride& ride)
    {
        uint8_t count = 0;
        for (uint8_t i = 0; i < Limits::kMaxTrainsPerRide; i++)
        {
            if (TryGetVehicle(ride.vehicles[i]) != nullptr)
            {
                count++;
            }
        }
        return count == 0 ? 1 : count;
    }

    static std::string FormatRatingVal(int32_t rating)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.%02d", rating / 100, std::abs(rating % 100));
        return buf;
    }

    class ViewportWindow final : public Window
    {
    private:
        u8string _windowTitle{};
        RideId _trackedRideId{ RideId::GetNull() };
        uint8_t _trackedTrainIndex{ 0 };
        bool _showTelemetryHUD{ true };
        bool _isPIPDocked{ false };
        ScreenCoordsXY _undockedPos{ 0, 0 };
        ScreenSize _undockedSize{ 0, 0 };

        void GetFreeViewportNumber()
        {
            number = 1;
            WindowVisitEach([&](WindowBase* w) {
                if (w != nullptr && w != this && w->classification == WindowClass::viewport)
                {
                    if (w->number >= number)
                        number = w->number + 1;
                }
            });
        }

    public:
        void TrackRide(RideId targetRideId)
        {
            _trackedRideId = targetRideId;
            _trackedTrainIndex = 0;
            auto ride = GetRide(_trackedRideId);
            if (ride != nullptr)
            {
                auto vehicle = TryGetVehicle(ride->vehicles[0]);
                if (vehicle != nullptr)
                {
                    viewportTargetSprite = vehicle->id;
                }
            }
            invalidate();
        }

        void TrackNextRide()
        {
            auto& gameState = getGameState();
            RideId firstFound = RideId::GetNull();
            RideId nextFound = RideId::GetNull();
            bool foundCurrent = _trackedRideId.IsNull();

            for (const auto& ride : RideManager(gameState))
            {
                if (!ride.flags.has(RideFlag::onTrack))
                    continue;

                if (TryGetVehicle(ride.vehicles[0]) == nullptr)
                    continue;

                if (firstFound.IsNull())
                    firstFound = ride.id;

                if (foundCurrent)
                {
                    nextFound = ride.id;
                    break;
                }

                if (ride.id == _trackedRideId)
                {
                    foundCurrent = true;
                }
            }

            RideId target = !nextFound.IsNull() ? nextFound : firstFound;
            if (!target.IsNull())
            {
                TrackRide(target);
            }
        }

        void TrackNextTrain()
        {
            if (_trackedRideId.IsNull())
            {
                TrackNextRide();
                return;
            }

            auto ride = GetRide(_trackedRideId);
            if (ride == nullptr)
            {
                TrackNextRide();
                return;
            }

            uint8_t totalTrains = GetRideTrainCount(*ride);
            if (totalTrains == 0)
                return;

            _trackedTrainIndex = (_trackedTrainIndex + 1) % totalTrains;
            auto vehicle = TryGetVehicle(ride->vehicles[_trackedTrainIndex]);
            if (vehicle != nullptr)
            {
                viewportTargetSprite = vehicle->id;
            }
            invalidate();
        }

        void TogglePIPDock()
        {
            if (!_isPIPDocked)
            {
                _undockedPos = windowPos;
                _undockedSize = { width, height };

                int32_t screenW = ContextGetWidth();
                int32_t screenH = ContextGetHeight();

                int32_t pipW = 320;
                int32_t pipH = 240;
                int32_t pipX = std::max(0, screenW - pipW - 12);
                int32_t pipY = std::max(0, screenH - pipH - 38);

                windowPos = { pipX, pipY };
                width = pipW;
                height = pipH;
                _isPIPDocked = true;
            }
            else
            {
                if (_undockedSize.width > 0 && _undockedSize.height > 0)
                {
                    windowPos = _undockedPos;
                    width = _undockedSize.width;
                    height = _undockedSize.height;
                }
                _isPIPDocked = false;
            }
            invalidate();
        }

        void onOpen() override
        {
            GetFreeViewportNumber();

            setWidgets(_viewportWidgets);

            // Create viewport
            ViewportCreate(*this, windowPos, width, height, Focus(TileCoordsXYZ(128, 128, 0).toCoordsXYZ()));
            if (viewport == nullptr)
            {
                close();
                ErrorOpen("Unexpected Error", "Failed to create viewport window.");
                return;
            }

            auto* mainWindow = WindowGetMain();
            if (mainWindow != nullptr)
            {
                Viewport* mainViewport = mainWindow->viewport;
                int32_t x = mainViewport->viewPos.x + (mainViewport->ViewWidth() / 2);
                int32_t y = mainViewport->viewPos.y + (mainViewport->ViewHeight() / 2);
                savedViewPos = { x - (viewport->ViewWidth() / 2), y - (viewport->ViewHeight() / 2) };
            }

            viewport->flags.set(ViewportFlag::soundOn, ViewportFlag::independentRotation);

            WindowSetResize(*this, { 220, 180 }, { (ContextGetWidth() * 4) / 5, (ContextGetHeight() * 4) / 5 });

            // Automatically lock onto first ride in park if available
            TrackNextRide();
        }

        void onUpdate() override
        {
            if (!_trackedRideId.IsNull())
            {
                auto ride = GetRide(_trackedRideId);
                if (ride != nullptr && ride->flags.has(RideFlag::onTrack))
                {
                    auto vehicle = TryGetVehicle(ride->vehicles[_trackedTrainIndex]);
                    if (vehicle != nullptr)
                    {
                        viewportTargetSprite = vehicle->id;
                    }
                    else
                    {
                        for (uint8_t t = 0; t < Limits::kMaxTrainsPerRide; t++)
                        {
                            auto v = TryGetVehicle(ride->vehicles[t]);
                            if (v != nullptr)
                            {
                                _trackedTrainIndex = t;
                                viewportTargetSprite = v->id;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    TrackNextRide();
                }
            }

            auto* mainWindow = WindowGetMain();
            if (mainWindow == nullptr)
                return;

            if (viewport != nullptr && viewport->flags != mainWindow->viewport->flags.with(ViewportFlag::independentRotation))
            {
                viewport->flags = mainWindow->viewport->flags.with(ViewportFlag::independentRotation);
                invalidateWidget(WIDX_VIEWPORT);
            }
        }

        void onMouseUp(WidgetIndex widgetIndex) override
        {
            switch (widgetIndex)
            {
                case WIDX_CLOSE:
                    close();
                    break;
                case WIDX_ZOOM_IN:
                    WindowZoomIn(*this, false);
                    break;
                case WIDX_ZOOM_OUT:
                    WindowZoomOut(*this, false);
                    break;
                case WIDX_LOCATE:
                {
                    auto* mainWindow = WindowGetMain();
                    if (mainWindow != nullptr)
                    {
                        if (!_trackedRideId.IsNull())
                        {
                            auto ride = GetRide(_trackedRideId);
                            if (ride != nullptr)
                            {
                                auto vehicle = TryGetVehicle(ride->vehicles[_trackedTrainIndex]);
                                if (vehicle != nullptr)
                                {
                                    WindowScrollToLocation(*mainWindow, { { vehicle->x, vehicle->y }, vehicle->z });
                                    break;
                                }
                            }
                        }
                        auto info = GetMapCoordinatesFromPos(
                            { windowPos.x + (width / 2), windowPos.y + (height / 2) }, kViewportInteractionItemAll);
                        WindowScrollToLocation(*mainWindow, { info.Loc, TileElementHeight(info.Loc) });
                    }
                    break;
                }
                case WIDX_ROTATE:
                    ViewportRotateSingle(this, 1);
                    invalidate();
                    break;
                case WIDX_NEXT_RIDE:
                    TrackNextRide();
                    break;
                case WIDX_NEXT_TRAIN:
                    TrackNextTrain();
                    break;
                case WIDX_TOGGLE_HUD:
                    _showTelemetryHUD = !_showTelemetryHUD;
                    invalidate();
                    break;
                case WIDX_PIP_DOCK:
                    TogglePIPDock();
                    break;
            }
        }

        void DrawTelemetryHUD(Drawing::RenderTarget& rt)
        {
            auto ride = GetRide(_trackedRideId);
            if (ride == nullptr)
                return;

            auto vehicle = TryGetVehicle(ride->vehicles[_trackedTrainIndex]);
            if (vehicle == nullptr)
            {
                vehicle = TryGetVehicle(ride->vehicles[0]);
            }

            int32_t vpLeft = viewport->pos.x;
            int32_t vpTop = viewport->pos.y;
            int32_t vpRight = vpLeft + viewport->width;
            int32_t vpBottom = vpTop + viewport->height;

            if (viewport->width < 180 || viewport->height < 120)
                return;

            // 1. TOP BROADCAST BANNER
            ScreenRect topBar = { { vpLeft + 4, vpTop + 4 }, { vpRight - 4, vpTop + 34 } };
            Drawing::Rectangle::filter(rt, topBar, FilterPaletteID::palette51);
            Drawing::Rectangle::filter(rt, topBar, FilterPaletteID::palette51);

            bool blinkOn = (gCurrentRealTimeTicks / 500) % 2 == 0;
            std::string liveTag = blinkOn ? "{RED}* LIVE POV CAM" : "{GREY}* LIVE POV CAM";
            TextPaint tpHeader;
            tpHeader.fontStyle = FontStyle::medium;
            tpHeader.colour = ColourWithFlags{ Drawing::Colour::white }.withFlag(ColourFlag::withOutline, true);
            drawText(rt, { vpLeft + 8, vpTop + 7 }, liveTag, tpHeader);

            std::string rideName = ride->getName();
            std::string trainTag = " [Train #" + std::to_string(_trackedTrainIndex + 1);
            if (vehicle != nullptr)
            {
                int32_t peeps = GetTrainPeepCount(*ride, _trackedTrainIndex);
                trainTag += " - " + std::to_string(peeps) + " Guests]";
            }
            else
            {
                trainTag += "]";
            }

            std::string fullHeader = "{YELLOW}" + rideName + "{WHITE}" + trainTag;
            drawText(rt, { vpLeft + 114, vpTop + 7 }, fullHeader, tpHeader);

            std::string ratingsStr;
            if (!ride->ratings.isNull())
            {
                ratingsStr = "{WHITE}Excitement: {BRIGHTGREEN}" + FormatRatingVal(ride->ratings.excitement)
                    + "  {WHITE}Intensity: {ORANGE}" + FormatRatingVal(ride->ratings.intensity)
                    + "  {WHITE}Nausea: {LIGHTBLUE}" + FormatRatingVal(ride->ratings.nausea);
            }
            else
            {
                ratingsStr = "{LIGHTBLUE}* Coaster Tracking Active";
            }
            TextPaint tpSub;
            tpSub.fontStyle = FontStyle::small;
            tpSub.colour = ColourWithFlags{ Drawing::Colour::white }.withFlag(ColourFlag::withOutline, true);
            drawText(rt, { vpLeft + 10, vpTop + 20 }, ratingsStr, tpSub);

            // 2. BOTTOM LEFT SPEEDOMETER PANEL
            int32_t speedMph = 0;
            int32_t displaySpeed = 0;
            std::string speedUnit = "MPH";
            if (vehicle != nullptr)
            {
                speedMph = ToHumanReadableSpeed(std::abs(vehicle->velocity));
                if (Config::Get().general.measurementFormat == MeasurementFormat::metric)
                {
                    displaySpeed = MphToKmph(speedMph);
                    speedUnit = "KM/H";
                }
                else
                {
                    displaySpeed = speedMph;
                }
            }

            ScreenRect speedPanel = { { vpLeft + 4, vpBottom - 44 }, { vpLeft + 140, vpBottom - 4 } };
            Drawing::Rectangle::filter(rt, speedPanel, FilterPaletteID::palette51);
            Drawing::Rectangle::filter(rt, speedPanel, FilterPaletteID::palette51);

            std::string speedStr = "{WHITE}SPEED: {YELLOW}" + std::to_string(displaySpeed) + " " + speedUnit;
            drawText(rt, { vpLeft + 8, vpBottom - 41 }, speedStr, tpHeader);

            ScreenRect gaugeBg = { { vpLeft + 8, vpBottom - 18 }, { vpLeft + 134, vpBottom - 10 } };
            Drawing::Rectangle::fill(rt, gaugeBg, PaletteIndex::pi10);

            float speedRatio = std::clamp(static_cast<float>(speedMph) / 90.0f, 0.0f, 1.0f);
            int32_t fillWidth = static_cast<int32_t>(speedRatio * 124.0f);
            if (fillWidth > 0)
            {
                ScreenRect gaugeFill = { { vpLeft + 9, vpBottom - 17 }, { vpLeft + 9 + fillWidth, vpBottom - 11 } };
                PaletteIndex barColor;
                if (speedMph < 35)
                {
                    barColor = getColourMap(Drawing::Colour::lightBlue).light;
                }
                else if (speedMph < 60)
                {
                    barColor = getColourMap(Drawing::Colour::brightGreen).light;
                }
                else if (speedMph < 80)
                {
                    barColor = getColourMap(Drawing::Colour::yellow).light;
                }
                else
                {
                    barColor = getColourMap(Drawing::Colour::brightRed).light;
                }
                Drawing::Rectangle::fill(rt, gaugeFill, barColor);
            }

            // 3. BOTTOM RIGHT TELEMETRY (G-FORCE & ALTITUDE)
            ScreenRect telemPanel = { { vpRight - 150, vpBottom - 44 }, { vpRight - 4, vpBottom - 4 } };
            Drawing::Rectangle::filter(rt, telemPanel, FilterPaletteID::palette51);
            Drawing::Rectangle::filter(rt, telemPanel, FilterPaletteID::palette51);

            float vertG = 1.0f;
            if (vehicle != nullptr && vehicle->velocity != 0)
            {
                vertG = 1.0f + (static_cast<float>(vehicle->acceleration) / 32768.0f) * 2.5f;
                vertG = std::clamp(vertG, -1.5f, 6.0f);
            }

            char gBuf[32];
            snprintf(gBuf, sizeof(gBuf), "%.1f", vertG);
            std::string gColor = (vertG > 4.2f) ? "{RED}" : ((vertG > 2.8f) ? "{ORANGE}" : "{BRIGHTGREEN}");
            std::string gStr = "{WHITE}VERT: " + gColor + "+" + gBuf + "g";
            drawText(rt, { vpRight - 144, vpBottom - 41 }, gStr, tpHeader);

            int32_t altM = 0;
            std::string altUnit = "M";
            int32_t altDisplay = 0;
            if (vehicle != nullptr)
            {
                altM = BaseZToMetres(vehicle->z);
                if (Config::Get().general.measurementFormat == MeasurementFormat::imperial)
                {
                    altDisplay = MetresToFeet(altM);
                    altUnit = "FT";
                }
                else
                {
                    altDisplay = altM;
                }
            }
            std::string altStr = "{WHITE}ALT: {LIGHTBLUE}" + std::to_string(altDisplay) + " " + altUnit;
            drawText(rt, { vpRight - 144, vpBottom - 20 }, altStr, tpSub);
        }

        void onDraw(Drawing::RenderTarget& rt) override
        {
            drawWidgets(rt);

            if (viewport != nullptr)
                WindowDrawViewport(rt, *this);

            if (_showTelemetryHUD && viewport != nullptr && !_trackedRideId.IsNull())
            {
                DrawTelemetryHUD(rt);
            }
        }

        void onResize() override
        {
            int32_t screenWidth = ContextGetWidth();
            int32_t screenHeight = ContextGetHeight();

            maxWidth = (screenWidth * 4) / 5;
            maxHeight = (screenHeight * 4) / 5;

            minWidth = 220;
            minHeight = 180;

            WindowSetResize(*this, { minWidth, minHeight }, { maxWidth, maxHeight });
        }

        void onPrepareDraw() override
        {
            widgets[WIDX_ZOOM_IN].left = width - 27;
            widgets[WIDX_ZOOM_IN].right = width - 2;
            widgets[WIDX_ZOOM_OUT].left = width - 27;
            widgets[WIDX_ZOOM_OUT].right = width - 2;
            widgets[WIDX_LOCATE].left = width - 27;
            widgets[WIDX_LOCATE].right = width - 2;
            widgets[WIDX_ROTATE].left = width - 27;
            widgets[WIDX_ROTATE].right = width - 2;

            widgets[WIDX_NEXT_RIDE].left = width - 27;
            widgets[WIDX_NEXT_RIDE].right = width - 2;
            widgets[WIDX_NEXT_TRAIN].left = width - 27;
            widgets[WIDX_NEXT_TRAIN].right = width - 2;
            widgets[WIDX_TOGGLE_HUD].left = width - 27;
            widgets[WIDX_TOGGLE_HUD].right = width - 2;
            widgets[WIDX_PIP_DOCK].left = width - 27;
            widgets[WIDX_PIP_DOCK].right = width - 2;

            widgets[WIDX_VIEWPORT].right = widgets[WIDX_ZOOM_IN].left - 2;
            widgets[WIDX_VIEWPORT].bottom = widgets[WIDX_BACKGROUND].bottom - 3;

            // Set title
            if (!_trackedRideId.IsNull())
            {
                auto ride = GetRide(_trackedRideId);
                if (ride != nullptr)
                {
                    _windowTitle = "Live Coaster-Cam: " + ride->getName();
                }
                else
                {
                    _windowTitle = "Live Coaster-Cam (PIP)";
                }
            }
            else
            {
                _windowTitle = FormatStringID(STR_VIEWPORT_NO, static_cast<uint32_t>(number));
            }
            widgets[WIDX_TITLE].setString(_windowTitle.c_str());

            // Set disabled widgets
            setWidgetDisabled(WIDX_ZOOM_IN, viewport != nullptr && viewport->zoom == ZoomLevel::min());
            setWidgetDisabled(WIDX_ZOOM_OUT, viewport != nullptr && viewport->zoom >= ZoomLevel::max());

            if (viewport != nullptr)
            {
                Widget* viewportWidget = &widgets[WIDX_VIEWPORT];
                viewport->pos = windowPos + ScreenCoordsXY{ viewportWidget->left + 1, viewportWidget->top + 1 };
                viewport->width = widgets[WIDX_VIEWPORT].width() - 2;
                viewport->height = widgets[WIDX_VIEWPORT].height() - 2;
            }
        }
    };

    WindowBase* ViewportOpen()
    {
        return GetWindowManager()->Create<ViewportWindow>(WindowClass::viewport, kWindowSize, WindowFlag::resizable);
    }

    WindowBase* ViewportOpenCoasterCam(RideId rideId)
    {
        auto* windowMgr = GetWindowManager();
        auto* window = windowMgr->FindByClass(WindowClass::viewport);
        if (window == nullptr)
        {
            window = windowMgr->Create<ViewportWindow>(WindowClass::viewport, kWindowSize, WindowFlag::resizable);
        }
        else
        {
            windowMgr->BringToFront(*window);
        }

        if (window != nullptr)
        {
            auto* vpWin = static_cast<ViewportWindow*>(window);
            if (!rideId.IsNull())
            {
                vpWin->TrackRide(rideId);
            }
            else
            {
                vpWin->TrackNextRide();
            }
        }
        return window;
    }
} // namespace OpenRCT2::Ui::Windows
