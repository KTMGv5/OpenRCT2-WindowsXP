/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "LightFX.h"

#include "../Game.h"
#include "../GameState.h"
#include "../config/Config.h"
#include "../entity/EntityRegistry.h"
#include "../interface/Viewport.h"
#include "../paint/Paint.h"
#include "../ride/Ride.h"
#include "../ride/RideData.h"
#include "../ride/Vehicle.h"
#include "../util/Util.h"
#include "../world/Map.h"
#include "../world/tile_element/SurfaceElement.h"
#include "../world/tile_element/TileElement.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace OpenRCT2::Drawing::LightFx
{
    static uint8_t _bakedLightTexture_lantern_0[32 * 32];
    static uint8_t _bakedLightTexture_lantern_1[64 * 64];
    static uint8_t _bakedLightTexture_lantern_2[128 * 128];
    static uint8_t _bakedLightTexture_lantern_3[256 * 256];
    static uint8_t _bakedLightTexture_spot_0[32 * 32];
    static uint8_t _bakedLightTexture_spot_1[64 * 64];
    static uint8_t _bakedLightTexture_spot_2[128 * 128];
    static uint8_t _bakedLightTexture_spot_3[256 * 256];
    static RenderTarget _pixelInfo;
    static bool _lightfxAvailable = false;

    static void* _light_rendered_buffer_back = nullptr;
    static void* _light_rendered_buffer_front = nullptr;

    static uint32_t _lightPolution_back = 0;
    static uint32_t _lightPolution_front = 0;

    enum class Qualifier : uint8_t
    {
        entity,
        map,
    };

    struct LightListEntry
    {
        CoordsXYZ position;
        ScreenCoordsXY viewCoords;
        LightType type;
        uint8_t lightIntensity;
        uint32_t lightHash;
        Qualifier qualifier;
        uint8_t lightID;
        uint8_t lightLinger;
    };

    static LightListEntry _LightListA[16000];
    static LightListEntry _LightListB[16000];

    static LightListEntry* _LightListBack;
    static LightListEntry* _LightListFront;

    static uint32_t LightListCurrentCountBack;
    static uint32_t LightListCurrentCountFront;

    static int16_t _current_view_x_front = 0;
    static int16_t _current_view_y_front = 0;
    static uint8_t _current_view_rotation_front = 0;
    static ZoomLevel _current_view_zoom_front{ 0 };
    static int16_t _current_view_x_back = 0;
    static int16_t _current_view_y_back = 0;
    static uint8_t _current_view_rotation_back = 0;
    static ZoomLevel _current_view_zoom_back{ 0 };
    static ZoomLevel _current_view_zoom_back_delay{ 0 };

    static GamePalette gPalette_light;

    constexpr uint8_t GetLightTypeSize(LightType type)
    {
        return static_cast<uint8_t>(type) & 0x3;
    }

    constexpr LightType SetLightTypeSize(LightType type, uint8_t size)
    {
        return static_cast<LightType>((static_cast<uint8_t>(type) & ~0x3) | size);
    }

    static void GenerateLightTexture(uint8_t* target, int32_t size, bool isSpot)
    {
        float radius = static_cast<float>(size) / 2.0f;
        for (int32_t y = 0; y < size; y++)
        {
            float dy = (static_cast<float>(y) + 0.5f) - radius;
            for (int32_t x = 0; x < size; x++)
            {
                float dx = (static_cast<float>(x) + 0.5f) - radius;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= radius)
                {
                    *target++ = 0;
                }
                else
                {
                    float u = dist / radius; // 0.0 at center, 1.0 at edge
                    float oneMinusU2 = 1.0f - (u * u);
                    float val;
                    if (isSpot)
                    {
                        // Focused spotlight: smooth cubic falloff
                        val = oneMinusU2 * oneMinusU2 * std::sqrt(oneMinusU2);
                    }
                    else
                    {
                        // Omnidirectional lantern: smooth quadratic falloff
                        val = oneMinusU2 * oneMinusU2;
                    }
                    *target++ = static_cast<uint8_t>(std::clamp(val * 255.0f, 0.0f, 255.0f));
                }
            }
        }
    }

    void SetAvailable(bool available)
    {
        _lightfxAvailable = available;
    }

    bool IsAvailable()
    {
        return _lightfxAvailable && Config::Get().general.enableLightFx;
    }

    bool ForVehiclesIsAvailable()
    {
        return IsAvailable() && Config::Get().general.enableLightFxForVehicles;
    }

    void Init()
    {
        _LightListBack = _LightListA;
        _LightListFront = _LightListB;

        GenerateLightTexture(_bakedLightTexture_lantern_0, 32, false);
        GenerateLightTexture(_bakedLightTexture_lantern_1, 64, false);
        GenerateLightTexture(_bakedLightTexture_lantern_2, 128, false);
        GenerateLightTexture(_bakedLightTexture_lantern_3, 256, false);

        GenerateLightTexture(_bakedLightTexture_spot_0, 32, true);
        GenerateLightTexture(_bakedLightTexture_spot_1, 64, true);
        GenerateLightTexture(_bakedLightTexture_spot_2, 128, true);
        GenerateLightTexture(_bakedLightTexture_spot_3, 256, true);
    }

    void UpdateBuffers(RenderTarget& info)
    {
        _light_rendered_buffer_front = realloc(_light_rendered_buffer_front, info.width * info.height);
        _light_rendered_buffer_back = realloc(_light_rendered_buffer_back, info.width * info.height);
        _pixelInfo = info;
    }

    static void PrepareLightList(const Viewport& vp)
    {
        for (uint32_t light = 0; light < LightListCurrentCountFront; light++)
        {
            LightListEntry& entry = _LightListFront[light];

            if (entry.position.z == 0x7FFF)
            {
                entry.lightIntensity = 0xFF;
                continue;
            }

            int32_t posOnScreenX = entry.viewCoords.x - _current_view_x_front;
            int32_t posOnScreenY = entry.viewCoords.y - _current_view_y_front;

            posOnScreenX = _current_view_zoom_front.ApplyInversedTo(posOnScreenX);
            posOnScreenY = _current_view_zoom_front.ApplyInversedTo(posOnScreenY);

            if ((posOnScreenX < -128) || (posOnScreenY < -128) || (posOnScreenX > _pixelInfo.width + 128)
                || (posOnScreenY > _pixelInfo.height + 128))
            {
                entry.type = LightType::none;
                continue;
            }

            CoordsXY mapLoc{ static_cast<int16_t>(entry.position.x), static_cast<int16_t>(entry.position.y) };
            if (!MapIsLocationValid(mapLoc))
            {
                entry.type = LightType::none;
                continue;
            }

            // 1. Underground occlusion
            if (!vp.flags.has(ViewportFlag::undergroundInside))
            {
                const auto* surface = MapGetSurfaceElementAt(mapLoc);
                if (surface != nullptr && surface->getClearanceZ() > entry.position.z)
                {
                    entry.type = LightType::none;
                    continue;
                }
            }

            // 2. Line of sight isometric elevation occlusion towards camera
            static constexpr CoordsXY kStepByRotation[4] = {
                { 32, 32 },   // Rotation 0: looking from SW (+X, +Y are in front)
                { -32, 32 },  // Rotation 1: looking from NW (-X, +Y are in front)
                { -32, -32 }, // Rotation 2: looking from NE (-X, -Y are in front)
                { 32, -32 },  // Rotation 3: looking from SE (+X, -Y are in front)
            };
            uint8_t rot = _current_view_rotation_front & 3;
            CoordsXY step = kStepByRotation[rot];

            bool occluded = false;
            for (int32_t stepIdx = 1; stepIdx <= 2; stepIdx++)
            {
                CoordsXY checkPos = { static_cast<int16_t>(mapLoc.x + step.x * stepIdx),
                                      static_cast<int16_t>(mapLoc.y + step.y * stepIdx) };
                if (!MapIsLocationValid(checkPos))
                    break;
                const auto* s = MapGetSurfaceElementAt(checkPos);
                if (s != nullptr)
                {
                    int32_t heightLimit = entry.position.z + (stepIdx * 16);
                    if (s->getBaseZ() > heightLimit)
                    {
                        occluded = true;
                        break;
                    }
                }
            }

            if (occluded)
            {
                entry.type = LightType::none;
                continue;
            }

            if (_current_view_zoom_front > ZoomLevel{ 0 })
            {
                const int8_t zoomNumber = static_cast<int8_t>(_current_view_zoom_front);
                entry.lightIntensity = static_cast<uint8_t>(std::max(0, static_cast<int32_t>(entry.lightIntensity) - 5 * zoomNumber));
                if (GetLightTypeSize(entry.type) < zoomNumber)
                {
                    entry.type = LightType::none;
                    continue;
                }

                entry.type = SetLightTypeSize(entry.type, GetLightTypeSize(entry.type) - zoomNumber);
            }
        }
    }

    static void SwapBuffers()
    {
        void* tmp = _light_rendered_buffer_back;
        _light_rendered_buffer_back = _light_rendered_buffer_front;
        _light_rendered_buffer_front = tmp;

        tmp = _LightListBack;
        _LightListBack = _LightListFront;
        _LightListFront = static_cast<LightListEntry*>(tmp);

        LightListCurrentCountFront = LightListCurrentCountBack;
        LightListCurrentCountBack = 0x0;

        uint32_t uTmp = _lightPolution_back;
        _lightPolution_back = _lightPolution_front;
        _lightPolution_front = uTmp;

        _current_view_x_front = _current_view_x_back;
        _current_view_y_front = _current_view_y_back;
        _current_view_rotation_front = _current_view_rotation_back;
        _current_view_zoom_front = _current_view_zoom_back_delay;
        _current_view_zoom_back_delay = _current_view_zoom_back;
    }

    static void UpdateViewportSettings(const Viewport& vp)
    {
        _current_view_x_back = vp.viewPos.x;
        _current_view_y_back = vp.viewPos.y;
        _current_view_rotation_back = vp.rotation;
        _current_view_zoom_back = vp.zoom;
    }

    static void RenderLightsToFrontBuffer()
    {
        if (_light_rendered_buffer_front == nullptr)
        {
            return;
        }

        std::memset(_light_rendered_buffer_front, 0, _pixelInfo.width * _pixelInfo.height);

        _lightPolution_back = 0;

        //  LOG_WARNING("%i lights", LightListCurrentCountFront);

        for (uint32_t light = 0; light < LightListCurrentCountFront; light++)
        {
            const uint8_t* bufReadBase = nullptr;
            uint8_t* bufWriteBase = static_cast<uint8_t*>(_light_rendered_buffer_front);
            uint32_t bufReadWidth, bufReadHeight;
            int32_t bufWriteX, bufWriteY;
            int32_t bufWriteWidth, bufWriteHeight;
            uint32_t bufReadSkip, bufWriteSkip;

            LightListEntry& entry = _LightListFront[light];

            int32_t inRectCentreX = entry.viewCoords.x;
            int32_t inRectCentreY = entry.viewCoords.y;

            if (entry.position.z != 0x7FFF)
            {
                inRectCentreX -= _current_view_x_front;
                inRectCentreY -= _current_view_y_front;
                inRectCentreX = _current_view_zoom_front.ApplyInversedTo(inRectCentreX);
                inRectCentreY = _current_view_zoom_front.ApplyInversedTo(inRectCentreY);
            }

            switch (entry.type)
            {
                case LightType::lantern0:
                    bufReadWidth = 32;
                    bufReadHeight = 32;
                    bufReadBase = _bakedLightTexture_lantern_0;
                    break;
                case LightType::lantern1:
                    bufReadWidth = 64;
                    bufReadHeight = 64;
                    bufReadBase = _bakedLightTexture_lantern_1;
                    break;
                case LightType::lantern2:
                    bufReadWidth = 128;
                    bufReadHeight = 128;
                    bufReadBase = _bakedLightTexture_lantern_2;
                    break;
                case LightType::lantern3:
                    bufReadWidth = 256;
                    bufReadHeight = 256;
                    bufReadBase = _bakedLightTexture_lantern_3;
                    break;
                case LightType::spot0:
                    bufReadWidth = 32;
                    bufReadHeight = 32;
                    bufReadBase = _bakedLightTexture_spot_0;
                    break;
                case LightType::spot1:
                    bufReadWidth = 64;
                    bufReadHeight = 64;
                    bufReadBase = _bakedLightTexture_spot_1;
                    break;
                case LightType::spot2:
                    bufReadWidth = 128;
                    bufReadHeight = 128;
                    bufReadBase = _bakedLightTexture_spot_2;
                    break;
                case LightType::spot3:
                    bufReadWidth = 256;
                    bufReadHeight = 256;
                    bufReadBase = _bakedLightTexture_spot_3;
                    break;
                default:
                    continue;
            }

            // Clamp the reads to be no larger than the buffer size
            bufReadHeight = std::min<uint32_t>(_pixelInfo.height, bufReadHeight);
            bufReadWidth = std::min<uint32_t>(_pixelInfo.width, bufReadWidth);

            bufWriteX = inRectCentreX - bufReadWidth / 2;
            bufWriteY = inRectCentreY - bufReadHeight / 2;

            bufWriteWidth = bufReadWidth;
            bufWriteHeight = bufReadHeight;

            if (bufWriteX < 0)
            {
                bufReadBase += -bufWriteX;
                bufWriteWidth += bufWriteX;
            }
            else
            {
                bufWriteBase += bufWriteX;
            }

            if (bufWriteWidth <= 0)
                continue;

            if (bufWriteY < 0)
            {
                bufReadBase += -bufWriteY * bufReadWidth;
                bufWriteHeight += bufWriteY;
            }
            else
            {
                bufWriteBase += bufWriteY * _pixelInfo.width;
            }

            if (bufWriteHeight <= 0)
                continue;

            int32_t rightEdge = bufWriteX + bufWriteWidth;
            int32_t bottomEdge = bufWriteY + bufWriteHeight;

            if (rightEdge > _pixelInfo.width)
            {
                bufWriteWidth -= rightEdge - _pixelInfo.width;
            }
            if (bottomEdge > _pixelInfo.height)
            {
                bufWriteHeight -= bottomEdge - _pixelInfo.height;
            }

            if (bufWriteWidth <= 0)
                continue;
            if (bufWriteHeight <= 0)
                continue;

            _lightPolution_back += (bufWriteWidth * bufWriteHeight) / 256;

            bufReadSkip = bufReadWidth - bufWriteWidth;
            bufWriteSkip = _pixelInfo.width - bufWriteWidth;

            if (entry.lightIntensity == 0xFF)
            {
                for (int32_t y = 0; y < bufWriteHeight; y++)
                {
                    for (int32_t x = 0; x < bufWriteWidth; x++)
                    {
                        *bufWriteBase = std::min(0xFF, *bufWriteBase + *bufReadBase);
                        bufWriteBase++;
                        bufReadBase++;
                    }

                    bufWriteBase += bufWriteSkip;
                    bufReadBase += bufReadSkip;
                }
            }
            else
            {
                for (int32_t y = 0; y < bufWriteHeight; y++)
                {
                    for (int32_t x = 0; x < bufWriteWidth; x++)
                    {
                        *bufWriteBase = std::min(0xFF, *bufWriteBase + (((*bufReadBase) * (1 + entry.lightIntensity)) >> 8));
                        bufWriteBase++;
                        bufReadBase++;
                    }

                    bufWriteBase += bufWriteSkip;
                    bufReadBase += bufReadSkip;
                }
            }
        }
    }

    static void* GetFrontBuffer()
    {
        return _light_rendered_buffer_front;
    }

    const GamePalette& GetPalette()
    {
        return gPalette_light;
    }

    static void Add3DLight(
        const uint32_t lightHash, const Qualifier qualifier, const uint8_t id, const CoordsXYZ& loc, const LightType lightType)
    {
        if (LightListCurrentCountBack == 15999)
        {
            return;
        }

        //  LOG_WARNING("%i lights in back", LightListCurrentCountBack);

        for (uint32_t i = 0; i < LightListCurrentCountBack; i++)
        {
            LightListEntry& entry = _LightListBack[i];
            if (entry.lightHash != lightHash)
                continue;
            if (entry.qualifier != qualifier)
                continue;
            if (entry.lightID != id)
                continue;

            entry.position = loc;
            entry.viewCoords = Translate3DTo2DWithZ(GetCurrentRotation(), loc);
            entry.type = lightType;
            entry.lightIntensity = 0xFF;
            entry.lightHash = lightHash;
            entry.qualifier = qualifier;
            entry.lightID = id;
            entry.lightLinger = 1;

            return;
        }

        LightListEntry* entry = &_LightListBack[LightListCurrentCountBack++];

        entry->position = loc;
        entry->viewCoords = Translate3DTo2DWithZ(GetCurrentRotation(), loc);
        entry->type = lightType;
        entry->lightIntensity = 0xFF;
        entry->lightHash = lightHash;
        entry->qualifier = qualifier;
        entry->lightID = id;
        entry->lightLinger = 1;

        //  LOG_WARNING("new 3d light");
    }

    static void Add3DLight(const CoordsXYZ& loc, const LightType lightType)
    {
        Add3DLight(((loc.x << 16) | loc.y), Qualifier::map, loc.z, loc, lightType);
    }

    void Add3DLight(const EntityBase& entity, const uint8_t id, const CoordsXYZ& loc, const LightType lightType)
    {
        Add3DLight(entity.id.ToUnderlying(), Qualifier::entity, id, loc, lightType);
    }

    void Add3DLightMagicFromDrawingTile(
        const CoordsXY& mapPosition, int16_t offsetX, int16_t offsetY, int16_t offsetZ, LightType lightType)
    {
        int16_t x = mapPosition.x + offsetX + 16;
        int16_t y = mapPosition.y + offsetY + 16;

        Add3DLight({ x, y, offsetZ }, lightType);
    }

    static uint32_t GetLightPollution()
    {
        return _lightPolution_front;
    }

    static constexpr int16_t kOffsetLookup[] = {
        10, 10, 9, 8, 7, 6, 4, 2, 0, -2, -4, -6, -7, -8, -9, -10, -10, -10, -9, -8, -7, -6, -4, -2, 0, 2, 4, 6, 7, 8, 9, 10,
    };
    void AddLightsMagicVehicle_ObservationTower(const Vehicle* vehicle)
    {
        Add3DLight(*vehicle, 0, { vehicle->x, vehicle->y + 16, vehicle->z }, LightType::spot3);
        Add3DLight(*vehicle, 1, { vehicle->x + 16, vehicle->y, vehicle->z }, LightType::spot3);
        Add3DLight(*vehicle, 2, { vehicle->x - 16, vehicle->y, vehicle->z }, LightType::spot3);
        Add3DLight(*vehicle, 3, { vehicle->x, vehicle->y - 16, vehicle->z }, LightType::spot3);
    }

    void AddLightsMagicVehicle_MineTrainCoaster(const Vehicle* vehicle)
    {
        if (vehicle == vehicle->TrainHead())
        {
            int16_t place_x = vehicle->x - kOffsetLookup[(vehicle->orientation + 0) % 32] * 2;
            int16_t place_y = vehicle->y - kOffsetLookup[(vehicle->orientation + 8) % 32] * 2;
            Add3DLight(*vehicle, 0, { place_x, place_y, vehicle->z }, LightType::spot3);
        }
    }

    void AddLightsMagicVehicle_ChairLift(const Vehicle* vehicle)
    {
        Add3DLight(*vehicle, 0, { vehicle->x, vehicle->y, vehicle->z - 16 }, LightType::lantern2);
    }
    void AddLightsMagicVehicle_BoatHire(const Vehicle* vehicle)
    {
        Vehicle* vehicle_draw = vehicle->TrainHead();
        auto* nextVeh = getGameState().entities.getEntity<Vehicle>(vehicle_draw->next_vehicle_on_train);
        if (nextVeh != nullptr)
        {
            vehicle_draw = nextVeh;
        }
        int16_t place_x = vehicle_draw->x;
        int16_t place_y = vehicle_draw->y;
        place_x -= kOffsetLookup[(vehicle_draw->orientation + 0) % 32];
        place_y -= kOffsetLookup[(vehicle_draw->orientation + 8) % 32];
        Add3DLight(*vehicle, 0, { place_x, place_y, vehicle_draw->z }, LightType::spot2);
        place_x -= kOffsetLookup[(vehicle_draw->orientation + 0) % 32];
        place_y -= kOffsetLookup[(vehicle_draw->orientation + 8) % 32];
        Add3DLight(*vehicle, 1, { place_x, place_y, vehicle_draw->z }, LightType::spot2);
    }
    void AddLightsMagicVehicle_Monorail(const Vehicle* vehicle)
    {
        Add3DLight(*vehicle, 0, { vehicle->x, vehicle->y, vehicle->z + 12 }, LightType::spot2);
        int16_t place_x = vehicle->x;
        int16_t place_y = vehicle->y;
        if (vehicle == vehicle->TrainHead())
        {
            place_x -= kOffsetLookup[(vehicle->orientation + 0) % 32] * 2;
            place_y -= kOffsetLookup[(vehicle->orientation + 8) % 32] * 2;
            Add3DLight(*vehicle, 1, { place_x, place_y, vehicle->z + 10 }, LightType::lantern3);
            place_x -= kOffsetLookup[(vehicle->orientation + 0) % 32] * 3;
            place_y -= kOffsetLookup[(vehicle->orientation + 8) % 32] * 3;
            Add3DLight(*vehicle, 2, { place_x, place_y, vehicle->z + 2 }, LightType::lantern3);
        }
        if (vehicle == vehicle->TrainTail())
        {
            place_x += kOffsetLookup[(vehicle->orientation + 0) % 32] * 2;
            place_y += kOffsetLookup[(vehicle->orientation + 8) % 32] * 2;
            Add3DLight(*vehicle, 3, { place_x, place_y, vehicle->z + 10 }, LightType::lantern3);
            place_x += kOffsetLookup[(vehicle->orientation + 0) % 32] * 2;
            place_y += kOffsetLookup[(vehicle->orientation + 8) % 32] * 2;
            Add3DLight(*vehicle, 4, { place_x, place_y, vehicle->z + 2 }, LightType::lantern3);
        }
    }
    void AddLightsMagicVehicle_MiniatureRailway(const Vehicle* vehicle)
    {
        if (vehicle == vehicle->TrainHead())
        {
            int16_t place_x = vehicle->x - kOffsetLookup[(vehicle->orientation + 0) % 32] * 2;
            int16_t place_y = vehicle->y - kOffsetLookup[(vehicle->orientation + 8) % 32] * 2;
            Add3DLight(*vehicle, 1, { place_x, place_y, vehicle->z + 10 }, LightType::lantern3);
            place_x -= kOffsetLookup[(vehicle->orientation + 0) % 32] * 2;
            place_y -= kOffsetLookup[(vehicle->orientation + 8) % 32] * 2;
            Add3DLight(*vehicle, 2, { place_x, place_y, vehicle->z + 2 }, LightType::lantern3);
        }
        else
        {
            Add3DLight(*vehicle, 0, { vehicle->x, vehicle->y, vehicle->z + 10 }, LightType::lantern3);
        }
    }

    void AddLightsMagicVehicle(const Vehicle* vehicle)
    {
        auto ride = vehicle->GetRide();
        if (ride == nullptr)
            return;

        const auto& rtd = GetRideTypeDescriptor(ride->type);
        if (rtd.LightFXAddLightsMagicVehicle != nullptr)
            rtd.LightFXAddLightsMagicVehicle(vehicle);
    }

    void AddKioskLights(const CoordsXY& mapPosition, const int32_t height, const uint8_t zOffset)
    {
        uint8_t relativeRotation = (4 - GetCurrentRotation()) % 4;
        CoordsXY lanternOffset1 = CoordsXY(0, 16).rotate(relativeRotation);
        CoordsXY lanternOffset2 = CoordsXY(16, 0).rotate(relativeRotation);
        Add3DLightMagicFromDrawingTile(mapPosition, lanternOffset1.x, lanternOffset1.y, height + zOffset, LightType::lantern3);
        Add3DLightMagicFromDrawingTile(mapPosition, lanternOffset2.x, lanternOffset2.y, height + zOffset, LightType::lantern3);
        Add3DLightMagicFromDrawingTile(mapPosition, 8, 32, height, LightType::spot1);
        Add3DLightMagicFromDrawingTile(mapPosition, 32, 8, height, LightType::spot1);
        Add3DLightMagicFromDrawingTile(mapPosition, -32, 8, height, LightType::spot1);
        Add3DLightMagicFromDrawingTile(mapPosition, 8, -32, height, LightType::spot1);
        Add3DLightMagicFromDrawingTile(mapPosition, -8, 32, height, LightType::spot1);
        Add3DLightMagicFromDrawingTile(mapPosition, 32, -8, height, LightType::spot1);
        Add3DLightMagicFromDrawingTile(mapPosition, -32, -8, height, LightType::spot1);
        Add3DLightMagicFromDrawingTile(mapPosition, -8, -32, height, LightType::spot1);
    }

    void AddShopLights(const CoordsXY& mapPosition, const uint8_t direction, const int32_t height, const uint8_t zOffset)
    {
        if (direction == (4 - GetCurrentRotation()) % 4) // Back Right Facing Stall
        {
            CoordsXY spotOffset1 = CoordsXY(-32, 8).rotate(direction);
            CoordsXY spotOffset2 = CoordsXY(-32, 4).rotate(direction);
            Add3DLightMagicFromDrawingTile(mapPosition, spotOffset1.x, spotOffset1.y, height, LightType::spot1);
            Add3DLightMagicFromDrawingTile(mapPosition, spotOffset2.x, spotOffset2.y, height, LightType::spot2);
        }
        else if (direction == (7 - GetCurrentRotation()) % 4) // Back left Facing Stall
        {
            CoordsXY spotOffset1 = CoordsXY(-32, -8).rotate(direction);
            CoordsXY spotOffset2 = CoordsXY(-32, -4).rotate(direction);
            Add3DLightMagicFromDrawingTile(mapPosition, spotOffset1.x, spotOffset1.y, height, LightType::spot1);
            Add3DLightMagicFromDrawingTile(mapPosition, spotOffset2.x, spotOffset2.y, height, LightType::spot2);
        }
        else // Forward Facing Stall
        {
            CoordsXY spotOffset1 = CoordsXY(-32, 8).rotate(direction);
            CoordsXY spotOffset2 = CoordsXY(-32, -8).rotate(direction);
            CoordsXY lanternOffset = CoordsXY(-16, 0).rotate(direction);
            Add3DLightMagicFromDrawingTile(
                mapPosition, lanternOffset.x, lanternOffset.y, height + zOffset, LightType::lantern3);
            Add3DLightMagicFromDrawingTile(mapPosition, spotOffset1.x, spotOffset1.y, height, LightType::spot1);
            Add3DLightMagicFromDrawingTile(mapPosition, spotOffset2.x, spotOffset2.y, height, LightType::spot1);
        }
    }

    void ApplyPaletteFilter(uint8_t i, uint8_t* r, uint8_t* g, uint8_t* b)
    {
        const uint8_t dayR = *r;
        const uint8_t dayG = *g;
        const uint8_t dayB = *b;

        auto& gameState = getGameState();

        float night = static_cast<float>(pow(gDayNightCycle, 1.5));

        float natLightR = 1.0f;
        float natLightG = 1.0f;
        float natLightB = 1.0f;

        float elecMultR = 1.05f;
        float elecMultG = 0.98f;
        float elecMultB = 0.75f;

        static float wetness = 0.0f;
        static float fogginess = 0.0f;
        static float lightPolution = 0.0f;

        float sunLight = std::max(0.0f, std::min(1.0f, 2.0f - night * 3.0f));

        // Night version
        natLightR = FLerp(natLightR * 4.0f, 0.635f, (std::pow(night, 0.035f + sunLight * 10.50f)));
        natLightG = FLerp(natLightG * 4.0f, 0.650f, (std::pow(night, 0.100f + sunLight * 5.50f)));
        natLightB = FLerp(natLightB * 4.0f, 0.850f, (std::pow(night, 0.200f + sunLight * 1.5f)));

        float overExpose = 0.0f;
        float lightAvg = (natLightR + natLightG + natLightB) / 3.0f;
#ifdef LIGHTFX_UNKNOWN_PART_2
        float lightMax = (natLightR + natLightG + natLightB) / 3.0f;
#endif // LIGHTFX_UNKNOWN_PART_2

        //  overExpose += ((lightMax - lightAvg) / lightMax) * 0.01f;

        if (gameState.weatherCurrent.temperature > 20)
        {
            float offset = (static_cast<float>(gameState.weatherCurrent.temperature - 20)) * 0.04f;
            offset *= 1.0f - night;
            lightAvg /= 1.0f + offset;
            //      overExpose += offset * 0.1f;
        }

#ifdef LIGHTFX_UNKNOWN_PART_2
        lightAvg += (lightMax - lightAvg) * 0.6f;
#endif // LIGHTFX_UNKNOWN_PART_2

        if (lightAvg > 1.0f)
        {
            natLightR /= lightAvg;
            natLightG /= lightAvg;
            natLightB /= lightAvg;
        }

        natLightR *= 1.0f + overExpose;
        natLightG *= 1.0f + overExpose;
        natLightB *= 1.0f + overExpose;
        overExpose *= 255.0f;

        float targetFogginess = static_cast<float>(gameState.weatherCurrent.level) / 8.0f;
        targetFogginess += (night * night) * 0.15f;

        if (gameState.weatherCurrent.temperature < 10)
        {
            targetFogginess += (static_cast<float>(10 - gameState.weatherCurrent.temperature)) * 0.01f;
        }

        fogginess -= (fogginess - targetFogginess) * 0.00001f;

        wetness *= 0.999995f;
        wetness += fogginess * 0.001f;
        wetness = std::min(wetness, 1.0f);

        float boost = 1.0f;
        float envFog = fogginess;
        float lightFog = envFog;

        float addLightNatR = 0.0f;
        float addLightNatG = 0.0f;
        float addLightNatB = 0.0f;

        float reduceColourNat = 1.0f;
        float reduceColourLit = 1.0f;

        reduceColourLit *= night / static_cast<float>(std::pow(std::max(1.01f, 0.4f + lightAvg), 2.0));

        float targetLightPollution = reduceColourLit
            * std::max(0.0f, 0.0f + 0.000001f * static_cast<float>(GetLightPollution()));
        lightPolution -= (lightPolution - targetLightPollution) * 0.001f;

        //  lightPollution /= 1.0f + fogginess * 1.0f;

        natLightR /= 1.0f + lightPolution * 20.0f;
        natLightG /= 1.0f + lightPolution * 20.0f;
        natLightB /= 1.0f + lightPolution * 20.0f;
        natLightR += elecMultR * 0.6f * lightPolution;
        natLightG += elecMultG * 0.6f * lightPolution;
        natLightB += elecMultB * 0.6f * lightPolution;
        natLightR /= 1.0f + lightPolution;
        natLightG /= 1.0f + lightPolution;
        natLightB /= 1.0f + lightPolution;

        reduceColourLit += static_cast<float>(gameState.weatherCurrent.level) / 2.0f;

        reduceColourNat /= 1.0f + fogginess;
        reduceColourLit /= 1.0f + fogginess;

        lightFog *= reduceColourLit;

        reduceColourNat *= 1.0f - envFog;
        reduceColourLit *= 1.0f - lightFog;

        float fogR = 35.5f * natLightR * 1.3f;
        float fogG = 45.0f * natLightG * 1.3f;
        float fogB = 50.0f * natLightB * 1.3f;
        lightFog *= 10.0f;

        float wetnessBoost = 1.0f; // 1.0f + wetness * wetness * 0.1f;

        if (night >= 0 && Weather::gLightningFlash != 1)
        {
            *r = Lerp(*r, SoftLight(*r, 8), night);
            *g = Lerp(*g, SoftLight(*g, 8), night);
            *b = Lerp(*b, SoftLight(*b, 128), night);

            //  if (i == 32)
            //      boost = 300000.0f;
            if ((i % 32) == 0)
                boost = 1.01f * wetnessBoost;
            else if ((i % 16) < 7)
                boost = 1.001f * wetnessBoost;
            if (i > 230 && i < 232)
                boost = (static_cast<float>(*b)) / 64.0f;

            if (false)
            {
                // This experiment shifts the colour of pixels as-if they are wet, but it is not a pretty solution at all
                if ((i % 16))
                {
                    float iVal = (static_cast<float>((i + 12) % 16)) / 16.0f;
                    float eff = (wetness * (static_cast<float>(std::pow(iVal, 1.5)) * 0.85f));
                    reduceColourNat *= 1.0f - eff;
                    addLightNatR += fogR * eff * 3.95f;
                    addLightNatG += fogR * eff * 3.95f;
                    addLightNatB += fogR * eff * 3.95f;
                }
            }

            addLightNatR *= 1.0f - envFog;
            addLightNatG *= 1.0f - envFog;
            addLightNatB *= 1.0f - envFog;

            *r = static_cast<uint8_t>(std::min(
                255.0f,
                std::max(
                    0.0f,
                    (-overExpose + static_cast<float>(*r) * reduceColourNat * natLightR + envFog * fogR + addLightNatR))));
            *g = static_cast<uint8_t>(std::min(
                255.0f,
                std::max(
                    0.0f,
                    (-overExpose + static_cast<float>(*g) * reduceColourNat * natLightG + envFog * fogG + addLightNatG))));
            *b = static_cast<uint8_t>(std::min(
                255.0f,
                std::max(
                    0.0f,
                    (-overExpose + static_cast<float>(*b) * reduceColourNat * natLightB + envFog * fogB + addLightNatB))));

            float litR = static_cast<float>(dayR) * elecMultR * boost + lightFog;
            float litG = static_cast<float>(dayG) * elecMultG * boost + lightFog;
            float litB = static_cast<float>(dayB) * elecMultB * boost + lightFog;

            auto dstEntry = &gPalette_light[i];
            dstEntry->red = static_cast<uint8_t>(std::clamp(FLerp(static_cast<float>(dayR), litR, night), 0.0f, 255.0f));
            dstEntry->green = static_cast<uint8_t>(std::clamp(FLerp(static_cast<float>(dayG), litG, night), 0.0f, 255.0f));
            dstEntry->blue = static_cast<uint8_t>(std::clamp(FLerp(static_cast<float>(dayB), litB, night), 0.0f, 255.0f));
            dstEntry->alpha = 0;
        }
        else
        {
            auto dstEntry = &gPalette_light[i];
            dstEntry->red = dayR;
            dstEntry->green = dayG;
            dstEntry->blue = dayB;
            dstEntry->alpha = 0;
        }
    }

    static inline uint32_t LerpColour(uint32_t darkC, uint32_t lightC, uint32_t intensity)
    {
        // Symmetric 32-bit channel interpolation preserving native alpha layout
        int32_t b0 = static_cast<int32_t>(darkC & 0xFF);
        int32_t g0 = static_cast<int32_t>((darkC >> 8) & 0xFF);
        int32_t r0 = static_cast<int32_t>((darkC >> 16) & 0xFF);
        uint32_t a0 = darkC & 0xFF000000;

        int32_t b1 = static_cast<int32_t>(lightC & 0xFF);
        int32_t g1 = static_cast<int32_t>((lightC >> 8) & 0xFF);
        int32_t r1 = static_cast<int32_t>((lightC >> 16) & 0xFF);

        int32_t factor = static_cast<int32_t>(intensity);
        uint32_t b = static_cast<uint32_t>(std::clamp(b0 + (((b1 - b0) * factor) >> 8), 0, 255));
        uint32_t g = static_cast<uint32_t>(std::clamp(g0 + (((g1 - g0) * factor) >> 8), 0, 255));
        uint32_t r = static_cast<uint32_t>(std::clamp(r0 + (((r1 - r0) * factor) >> 8), 0, 255));

        // Subtle warm luminous core for peak lamp/spotlight centers
        if (intensity > 200)
        {
            uint32_t glow = (intensity - 200) >> 2; // 0..13
            b = std::min<uint32_t>(255, b + (glow >> 1)); // warm tungsten
            g = std::min<uint32_t>(255, g + glow);
            r = std::min<uint32_t>(255, r + glow);
        }

        return a0 | (r << 16) | (g << 8) | b;
    }

    void RenderToTexture(
        const Viewport& vp, void* dstPixels, uint32_t dstPitch, PaletteIndex* bits, uint32_t width, uint32_t height,
        const uint32_t* palette, const uint32_t* lightPalette)
    {
        float night = static_cast<float>(pow(gDayNightCycle, 1.5));
        if (night <= 0.001f)
        {
            // Broad daylight fast-path: artificial lights have negligible contrast against full sunlight.
            // Directly blit the daytime palette, saving 100% of light rasterization overhead and guaranteeing zero smudges.
            for (uint32_t y = 0; y < height; y++)
            {
                uintptr_t dstOffset = static_cast<uintptr_t>(y * dstPitch);
                uint32_t* dst = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(dstPixels) + dstOffset);
                const PaletteIndex* srcRow = bits + (y * width);
                for (uint32_t x = 0; x < width; x++)
                {
                    *dst++ = palette[EnumValue(srcRow[x])];
                }
            }
            return;
        }

        UpdateViewportSettings(vp);
        SwapBuffers();
        PrepareLightList(vp);
        RenderLightsToFrontBuffer();

        uint8_t* lightBits = static_cast<uint8_t*>(GetFrontBuffer());
        if (lightBits == nullptr)
        {
            return;
        }

        for (uint32_t y = 0; y < height; y++)
        {
            uintptr_t dstOffset = static_cast<uintptr_t>(y * dstPitch);
            uint32_t* dst = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(dstPixels) + dstOffset);
            const uint32_t rowOffset = y * width;
            const PaletteIndex* srcRow = bits + rowOffset;
            const uint8_t* lightRow = lightBits + rowOffset;

            for (uint32_t x = 0; x < width; x++)
            {
                uint8_t lightIntensity = lightRow[x];
                PaletteIndex src = srcRow[x];
                uint32_t darkColour = palette[EnumValue(src)];

                if (lightIntensity == 0)
                {
                    *dst++ = darkColour;
                }
                else
                {
                    uint32_t lightColour = lightPalette[EnumValue(src)];
                    *dst++ = LerpColour(darkColour, lightColour, lightIntensity);
                }
            }
        }
    }
} // namespace OpenRCT2::Drawing::LightFx
