/* Shared by the runtime and PC launcher. Bump for changes to guest memory or deterministic
 * execution (recompiler, HLE, patches, gameplay). Platform/presentation changes need no bump. */
#pragma once
#define NET_COMPAT 6u  /* 6: scheduler-owned solo HUDs in four-player races */
