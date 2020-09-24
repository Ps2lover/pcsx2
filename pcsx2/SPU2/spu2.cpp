/* SPU2-X, A plugin for Emulating the Sound Processing Unit of the Playstation 2
 * Developed and maintained by the Pcsx2 Development Team.
 *
 * Original portions from SPU2ghz are (c) 2008 by David Quintana [gigaherz]
 *
 * SPU2-X is free software: you can redistribute it and/or modify it under the terms
 * of the GNU Lesser General Public License as published by the Free Software Found-
 * ation, either version 3 of the License, or (at your option) any later version.
 *
 * SPU2-X is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with SPU2-X.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Global.h"
#include "spu2.h"
#include "Dma.h"
#ifdef __linux__
#include "Linux/Dialogs.h"
#elif defined(_WIN32)
#include "Windows/Dialogs.h"
#endif

using namespace Threading;

MutexRecursive  mtx_SPU2Status;
bool SPU2_dummy_callback = false;

#include "svnrev.h"

#ifdef _MSC_VER
#define snprintf sprintf_s
#endif
int SampleRate = 48000;

static bool IsOpened = false;
static bool IsInitialized = false;

static u32 pClocks = 0;

u32 *cyclePtr = NULL;
u32 lClocks = 0;


//static bool cpu_detected = false;

static bool CheckSSE()
{
    return true;

#if 0
	if( !cpu_detected )
	{
		cpudetectInit();
		cpu_detected = true;
	}
	if( !x86caps.hasStreamingSIMDExtensions || !x86caps.hasStreamingSIMD2Extensions )
	{
		SysMessage( "Your CPU does not support SSE2 instructions.\nThe SPU2-X plugin requires SSE2 to run." );
		return false;
	}
	return true;
#endif
}

void SPU2configure()
{
    if (!CheckSSE())
        return;
    configure();
}

s32 SPU2test()
{
    if (!CheckSSE())
        return -1;

    ReadSettings();
    if (SndBuffer::Test() != 0) {
        // TODO : Implement a proper dialog that allows the user to test different audio out drivers.
        const wchar_t *wtf = mods[OutputModule]->GetIdent();
        SysMessage(L"The '%s' driver test failed.  Please configure\na different SoundOut module and try again.", wtf);
        return -1;
    }

    return 0;
}

// --------------------------------------------------------------------------------------
//  DMA 4/7 Callbacks from Core Emulator
// --------------------------------------------------------------------------------------

u16 *DMABaseAddr;

u32 SPU2ReadMemAddr(int core)
{
    return Cores[core].MADR;
}
void SPU2WriteMemAddr(int core, u32 value)
{
    Cores[core].MADR = value;
}

void SPU2setDMABaseAddr(uptr baseaddr)
{
    DMABaseAddr = (u16 *)baseaddr;
}

void SPU2setSettingsDir(const char *dir)
{
    CfgSetSettingsDir(dir);
}

void SPU2setLogDir(const char *dir)
{
    CfgSetLogDir(dir);
}

void SPU2readDMA4Mem(u16 *pMem, u32 size) // size now in 16bit units
{
    if (cyclePtr != NULL)
        TimeUpdate(*cyclePtr);

    FileLog("[%10d] SPU2 readDMA4Mem size %x\n", Cycles, size << 1);
    Cores[0].DoDMAread(pMem, size);
}

void SPU2writeDMA4Mem(u16 *pMem, u32 size) // size now in 16bit units
{
    if (cyclePtr != NULL)
        TimeUpdate(*cyclePtr);

    FileLog("[%10d] SPU2 writeDMA4Mem size %x at address %x\n", Cycles, size << 1, Cores[0].TSA);
#ifdef S2R_ENABLE
    if (!replay_mode)
        s2r_writedma4(Cycles, pMem, size);
#endif
    Cores[0].DoDMAwrite(pMem, size);
}

void SPU2interruptDMA4()
{
    FileLog("[%10d] SPU2 interruptDMA4\n", Cycles);
    Cores[0].Regs.STATX |= 0x80;
    //Cores[0].Regs.ATTR &= ~0x30;
}

void SPU2interruptDMA7()
{
    FileLog("[%10d] SPU2 interruptDMA7\n", Cycles);
    Cores[1].Regs.STATX |= 0x80;
    //Cores[1].Regs.ATTR &= ~0x30;
}

void SPU2readDMA7Mem(u16 *pMem, u32 size)
{
    if (cyclePtr != NULL)
        TimeUpdate(*cyclePtr);

    FileLog("[%10d] SPU2 readDMA7Mem size %x\n", Cycles, size << 1);
    Cores[1].DoDMAread(pMem, size);
}

void SPU2writeDMA7Mem(u16 *pMem, u32 size)
{
    if (cyclePtr != NULL)
        TimeUpdate(*cyclePtr);

    FileLog("[%10d] SPU2 writeDMA7Mem size %x at address %x\n", Cycles, size << 1, Cores[1].TSA);
#ifdef S2R_ENABLE
    if (!replay_mode)
        s2r_writedma7(Cycles, pMem, size);
#endif
    Cores[1].DoDMAwrite(pMem, size);
}

s32 SPU2reset()
{
    if (SndBuffer::Test() == 0 && SampleRate != 48000)
    {
        SampleRate = 48000;
        SndBuffer::Cleanup();

        try {
            SndBuffer::Init();
        }
        catch (std::exception& ex) {
            fprintf(stderr, "SPU2-X Error: Could not initialize device, or something.\nReason: %s", ex.what());
            SPU2close();
            return -1;
        }
    }
    else
        SampleRate = 48000;

    memset(spu2regs, 0, 0x010000);
    memset(_spu2mem, 0, 0x200000);
    memset(_spu2mem + 0x2800, 7, 0x10); // from BIOS reversal. Locks the voices so they don't run free.
    Cores[0].Init(0);
    Cores[1].Init(1);
    return 0;
}

s32 SPU2ps1reset()
{
    printf("RESET PS1 \n");

    if (SndBuffer::Test() == 0 && SampleRate != 44100)
    {
        SampleRate = 44100;
        SndBuffer::Cleanup();

        try {
            SndBuffer::Init();
        }
        catch (std::exception& ex) {
            fprintf(stderr, "SPU2-X Error: Could not initialize device, or something.\nReason: %s", ex.what());
            SPU2close();
            return -1;
        }
    }
    else
        SampleRate = 44100;

   /* memset(spu2regs, 0, 0x010000);
    memset(_spu2mem, 0, 0x200000);
    memset(_spu2mem + 0x2800, 7, 0x10); // from BIOS reversal. Locks the voices so they don't run free.
    Cores[0].Init(0);
    Cores[1].Init(1);*/
    return 0;
}

