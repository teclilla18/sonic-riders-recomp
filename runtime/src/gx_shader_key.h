/* Fragment-shader specialization keys (gx_shader_key.c). */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GX_FRAG_KEY_MAX 80
typedef struct GxFragKey {
    uint64_t hash;
    int n;
    uint8_t reg[GX_FRAG_KEY_MAX];
    uint32_t val[GX_FRAG_KEY_MAX];
} GxFragKey;

void gx_frag_key(const uint32_t* bp, GxFragKey* k);
/* append the key to `path` once (the file's existing keys count as already written) */
void gx_frag_key_record(const GxFragKey* k, const char* path);

#ifdef __cplusplus
}
#endif
