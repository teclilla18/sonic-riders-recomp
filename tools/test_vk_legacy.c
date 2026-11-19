/* Checked Vulkan-call harness; test_vk_legacy.py includes the actual renderer helpers. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <threads.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <vulkan/vulkan.h>
#include "gx_render.h"

#define VKC(x) assert((x) == VK_SUCCESS)
#define HANDLE(type, n) ((type)(uintptr_t)(n))
#define EFB_W 3416
#define EFB_H 1056
#define XSC(x) ((uint32_t)((x) * 1.5f))
#define SC(x) ((uint32_t)((x) * 1.5f))
static VkDevice g_dev;
static VkCommandBuffer g_cmd;
static VkPipelineCache g_pcache;
static VkFormat g_depth_fmt = VK_FORMAT_D24_UNORM_S8_UINT, g_swap_fmt = VK_FORMAT_B8G8R8A8_UNORM;
static int g_dynamic;
static int g_dual_src;
#define SPEC_FLAG (1ull << 60)
#define SPEC_SHIFT 36
#define ALPHA_PASS_FLAG (1ull << 61)
#define CONST_ALPHA_FLAG (1ull << 62)
#define GX_SPEC_COUNT 1
static const uint64_t k_spec_keys[1] = {123};
static VkShaderModule g_spec_sm[1] = {HANDLE(VkShaderModule, 10)}, g_spec_alpha_sm[1] = {HANDLE(VkShaderModule, 11)};
static VkShaderModule g_sm_fs = HANDLE(VkShaderModule, 2), g_sm_fs_ind = HANDLE(VkShaderModule, 3);
static VkShaderModule g_sm_fs_alpha = HANDLE(VkShaderModule, 4), g_sm_fs_ind_alpha = HANDLE(VkShaderModule, 5), g_sm_vs;
static VkPipelineLayout g_pl_gx;
static unsigned g_pipes_new, g_passes;
typedef struct { VkImageView view; } TestImage;
static TestImage g_efb = {HANDLE(VkImageView, 100)}, g_depth = {HANDLE(VkImageView, 101)};
static uint32_t g_nimg = 1;
static VkImageView g_sview[1] = {HANDLE(VkImageView, 200)};
static void to_layout(TestImage* image, VkImageLayout layout) { (void)image; (void)layout; }
static uint64_t platform_now_ns(void) { return 0; }
#define rt_log(...) ((void)0)

static unsigned npass, nfb, destroyed_fb, destroyed_pass, begins, ends, dynamic_begins, dynamic_ends;
static VkAttachmentDescription attachments[16][2];
static unsigned counts[16];
static VkRenderPass last_pass;
static VkRect2D last_area;
static int expect_dynamic, expect_depth;
static VkColorComponentFlags last_mask;
static VkCompareOp last_compare;
static VkBool32 last_depth_write;
static VkShaderModule last_fragment;
static VkBlendFactor last_src;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice dev, const VkRenderPassCreateInfo* ci,
                                                  const VkAllocationCallbacks* allocator, VkRenderPass* pass) {
    (void)dev; (void)allocator;
    assert(npass < 16 && ci->subpassCount == 1 && ci->attachmentCount <= 2);
    assert(ci->dependencyCount == 2);
    assert(ci->pDependencies[0].srcSubpass == VK_SUBPASS_EXTERNAL);
    assert(ci->pDependencies[1].dstSubpass == VK_SUBPASS_EXTERNAL);
    assert(ci->pDependencies[0].srcAccessMask == VK_ACCESS_MEMORY_WRITE_BIT);
    assert(!(ci->pDependencies[0].dstStageMask & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    assert(!(ci->pDependencies[1].srcStageMask & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    assert(ci->pDependencies[0].dstAccessMask & VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
    assert(ci->pDependencies[1].srcAccessMask & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    assert(ci->pSubpasses[0].colorAttachmentCount == 1);
    assert(ci->pSubpasses[0].pColorAttachments[0].attachment == 0);
    assert(!!ci->pSubpasses[0].pDepthStencilAttachment == (ci->attachmentCount == 2));
    memcpy(attachments[npass], ci->pAttachments, ci->attachmentCount * sizeof *ci->pAttachments);
    counts[npass] = ci->attachmentCount;
    *pass = HANDLE(VkRenderPass, ++npass);
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice dev, const VkFramebufferCreateInfo* ci,
                                                   const VkAllocationCallbacks* allocator, VkFramebuffer* fb) {
    (void)dev; (void)allocator;
    assert(ci->layers == 1 && ci->width && ci->height);
    assert(ci->attachmentCount == counts[(uintptr_t)ci->renderPass - 1]);
    *fb = HANDLE(VkFramebuffer, ++nfb);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice dev, VkFramebuffer fb, const VkAllocationCallbacks* a) {
    (void)dev; (void)fb; (void)a; destroyed_fb++;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice dev, VkRenderPass pass, const VkAllocationCallbacks* a) {
    (void)dev; (void)pass; (void)a; destroyed_pass++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer cmd, const VkRenderPassBeginInfo* bi, VkSubpassContents contents) {
    (void)cmd;
    assert(contents == VK_SUBPASS_CONTENTS_INLINE);
    unsigned i = (unsigned)(uintptr_t)bi->renderPass - 1;
    assert((attachments[i][0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) == (bi->clearValueCount == 1));
    if (bi->clearValueCount) assert(bi->pClearValues[0].color.float32[3] == 1.0f);
    last_pass = bi->renderPass; last_area = bi->renderArea; begins++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer cmd) { (void)cmd; ends++; }
static void VKAPI_CALL dynamic_begin(VkCommandBuffer cmd, const VkRenderingInfo* ri) {
    (void)cmd; last_area = ri->renderArea; dynamic_begins++;
}
static void VKAPI_CALL dynamic_end(VkCommandBuffer cmd) { (void)cmd; dynamic_ends++; }
static PFN_vkCmdBeginRenderingKHR p_begin_rendering = dynamic_begin;
static PFN_vkCmdEndRenderingKHR p_end_rendering = dynamic_end;
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice dev, VkPipelineCache cache, uint32_t count,
        const VkGraphicsPipelineCreateInfo* ci, const VkAllocationCallbacks* a, VkPipeline* pipe) {
    (void)dev; (void)cache; (void)a;
    assert(count == 1 && ci->subpass == 0);
    assert(!!ci->pNext == expect_dynamic);
    assert(!!ci->renderPass == !expect_dynamic);
    last_mask = ci->pColorBlendState->pAttachments[0].colorWriteMask;
    last_src = ci->pColorBlendState->pAttachments[0].srcColorBlendFactor;
    last_fragment = ci->pStages[1].module;
    if (ci->pDepthStencilState) {
        last_compare = ci->pDepthStencilState->depthCompareOp;
        last_depth_write = ci->pDepthStencilState->depthWriteEnable;
    }
    if (expect_dynamic) {
        const VkPipelineRenderingCreateInfo* ri = ci->pNext;
        assert(ri->depthAttachmentFormat == (expect_depth ? g_depth_fmt : VK_FORMAT_UNDEFINED));
    } else assert(counts[(uintptr_t)ci->renderPass - 1] == (expect_depth ? 2u : 1u));
    *pipe = HANDLE(VkPipeline, 1);
    return VK_SUCCESS;
}

#include "vk_legacy_under_test.h"

#ifdef _WIN32
static DWORD WINAPI prewarm_stress(LPVOID arg) {
#else
static void* prewarm_stress(void* arg) {
#endif
    (void)arg;
    for (int i = 0; i < 1000; i++)
        legacy_pass(VK_FORMAT_R8G8B8A8_UNORM, g_depth_fmt, 0);
    return 0;
}

int main(void) {
    mtx_init(&g_pass_lock, mtx_plain);
    assert(!dynamic_supported(VK_API_VERSION_1_1, 0, 0, 0, 0));
    assert(!dynamic_supported(VK_API_VERSION_1_1, 1, 0, 1, 1));
    assert(!dynamic_supported(VK_API_VERSION_1_1, 1, 1, 0, 1));
    assert(dynamic_supported(VK_API_VERSION_1_1, 1, 1, 1, 1));
    assert(dynamic_supported(VK_API_VERSION_1_2, 1, 0, 0, 1));
    assert(!dynamic_supported(VK_API_VERSION_1_2, 0, 1, 1, 1));
    assert(!dynamic_supported(VK_API_VERSION_1_3, 1, 1, 1, 0));
    assert(dynamic_supported(VK_API_VERSION_1_3, 0, 0, 0, 1));

    VkRenderPass load = legacy_pass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, 0);
    VkRenderPass clear = legacy_pass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, 1);
    assert(load != clear && npass == 2);
    assert(legacy_pass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, 0) == load && npass == 2);
    assert(attachments[0][0].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
    assert(attachments[1][0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
    assert(attachments[0][0].storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    assert(attachments[0][0].initialLayout == attachments[0][0].finalLayout);
    unsigned before = npass;
#ifdef _WIN32
    HANDLE workers[8];
    for (int t = 0; t < 8; t++) workers[t] = CreateThread(NULL, 0, prewarm_stress, NULL, 0, NULL);
    assert(WaitForMultipleObjects(8, workers, TRUE, INFINITE) == WAIT_OBJECT_0);
    for (int t = 0; t < 8; t++) CloseHandle(workers[t]);
#else
    pthread_t workers[8];
    for (int t = 0; t < 8; t++) assert(!pthread_create(&workers[t], NULL, prewarm_stress, NULL));
    for (int t = 0; t < 8; t++) assert(!pthread_join(workers[t], NULL));
#endif
    assert(npass == before + 1); /* render/prewarm callers share one immutable compatible pass */
    VkImageView view = HANDLE(VkImageView, 50);
    VkFramebuffer fb = legacy_framebuffer(load, view, VK_NULL_HANDLE, 64, 32);
    assert(legacy_framebuffer(clear, view, VK_NULL_HANDLE, 64, 32) == fb && nfb == 1);
    assert(legacy_framebuffer(load, view, VK_NULL_HANDLE, 128, 64) != fb && nfb == 2);
    legacy_forget_view(view);
    assert(destroyed_fb == 2 && !g_legacy_fbs);

    efb_begin(); efb_begin();
    assert(begins == 1 && g_passes == 1 && g_in_efb && last_area.extent.width == 960 && last_area.extent.height == 792);
    unsigned i = (unsigned)(uintptr_t)last_pass - 1;
    assert(counts[i] == 2 && attachments[i][1].format == g_depth_fmt);
    assert(attachments[i][1].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && attachments[i][1].storeOp == VK_ATTACHMENT_STORE_OP_STORE);
    efb_end(); efb_end(); assert(ends == 1 && !g_in_efb);
    target_begin(g_sview[0], 1920, 1080, 1); rendering_end();
    assert(attachments[(uintptr_t)last_pass - 1][0].format == g_swap_fmt);
    target_begin(g_sview[0], 1920, 1080, 0); rendering_end();
    assert(nfb == 4);  /* EFB plus one swapchain FB shared by clear/load */
    legacy_forget_view(g_depth.view); assert(destroyed_fb == 3);
    legacy_forget_view(g_sview[0]); assert(destroyed_fb == 4);

    for (int path = 0; path < 2; path++) {
        g_dynamic = expect_dynamic = path;
        for (int target = TGT_EFB; target <= TGT_SWAP; target++) {
            expect_depth = target == TGT_EFB;
            VkPipelineColorBlendAttachmentState b = {0};
            pipe_build(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                       &b, NULL, VK_CULL_MODE_NONE, target);
        }
    }
    efb_begin(); efb_end(); target_begin(view, 16, 8, 0); rendering_end();
    assert(dynamic_begins == 2 && dynamic_ends == 2);
    GxUniforms u = {0};
    u.bp[0x40] = 0x13; /* test/write depth, LESS */
    u.bp[0x41] = 1 | 8 | 16 | (4 << 8) | (5 << 5);
    u.bp[0x42] = 0x18c; /* store constant alpha; blend still uses TEV alpha */
    expect_depth = 1;
    uint64_t key = state_key(&u, GXP_TRIANGLES);
    assert(key & CONST_ALPHA_FLAG);
    gx_pipe_make(key);
    assert(last_mask == 7 && last_src == VK_BLEND_FACTOR_SRC_ALPHA && last_fragment == g_sm_fs);
    gx_pipe_make(key | ALPHA_PASS_FLAG);
    assert(last_mask == VK_COLOR_COMPONENT_A_BIT && !last_depth_write && last_compare == VK_COMPARE_OP_EQUAL);
    assert(last_fragment == g_sm_fs_alpha);
    gx_pipe_make(key | ALPHA_PASS_FLAG | SPEC_FLAG | (123ull << SPEC_SHIFT));
    assert(last_fragment == g_spec_alpha_sm[0]);
    g_dual_src = 1;
    key = state_key(&u, GXP_TRIANGLES);
    assert(!(key & CONST_ALPHA_FLAG));
    gx_pipe_make(key);
    assert(last_mask == 15 && last_src == VK_BLEND_FACTOR_SRC1_ALPHA && last_depth_write);
    unsigned old = npass;
    legacy_teardown(); assert(destroyed_pass == old && !g_legacy_passes && !g_legacy_fbs);
    legacy_pass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, 0);
    assert(npass == old + 1); legacy_teardown(); /* recovery recreates all handles */

    g_free_list = malloc(sizeof *g_free_list);
    *g_free_list = (Block){0, 1024, NULL};
    assert(pool_alloc(100, 256) == 0);
    assert(pool_alloc(512, 256) == 256);
    assert(pool_alloc(512, 256) == UINT32_MAX); /* allocation spills instead of fatal */
    assert(pool_alloc(UINT32_MAX, 256) == UINT32_MAX); /* do not wrap a large image into the pool */
    pool_free(0, 100); pool_free(256, 512);
    assert(g_free_list->off == 0 && g_free_list->size == 1024 && !g_free_list->next);
    free(g_free_list);
    mtx_destroy(&g_pass_lock);
    puts("PASS: feature/dependency matrix, compatible passes/pipelines, render areas, dispatch, framebuffer retirement/recovery, pool spill/coalescing");
    return 0;
}