s32 SPU2init()
{
    assert(regtable[0x400] == NULL);

    if (IsInitialized) {
        printf(" * SPU2-X: Already initialized - Ignoring SPU2init signal.");
        return 0;
    }

    IsInitialized = true;
    SPU2_dummy_callback = false;

    ReadSettings();

#ifdef SPU2_LOG
    if (AccessLog()) {
        spu2Log = OpenLog(AccessLogFileName);
        setvbuf(spu2Log, NULL, _IONBF, 0);
        FileLog("SPU2init\n");
    }
#endif
    srand((unsigned)time(NULL));

    spu2regs = (s16 *)malloc(0x010000);
    _spu2mem = (s16 *)malloc(0x200000);

    // adpcm decoder cache:
    //  the cache data size is determined by taking the number of adpcm blocks
    //  (2MB / 16) and multiplying it by the decoded block size (28 samples).
    //  Thus: pcm_cache_data = 7,340,032 bytes (ouch!)
    //  Expanded: 16 bytes expands to 56 bytes [3.5:1 ratio]
    //    Resulting in 2MB * 3.5.

    pcm_cache_data = (PcmCacheEntry *)calloc(pcm_BlockCount, sizeof(PcmCacheEntry));

    if ((spu2regs == NULL) || (_spu2mem == NULL) || (pcm_cache_data == NULL)) {
        SysMessage("SPU2-X: Error allocating Memory\n");
        return -1;
    }

    // Patch up a copy of regtable that directly maps "NULLs" to SPU2 memory.

    memcpy(regtable, regtable_original, sizeof(regtable));

    for (uint mem = 0; mem < 0x800; mem++) {
        u16 *ptr = regtable[mem >> 1];
        if (!ptr) {
            regtable[mem >> 1] = &(spu2Ru16(mem));
        }
    }

    SPU2reset();

    DMALogOpen();
    InitADSR();

#ifdef S2R_ENABLE
    if (!replay_mode)
        s2r_open(Cycles, "replay_dump.s2r");
#endif
    return 0;
}

#ifdef _MSC_VER
// Bit ugly to have this here instead of in RealttimeDebugger.cpp, but meh :p
extern bool debugDialogOpen;
extern HWND hDebugDialog;

