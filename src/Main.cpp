#include "vmread/hlapi/hlapi.h"
#include "utils/Logger.h"
#include "Interfaces.h"
#include "Netvars.h"
#include "utils/Memutils.h"
#include "features/Aimbot.h"
#include "features/Bhop.h"
#include "features/Glow.h"
#include "features/DumbExploits.h"
#include "sdk/CBaseEntity.h"
#include "sdk/CGlobalVars.h"
#include "utils/Memutils.h"
#include "globals.h"
#include "utils/Wrappers.h"
#include "utils/minitrace.h"
#include "utils/InputSystem.h"

#include "m0dular/utils/threading.h"
#include "m0dular/utils/pattern_scan.h"

#include <unistd.h> //getpid
#include <thread>
#include <atomic>
#include <csignal>
#include <numeric>
#include <thread>
#include <chrono>
#include <iostream>
//#include <tclDecls.h>

//#define USE_EAC_LAUNCHER

#ifdef USE_EAC_LAUNCHER
#define PROCNAME "EasyAntiCheat_"
#define MODNAME "EasyAntiCheat_launcher.exe"
#else
#define PROCNAME "r5apex.exe"
#define MODNAME "R5Apex.exe"
#endif

#include "Signatures.h"

static thread_t mainThread;
static thread_t inputSystemThread;

#if (LMODE() == MODE_EXTERNAL())

int main() {
    while (running) {
        char c = (char) getchar();

        if (c == 'Q')
            break;
    }

    return 0;
}

#endif

typedef std::chrono::high_resolution_clock Clock;

static bool sigscanFailed = false;

static void *ThreadSignature(const Signature *sig) {
    MTR_SCOPED_TRACE("Initialization", "ThreadedSignature");

    *sig->result = PatternScan::FindPattern(sig->pattern, sig->module);

    if (!*sig->result) {
        Logger::Log("Failed to find pattern {%s}\n", sig->pattern);
        sigscanFailed = true;
    }

    return nullptr;
}


