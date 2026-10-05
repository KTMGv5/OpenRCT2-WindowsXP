/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "PaletteType.h"

#include <cstdint>

struct CoordsXY;
struct CoordsXYZ;

namespace OpenRCT2
{
    struct EntityBase;
    struct Vehicle;
    struct Viewport;
} // namespace OpenRCT2

namespace OpenRCT2::Drawing
{
    struct RenderTarget;
    enum class PaletteIndex : uint8_t;
} // namespace OpenRCT2::Drawing

namespace OpenRCT2::Drawing::LightFx
{
    enum class LightType : uint8_t
    {
        none = 0,

        lantern0 = 4,
        lantern1 = 5,
        lantern2 = 6,
        lantern3 = 7,

        spot0 = 8,
        spot1 = 9,
        spot2 = 10,
        spot3 = 11,
    };

    inline void SetAvailable([[maybe_unused]] bool available) {}
    constexpr bool IsAvailable() { return false; }
    constexpr bool ForVehiclesIsAvailable() { return false; }

    inline void Init() {}

    inline void UpdateBuffers([[maybe_unused]] RenderTarget& rt) {}
    const GamePalette& GetPalette();

    inline void Add3DLight([[maybe_unused]] const EntityBase& entity, [[maybe_unused]] uint8_t id, [[maybe_unused]] const CoordsXYZ& loc, [[maybe_unused]] LightType lightType, [[maybe_unused]] uint8_t orientation = 0xFF) {}

    inline void Add3DLightMagicFromDrawingTile(
        [[maybe_unused]] const CoordsXY& mapPosition, [[maybe_unused]] int16_t offsetX, [[maybe_unused]] int16_t offsetY, [[maybe_unused]] int16_t offsetZ, [[maybe_unused]] LightType lightType) {}

    inline void AddLightsMagicVehicle([[maybe_unused]] const Vehicle* vehicle) {}
    inline void AddLightsMagicVehicle_ObservationTower([[maybe_unused]] const Vehicle* vehicle) {}
    inline void AddLightsMagicVehicle_MineTrainCoaster([[maybe_unused]] const Vehicle* vehicle) {}
    inline void AddLightsMagicVehicle_ChairLift([[maybe_unused]] const Vehicle* vehicle) {}
    inline void AddLightsMagicVehicle_BoatHire([[maybe_unused]] const Vehicle* vehicle) {}
    inline void AddLightsMagicVehicle_Monorail([[maybe_unused]] const Vehicle* vehicle) {}
    inline void AddLightsMagicVehicle_MiniatureRailway([[maybe_unused]] const Vehicle* vehicle) {}

    inline void AddKioskLights([[maybe_unused]] const CoordsXY& mapPosition, [[maybe_unused]] uint8_t direction, [[maybe_unused]] int32_t height, [[maybe_unused]] uint8_t zOffset) {}
    inline void AddKioskLights([[maybe_unused]] const CoordsXY& mapPosition, [[maybe_unused]] int32_t height, [[maybe_unused]] uint8_t zOffset) {}
    inline void AddShopLights([[maybe_unused]] const CoordsXY& mapPosition, [[maybe_unused]] uint8_t direction, [[maybe_unused]] int32_t height, [[maybe_unused]] uint8_t zOffset) {}

    inline void ApplyPaletteFilter([[maybe_unused]] uint8_t i, [[maybe_unused]] uint8_t* r, [[maybe_unused]] uint8_t* g, [[maybe_unused]] uint8_t* b) {}
    inline void RenderToTexture(
        [[maybe_unused]] const Viewport& vp, [[maybe_unused]] void* dstPixels, [[maybe_unused]] uint32_t dstPitch, [[maybe_unused]] Drawing::PaletteIndex* bits, [[maybe_unused]] uint32_t width, [[maybe_unused]] uint32_t height,
        [[maybe_unused]] const uint32_t* palette, [[maybe_unused]] const uint32_t* lightPalette) {}

} // namespace OpenRCT2::Drawing::LightFx
