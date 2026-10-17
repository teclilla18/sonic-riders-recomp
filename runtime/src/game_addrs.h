/* Addresses in Sonic Riders (GXEE8P rev 0) main.dol that the native runtime uses. Found with
 * decomp-toolkit + tools/showfn.py; see NOTES.md "SDK map". Low-memory globals are the Dolphin OS ABI. */
#pragma once

/* OS globals (low memory) */
#define OS_CURRENT_CONTEXT  0x800000D4u  /* OSContext* */
#define OS_CURRENT_THREAD   0x800000E4u  /* OSThread*  */
#define OS_INTERRUPT_MASK   0x800000C4u  /* __OSInterruptMask (OS) */
#define OS_USER_MASK        0x800000C8u  /* user interrupt mask */

/* OSThread.c statics */
#define OS_RUNQUEUE         0x801B31F8u
#define OS_IDLE_CONTEXT     (OS_RUNQUEUE + 0x730)
#define OS_DEFAULT_THREAD   (OS_RUNQUEUE + 0x418)
#define OS_RUNQUEUE_BITS    0x8021BCF8u
#define OS_RUNQUEUE_HINT    0x8021BCFCu
#define OS_RESCHEDULE       0x8021BD00u

/* _Main.rel: the unlock data (REL bss at the runtime's fixed 819FF380; a GameCube runs it from 805FB080,
 * where these are 806A9048 / 806A90A0). One status byte per Extreme Gear (0 locked, 1 the default gear,
 * 2 owned) and per character. The Chaos Emerald is gear 18: race code tests the player's gear
 * (player +0x1034) against 18 and 19, the two gears that run on rings. */
#define GAME_GEAR_STATUS     0x81AAD348u
#define GAME_GEAR_CHAOS_EMERALD 18
#define GAME_CHAR_STATUS     0x81AAD3A0u

/* dvd.c statics (hang dumps only) */
#define DVD_EXECUTING        0x8021BD80u  /* DVDCommandBlock* */
#define DVD_PAUSE_FLAG       0x8021BD8Cu
#define DVD_PAUSING_FLAG     0x8021BD90u
#define DVD_FATAL_ERROR_FLAG 0x8021BD98u
#define DVD_CURR_COMMAND     0x8021BD9Cu
#define DVD_RESUME_FROM_HERE 0x8021BDA8u
#define DVD_DUMMY_BLOCK      0x801B3D00u

/* functions */
#define FN_OSSaveContext                 0x8000BB78u
#define FN_OSLoadContext                 0x8000BBF8u
#define FN_OSExitThread                  0x80010868u
#define FN_DecrementerExceptionCallback  0x8000A7E8u
#define FN_OSReschedule                  0x80010650u

/* OSContext layout */
#define CTX_GPR   0x000
#define CTX_CR    0x080
#define CTX_LR    0x084
#define CTX_CTR   0x088
#define CTX_XER   0x08C
#define CTX_FPR   0x090
#define CTX_SRR0  0x198
#define CTX_SRR1  0x19C
#define CTX_STATE 0x1A2
#define CTX_GQR   0x1A4

#define MSR_EE 0x8000u