static INT_PTR CALLBACK DebugProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int wmId;

    switch (uMsg) {
        case WM_PAINT:
            return FALSE;
        case WM_INITDIALOG: {
            debugDialogOpen = true;
        } break;

        case WM_COMMAND:
            wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId) {
                case IDOK:
                case IDCANCEL:
                    debugDialogOpen = false;
                    EndDialog(hWnd, 0);
                    break;
                default:
                    return FALSE;
            }
            break;

        default:
            return FALSE;
    }
    return TRUE;
}
#endif
uptr gsWindowHandle = 0;

s32 SPU2open(void *pDsp)
{
	ScopedLock lock( mtx_SPU2Status );
    if (IsOpened)
        return 0;

    FileLog("[%10d] SPU2 Open\n", Cycles);

    if (pDsp != NULL)
        gsWindowHandle = *(uptr *)pDsp;
    else
        gsWindowHandle = 0;

#ifdef _MSC_VER
#ifdef PCSX2_DEVBUILD // Define may not be needed but not tested yet. Better make sure.
    if (IsDevBuild && VisualDebug()) {
        if (debugDialogOpen == 0) {
            hDebugDialog = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_DEBUG), 0, DebugProc, 0);
            ShowWindow(hDebugDialog, SW_SHOWNORMAL);
            debugDialogOpen = 1;
        }
    } else if (debugDialogOpen) {
        DestroyWindow(hDebugDialog);
        debugDialogOpen = 0;
    }
#endif
#endif

    IsOpened = true;
    lClocks = (cyclePtr != NULL) ? *cyclePtr : 0;

    try {
        SndBuffer::Init();

#ifndef __POSIX__
        DspLoadLibrary(dspPlugin, dspPluginModule);
#endif
        WaveDump::Open();
    } catch (std::exception &ex) {
        fprintf(stderr, "SPU2-X Error: Could not initialize device, or something.\nReason: %s", ex.what());
        SPU2close();
        return -1;
    }
    return 0;
}

void SPU2close()
{
	ScopedLock lock( mtx_SPU2Status );
    if (!IsOpened)
        return;
    IsOpened = false;

    FileLog("[%10d] SPU2 Close\n", Cycles);

#ifndef __POSIX__
    DspCloseLibrary();
#endif

    SndBuffer::Cleanup();
}

void SPU2shutdown()
{
    if (!IsInitialized)
        return;
    IsInitialized = false;
    SPU2_dummy_callback = false;

    ConLog("* SPU2-X: Shutting down.\n");

    SPU2close();

#ifdef S2R_ENABLE
    if (!replay_mode)
        s2r_close();
#endif

    DoFullDump();
#ifdef STREAM_DUMP
    fclose(il0);
    fclose(il1);
#endif
#ifdef EFFECTS_DUMP
    fclose(el0);
    fclose(el1);
#endif
    WaveDump::Close();

    DMALogClose();

    safe_free(spu2regs);
    safe_free(_spu2mem);
    safe_free(pcm_cache_data);


#ifdef SPU2_LOG
    if (!AccessLog())
        return;
    FileLog("[%10d] SPU2shutdown\n", Cycles);
    if (spu2Log)
        fclose(spu2Log);
#endif
}

void SPU2setClockPtr(u32 *ptr)
{
    cyclePtr = ptr;
}

#ifdef DEBUG_KEYS
static u32 lastTicks;
static bool lState[6];
#endif

void SPU2async(u32 cycles)
{
    DspUpdate();

    if (cyclePtr != NULL) {
        TimeUpdate(*cyclePtr);
    } else {
        pClocks += cycles;
        TimeUpdate(pClocks);
    }

#ifdef DEBUG_KEYS
    u32 curTicks = GetTickCount();
    if ((curTicks - lastTicks) >= 50) {
        int oldI = Interpolation;
        bool cState[6];
        for (int i = 0; i < 6; i++) {
            cState[i] = !!(GetAsyncKeyState(VK_NUMPAD0 + i) & 0x8000);

            if ((cState[i] && !lState[i]) && i != 5)
                Interpolation = i;

            if ((cState[i] && !lState[i]) && i == 5) {
                postprocess_filter_enabled = !postprocess_filter_enabled;
                printf("Post process filters %s \n", postprocess_filter_enabled ? "enabled" : "disabled");
            }

            lState[i] = cState[i];
        }

        if (Interpolation != oldI) {
            printf("Interpolation set to %d", Interpolation);
            switch (Interpolation) {
                case 0:
                    printf(" - Nearest.\n");
                    break;
                case 1:
                    printf(" - Linear.\n");
                    break;
                case 2:
                    printf(" - Cubic.\n");
                    break;
                case 3:
                    printf(" - Hermite.\n");
                    break;
                case 4:
                    printf(" - Catmull-Rom.\n");
                    break;
                default:
                    printf(" (unknown).\n");
                    break;
            }
        }

        lastTicks = curTicks;
    }
#endif
}

