/* AX microcode stand-in: the DSP side of the AX audio library. Each 5 ms frame the AX library hands
 * the DSP a command list (main-RAM address, size in 16-bit words). For now the list is only counted;
 * mixing the voice parameter blocks into the output buffer comes next (NOTES.md "Audio"). */
#include "runtime.h"

static unsigned long long g_lists;

void ax_process_command_list(uint32_t addr, uint32_t size) {
    if (g_lists++ == 0) rt_log("[ax] first command list at %08X (%u words)", addr, size);
}

/* host audio output: the AI DMA "played" `bytes` of 16-bit stereo from guest `addr` */
void audio_submit(uint32_t addr, uint32_t bytes) { (void)addr; (void)bytes; }

unsigned long long ax_list_count(void) { return g_lists; }
