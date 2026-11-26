/* Shared by the runtime and PC launcher. Bump for changes to guest memory or deterministic
 * execution (recompiler, HLE, patches, gameplay). Platform/presentation changes need no bump. */
#pragma once
#define NET_COMPAT 9u  /* Super Sonic balanced or classic (host picks); 8: online progression continues until each racer finishes; held boost disabled */
