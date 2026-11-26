/* Shared by the runtime and PC launcher. Bump for changes to guest memory or deterministic
 * execution (recompiler, HLE, patches, gameplay). Platform/presentation changes need no bump. */
#pragma once
#define NET_COMPAT 10u  /* 2-player rooms, held boost online (Super Sonic only with Limit Break); 9: Super Sonic balanced or classic (host picks); 8: online progression continues until each racer finishes; held boost disabled */
