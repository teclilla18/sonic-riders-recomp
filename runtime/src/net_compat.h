/* Shared by the runtime and PC launcher. Bump for changes to guest memory or deterministic
 * execution (recompiler, HLE, patches, gameplay). Platform/presentation changes need no bump. */
#pragma once
#define NET_COMPAT 2u  /* 2: Super Sonic flight follows the host (WELCOME byte 6 bit 3) */
