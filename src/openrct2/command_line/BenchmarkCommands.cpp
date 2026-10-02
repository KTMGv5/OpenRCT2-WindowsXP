/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../Context.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../core/Console.hpp"
#include "../entity/EntityRegistry.h"
#include "../network/NetworkTypes.h"
#include "CommandLine.hpp"

#include <chrono>
#include <memory>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

using namespace OpenRCT2::CommandLine;

namespace OpenRCT2
{
    // clang-format off
    static constexpr CommandLineOptionDefinition kNoOptions[]
    {
        kOptionTableEnd
    };

    static ExitCode HandleBenchmark(CommandLineArgEnumerator* argEnumerator);

    const CommandLineCommand CommandLine::kBenchmarkCommands[]{
        // Main commands
        DefineCommand("", "<park file> [ticks]", kNoOptions, HandleBenchmark),
        kCommandTableEnd
    };
    // clang-format on

    static ExitCode HandleBenchmark(CommandLineArgEnumerator* argEnumerator)
    {
        const utf8* inputPath;
        if (!argEnumerator->TryPopString(&inputPath))
        {
            Console::Error::WriteLine("Usage: openrct2 benchmark <park file> [ticks]");
            return ExitCode::fail;
        }

        int32_t ticks = 1000;
        if (argEnumerator->HasMoreArgs())
        {
            argEnumerator->TryPopInteger(&ticks);
        }

        if (ticks <= 0)
        {
            ticks = 1000;
        }

        gOpenRCT2Headless = true;

#ifndef DISABLE_NETWORK
        gNetworkStart = Network::Mode::server;
#endif

        std::unique_ptr<IContext> context(CreateContext());
        if (!context->Initialise())
        {
            Console::Error::WriteLine("Context initialization failed.");
            return ExitCode::fail;
        }

        if (!context->LoadParkFromFile(inputPath))
        {
            Console::Error::WriteLine("Failed to load park file: %s", inputPath);
            return ExitCode::fail;
        }

        Console::WriteLine("============================================================");
        Console::WriteLine("       OpenRCT2: Windows XP Edition - Benchmark Suite       ");
        Console::WriteLine("============================================================");
        Console::WriteLine("Park:              %s", inputPath);
        Console::WriteLine("Simulation Target: %d ticks", ticks);
        Console::WriteLine("Simulating, please wait...");

        auto startTime = std::chrono::high_resolution_clock::now();

        for (int32_t i = 0; i < ticks; i++)
        {
            gameStateUpdateLogic();
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        double elapsedSec = elapsed.count();
        if (elapsedSec <= 0.000001)
        {
            elapsedSec = 0.000001;
        }

        double ticksPerSec = static_cast<double>(ticks) / elapsedSec;
        double realtimeMultiplier = ticksPerSec / 40.0;
        auto numGuests = getGameState().park.numGuestsInPark;

        uint32_t xpMark = static_cast<uint32_t>((ticksPerSec * 100.0) + (numGuests * 2.0));

        const char* rating = "Good";
        if (xpMark > 60000)
            rating = "Legendary (Modern Enthusiast)";
        else if (xpMark > 40000)
            rating = "Excellent (High-End Retro Rig)";
        else if (xpMark > 20000)
            rating = "Great (Standard Vintage PC)";
        else if (xpMark > 10000)
            rating = "Fair (Era-Accurate Pentium 4)";
        else
            rating = "Entry-Level (Vintage Pentium III)";

        Console::WriteLine("------------------------------------------------------------");
        Console::WriteLine("Elapsed Time:      %.3f seconds", elapsedSec);
        Console::WriteLine("Simulation Rate:   %.2f ticks/sec (%.2fx realtime)", ticksPerSec, realtimeMultiplier);
        Console::WriteLine("Guests in Park:    %u", numGuests);

#ifdef _WIN32
        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus))
        {
            uint32_t totalRamMB = static_cast<uint32_t>(memStatus.ullTotalPhys / (1024 * 1024));
            uint32_t availRamMB = static_cast<uint32_t>(memStatus.ullAvailPhys / (1024 * 1024));
            uint32_t usedRamMB = totalRamMB > availRamMB ? (totalRamMB - availRamMB) : 0;
            Console::WriteLine("Host RAM Usage:    %u MB used / %u MB total (%u%% in use)",
                               usedRamMB, totalRamMB, memStatus.dwMemoryLoad);
        }
#endif

        Console::WriteLine("------------------------------------------------------------");
        Console::WriteLine("XP-MARK SCORE:     %u XP-Marks", xpMark);
        Console::WriteLine("Hardware Rating:   [%s]", rating);
        Console::WriteLine("State Checksum:    %s", getGameState().entities.getAllEntitiesChecksum().toString().c_str());
        Console::WriteLine("============================================================");

        return ExitCode::ok;
    }
} // namespace OpenRCT2
