/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "Paint.Staff.h"

#include "../../drawing/LightFX.h"
#include "../../entity/Staff.h"
#include "../../profiling/Profiling.h"
#include "../Paint.h"
#include "Paint.Peep.h"

using namespace OpenRCT2;

namespace LightFx = OpenRCT2::Drawing::LightFx;

void PaintStaff(PaintSession& session, const Staff& staff, int32_t orientation)
{
    PROFILED_FUNCTION();

    if (session.rt.zoom_level > ZoomLevel{ 2 })
    {
        return;
    }

    PaintStaffLightingEffects(staff);

    Direction direction = (orientation >> 3);
    auto baseImageData = PaintPeepGetBaseImageAndOffset(staff, direction);
    auto imageId = ImageId(baseImageData.baseImageId, staff.tShirtColour);

    PaintAddImageAsParent(session, imageId, kPaintPeepOffset(staff.z), kPaintPeepBoundBox(staff.z));
}

static constexpr int16_t kStaffFlashlightOffset[] = {
    10, 10, 9, 8, 7, 6, 4, 2, 0, -2, -4, -6, -7, -8, -9, -10, -10, -10, -9, -8, -7, -6, -4, -2, 0, 2, 4, 6, 7, 8, 9, 10,
};

void PaintStaffLightingEffects(const Staff& staff)
{
    if (!LightFx::IsAvailable())
        return;

    // Only activate flashlight when staff member is actively walking or moving
    bool isMoving = (staff.state == PeepState::walking ||
                     staff.state == PeepState::patrolling ||
                     staff.state == PeepState::headingToInspection ||
                     staff.state == PeepState::answering ||
                     staff.state == PeepState::sweeping ||
                     staff.state == PeepState::mowing ||
                     staff.state == PeepState::watering);
    if (!isMoving)
    {
        return;
    }

    auto loc = staff.getLocation();
    uint8_t ori = staff.orientation % 32;
    // Cast beam forward 15 units in the direction of movement, on the ground ahead of their feet
    loc.x -= (kStaffFlashlightOffset[(ori + 0) % 32] * 3) / 2;
    loc.y -= (kStaffFlashlightOffset[(ori + 8) % 32] * 3) / 2;
    loc.z = staff.z + 1;

    LightFx::Add3DLight(staff, 0, loc, LightFx::LightType::spot1, ori);
}
