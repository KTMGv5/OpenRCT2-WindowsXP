/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../config/Config.h"

using namespace OpenRCT2;

template<typename T>
class DrawingUniqueLock
{
    T& _mutex;

public:
    DrawingUniqueLock(T& mutex)
        : _mutex(mutex)
    {
        _mutex.lock();
    }
    ~DrawingUniqueLock()
    {
        _mutex.unlock();
    }
};

template<typename T>
class DrawingSharedLock
{
    T& _mutex;

public:
    DrawingSharedLock(T& mutex)
        : _mutex(mutex)
    {
        _mutex.lock_shared();
    }
    ~DrawingSharedLock()
    {
        _mutex.unlock_shared();
    }
};
