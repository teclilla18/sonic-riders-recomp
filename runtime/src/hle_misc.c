/* Small native replacements shared by several SDK functions (config/GXEE8P/hle.txt). */
#include "runtime.h"

/* hardware set-up/tear-down that has nothing to do natively; returns 0 */
void hle_nop(Cpu* c) { c->r[3] = 0; }
