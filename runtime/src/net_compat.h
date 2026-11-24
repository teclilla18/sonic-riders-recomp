/* Shared by the runtime and PC launcher. Bump for changes to guest memory or deterministic
 * execution (recompiler, HLE, patches, gameplay). Platform/presentation changes need no bump. */
#pragma once
#define NET_COMPAT 5u  /* 5: host-relayed input bundles for up to four players */