static void *MainThread(void *) {
    Logger::Log("Main Loaded.\n");
    pid_t pid;

#if (LMODE() == MODE_EXTERNAL())
    FILE *pipe = popen("pidof qemu-system-x86_64", "r");
    fscanf(pipe, "%d", &pid);
    pclose(pipe);
#else
    pid = getpid();
#endif

#ifdef MTR_ENABLED
    Logger::Log("Initialize performance tracing...\n");
    mtr_init("/tmp/ape-ex-trace.json");
    MTR_META_PROCESS_NAME("Ape-ex");
#endif

    Threading::InitThreads();

    try {
        Logger::Log("doing shit\n");
        MTR_BEGIN("Initialization", "InitCTX");
        WinContext ctx(pid);
        MTR_END("Initialization", "InitCTX");

        MTR_BEGIN("Initialization", "FindProcesses");
        ctx.processList.Refresh();
        for (auto &i : ctx.processList) {
            if (!strcasecmp(PROCNAME, i.proc.name)) {
                Logger::Log("\nFound Apex Process %s(PID:%ld)", i.proc.name, i.proc.pid);
                PEB peb = i.GetPeb();
                short magic = i.Read<short>(peb.ImageBaseAddress);
                uintptr_t translatedBase = VTranslate(&i.ctx->process, i.proc.dirBase, peb.ImageBaseAddress);
                Logger::Log("\tWinBase:\t%p\tBase:\t%p\tQemuBase:\t%p\tMagic:\t%hx (valid: %hhx)\n", (void *) peb.ImageBaseAddress, (void *) i.proc.process,
                            (void *) translatedBase,
                            magic, (char) (magic == IMAGE_DOS_SIGNATURE));
                process = &i;

                for (auto &o : i.modules) {
                    if (!strcasecmp(MODNAME, o.info.name)) {
                        apexBase = o.info.baseAddress;
                        for (auto &u : o.exports)
                            Logger::Log("\t\t%lx\t%s\n", u.address, u.name);
                    }
                }

            }
        }
        MTR_END("Initialization", "FindProcesses");

        if (!process) {
            Logger::Log("Could not Find Apex Process/Base. Exiting...\n");
            goto quit;
        }

        auto t1 = Clock::now();

        MTR_BEGIN("Initialization", "FindOffsets");
        Threading::QueueJobRef(Interfaces::FindInterfaces, MODNAME);
        Threading::QueueJobRef(Netvars::CacheNetvars, MODNAME);
        //Netvars::PrintNetvars(*process, MODNAME);

        for (const Signature &sig : signatures)
            Threading::QueueJobRef(ThreadSignature, &sig);

        Threading::FinishQueue(true);
        MTR_END("Initialization", "FindOffsets");

        if (sigscanFailed) {
            Logger::Log("One of the sigs failed. Stopping.\n");
            goto quit;
        }

        // Print some sig stuff - useful for reclass analysis etc
        Logger::Log("Localplayer: %p\n", (void *) GetLocalPlayer());
        Logger::Log("LocalplayerPtr: %p\n", (void *) localPlayerPtr);
        Logger::Log("(Linux)Localplayer: %p\n", (void *) &localPlayer);
        Logger::Log("Entlist: %p\n", (void *) entList);
        Logger::Log("GlobalVars: %p\n", (void *) globalVarsAddr);
        Logger::Log("input: %p\n", (void *) inputAddr);
        Logger::Log("clientstate: %p\n", (void *) clientStateAddr);
        Logger::Log("forcejump: %p\n", (void *) forceJump);

        auto t2 = Clock::now();
        printf("Initialization time: %lld ms\n", (long long) std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());

        Logger::Log("Starting Main Loop.\n");

        static int lastFrame = 0;
        static int lastTick = 0;
        static bool updateWrites = false;

        // these buffers wont get re-allocated, getting the address of em' here is fine.
        userCmdArr = process->Read<uintptr_t>(inputAddr + OFFSET_OF(&CInput::m_commands));
        verifiedUserCmdArr = process->Read<uintptr_t>(inputAddr + OFFSET_OF(&CInput::m_verifiedCommands));
        //goto quit;
        while (running) {
            globalVars = process->Read<CGlobalVars>(globalVarsAddr);

            // read first 0x344 bytes of clientstate (next member we want after 0x344 is over 100k bytes away)
            VMemRead(&process->ctx->process, process->proc.dirBase, (uint64_t) &clientState, clientStateAddr, 0x344);
            netChan = process->Read<CNetChan>((uint64_t) clientState.m_netChan);

            /* Per Tick Operations */
            updateWrites = (globalVars.tickCount != lastTick || globalVars.framecount != lastFrame);
            // reset fakelag if we arent ingame
            /*if (clientState.m_signonState != SIGNONSTATE_INGAMEAPEX)
                process->Write<double>(clientStateAddr + OFFSET_OF(&CClientState::m_nextCmdTime), 0.0);*/

            if (updateWrites) {
                /* -=-=-=-=-=-=-=-=-= Tick Operations -=-=-=-=-=-=-=-=-=-=-= */
                MTR_SCOPED_TRACE("MainLoop", "Tick");

                int entityCount = process->Read<int>(apexBase + 0x1adac1c); // TODO: fix and sig

                if (!entityCount || entityCount > 50000) {
                    entityCount = 100; // hardcoded to 100 as item esp is disabled
                }
                InputSystem::InputSystem();

                validEntities.clear();

                for (int ent = 0; ent < entityCount; ent++) {
                    uintptr_t entity = GetEntityById(ent);
                    if (!entity) continue;

                    bool isPlayer = IsPlayer(entity);

                    if (!isPlayer) {
                        if (!IsProp(entity)) continue;
                    }

                    validEntities.push_back(ent);
                    entities[ent].Update(entity);
                    entities[ent].SetPlayerState(isPlayer);
                }
                localPlayer.Update(GetLocalPlayer());

                //Vector localPos = localPlayer.eyePos;
                //Logger::Log("Local eyepos: (%f/%f/%f)\n", localPos[0], localPos[1], localPos[2]);
                Exploits::Speedhack();

                Aimbot::Aimbot();
                Bhop::Bhop(localPlayer);
                Bhop::Strafe();
                /*int32_t commandNr= process->Read<int32_t>(clientStateAddr + OFFSET_OF(&CClientState::m_lastUsedCommandNr));
                int32_t targetCommand = (commandNr - 1) % 300;
                CUserCmd userCmd = process->Read<CUserCmd>(userCmdArr + targetCommand * sizeof(CUserCmd));
                QAngle recoil = Aimbot::RecoilCompensation();

                sway_history.insert({commandNr, recoil});
                */


                /* -=-=-=-=-=-=-=-=-= Frame Operations -=-=-=-=-=-=-=-=-=-=-= */
                MTR_SCOPED_TRACE("MainLoop", "Frame");

                Glow::Glow();
                Exploits::ServerCrasher();
                lastFrame = globalVars.framecount;

                /* -=-=-=-=-=-=-=-=-= Memory Operations -=-=-=-=-=-=-=-=-=-=-= */

                MTR_SCOPED_TRACE("MainLoop", "WriteBack");
                WriteList writeList(process);
                for (size_t i : validEntities) {
                    if (!entities[i].GetPlayerState()) // Do not write item structs; race condition problem
                        continue;

                    entities[i].WriteBack(writeList);
                }

                localPlayer.WriteBack(writeList);

                writeList.Commit();

                lastTick = globalVars.tickCount;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(2000));
        }

        // reset these values to properly reset after exiting the cheat
        //process->Write<double>(clientStateAddr + OFFSET_OF(&CClientState::m_nextCmdTime), 0.0);
        //process->Write<float>(timescale, 1.0f); // reset speedhack // reset speedhack

        Logger::Log("Main Loop Ended.\n");
    } catch (VMException &e) {
        Logger::Log("Initialization error: %d\n", e.value);
    }

    quit:
    running = false;

    Threading::FinishQueue(true);
    Threading::EndThreads();

#ifdef MTR_ENABLED
    mtr_flush();
    mtr_shutdown();
#endif

    Logger::Log("Main Ended.\n");

    return nullptr;
}

static void __attribute__((constructor)) Startup() {
    //inputSystemThread = Threading::StartThread(InputSystem::InputSystem, nullptr, false);
    mainThread = Threading::StartThread(MainThread, nullptr, false);
}

static void __attribute__((destructor)) Shutdown() {
    Logger::Log("Unloading...");

    running = false;

    //Threading::JoinThread(inputSystemThread, nullptr);
    Threading::JoinThread(mainThread, nullptr);

    Logger::Log("Done\n");
}