u16 SPU2read(u32 rmem)
{
    //	if(!replay_mode)
    //		s2r_readreg(Cycles,rmem);

    u16 ret = 0xDEAD;
    u32 core = 0, mem = rmem & 0xFFFF, omem = mem;
    if (mem & 0x400) {
        omem ^= 0x400;
        core = 1;
    }

    if (omem == 0x1f9001AC) {
        ret = Cores[core].DmaRead();
    } else {
        if (cyclePtr != NULL)
            TimeUpdate(*cyclePtr);

        if (rmem >> 16 == 0x1f80) {
            ret = Cores[0].ReadRegPS1(rmem);
        } else if (mem >= 0x800) {
            ret = spu2Ru16(mem);
            ConLog("* SPU2-X: Read from reg>=0x800: %x value %x\n", mem, ret);
        } else {
            ret = *(regtable[(mem >> 1)]);
            //FileLog("[%10d] SPU2 read mem %x (core %d, register %x): %x\n",Cycles, mem, core, (omem & 0x7ff), ret);
            SPU2writeLog("read", rmem, ret);
        }
    }

    return ret;
}

void SPU2write(u32 rmem, u16 value)
{
#ifdef S2R_ENABLE
    if (!replay_mode)
        s2r_writereg(Cycles, rmem, value);
#endif

    // Note: Reverb/Effects are very sensitive to having precise update timings.
    // If the SPU2 isn't in in sync with the IOP, samples can end up playing at rather
    // incorrect pitches and loop lengths.

    if (cyclePtr != NULL)
        TimeUpdate(*cyclePtr);

    if (rmem >> 16 == 0x1f80)
        Cores[0].WriteRegPS1(rmem, value);
    else {
        SPU2writeLog("write", rmem, value);
        SPU2_FastWrite(rmem, value);
    }
}

// if start is 1, starts recording spu2 data, else stops
// returns a non zero value if successful
// for now, pData is not used
int SPU2setupRecording(int start, std::wstring* filename)
{
    if (start == 0)
        RecordStop();
    else if (start == 1)
        RecordStart(filename);

    return 0;
}

s32 SPU2freeze(int mode, freezeData *data)
{
    pxAssume(data != NULL);
    if (!data) {
        printf("SPU2-X savestate null pointer!\n");
        return -1;
    }

    if (mode == FREEZE_SIZE) {
        data->size = SPU2Savestate::SizeIt();
        return 0;
    }

    pxAssume(mode == FREEZE_LOAD || mode == FREEZE_SAVE);

    if (data->data == NULL) {
        printf("SPU2-X savestate null pointer!\n");
        return -1;
    }

    SPU2Savestate::DataBlock &spud = (SPU2Savestate::DataBlock &)*(data->data);

    switch (mode) {
        case FREEZE_LOAD:
            return SPU2Savestate::ThawIt(spud);
        case FREEZE_SAVE:
            return SPU2Savestate::FreezeIt(spud);

            jNO_DEFAULT;
    }

    // technically unreachable, but kills a warning:
    return 0;
}

void SPU2DoFreeze( SaveStateBase& state )
{
	ScopedLock lock( mtx_SPU2Status );

	freezeData fP = { 0, NULL };
	if( !SPU2freeze( FREEZE_SIZE, &fP ) )
		fP.size = 0;

	int fsize = fP.size;
	state.Freeze( fsize );

	Console.Indent().WriteLn( "%s SPU-2", state.IsSaving() ? "Saving" : "Loading");

	fP.size = fsize;
	if( fP.size == 0 ) return;

	state.PrepBlock( fP.size );
	fP.data = (s8*)state.GetBlockPtr();

	if( state.IsSaving() )
	{
		if( !SPU2freeze(FREEZE_SAVE, &fP) )
            throw std::runtime_error(" * SPU-2: Error saving state!\n");
	}
	else
	{
		if( !SPU2freeze(FREEZE_LOAD, &fP) )
            throw std::runtime_error(" * SPU-2: Error loading state!\n");
	}

	state.CommitBlock( fP.size );
}