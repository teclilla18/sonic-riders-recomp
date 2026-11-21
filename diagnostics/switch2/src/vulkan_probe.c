/* SPDX-License-Identifier: MIT
 * Native NWindow/VK_NN_vi_surface integration follows ounce-devkit's MIT
 * vulkan-clear example. No shaders or game-derived content are included.
 */
#include "diag.h"
#define VK_USE_PLATFORM_VI_NN 1
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_LIMIT 16
typedef struct {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkSwapchainKHR swapchain;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkSemaphore acquired, rendered[IMAGE_LIMIT];
    VkFence fence;
    VkImage images[IMAGE_LIMIT];
    uint32_t count;
} Gpu;

static bool checked(VkResult rc, const char *name) {
    diag_log("%s VkResult=%d", name, rc);
    return rc == VK_SUCCESS;
}

/* Steady-state calls log errors only; color checkpoints are flushed beforehand. */
static bool frame_checked(VkResult rc, const char *name) {
    return rc == VK_SUCCESS || checked(rc, name);
}
#define VK_TRY(call) do { diag_log("BEGIN %s", #call); if (!checked((call), #call)) goto cleanup; } while (0)

static void report_capabilities(Gpu *g) {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(g->physical, &p);
    diag_log("GPU %s vendor=0x%x device=0x%x driver=%u Vulkan=%u.%u.%u", p.deviceName, p.vendorID,
             p.deviceID, p.driverVersion, VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion));
    diag_log("LIMITS image2D=%u samplers=%u UBOalignment=%llu SSBOalignment=%llu timestampPeriod=%g",
             p.limits.maxImageDimension2D, p.limits.maxPerStageDescriptorSamplers,
             (unsigned long long)p.limits.minUniformBufferOffsetAlignment,
             (unsigned long long)p.limits.minStorageBufferOffsetAlignment, (double)p.limits.timestampPeriod);
    VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f13};
    vkGetPhysicalDeviceFeatures2(g->physical, &f2);
    diag_log("FEATURES dynamicRendering=%u synchronization2=%u anisotropy=%u independentBlend=%u; reported, NOT workload-tested",
             f13.dynamicRendering, f13.synchronization2, f2.features.samplerAnisotropy, f2.features.independentBlend);
    VkFormat formats[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
                          VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT,
                          VK_FORMAT_R16G16B16A16_SFLOAT};
    for (unsigned i = 0; i < sizeof formats / sizeof formats[0]; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g->physical, formats[i], &fp);
        diag_log("FORMAT %d linear=0x%x optimal=0x%x buffer=0x%x", formats[i], fp.linearTilingFeatures, fp.optimalTilingFeatures, fp.bufferFeatures);
    }
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g->physical, &mp);
    for (uint32_t i = 0; i < mp.memoryHeapCount; i++)
        diag_log("GPU heap %u size=%llu flags=0x%x", i, (unsigned long long)mp.memoryHeaps[i].size, mp.memoryHeaps[i].flags);
}

static bool choose_queue(Gpu *g) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g->physical, &count, NULL);
    VkQueueFamilyProperties *props = calloc(count, sizeof *props);
    if (!props || !count) { free(props); return false; }
    vkGetPhysicalDeviceQueueFamilyProperties(g->physical, &count, props);
    bool found = false;
    for (uint32_t i = 0; i < count; i++) {
        VkBool32 present = VK_FALSE;
        VkResult rc = vkGetPhysicalDeviceSurfaceSupportKHR(g->physical, i, g->surface, &present);
        diag_log("QUEUE %u flags=0x%x count=%u present=%u query=%d timestampBits=%u", i, props[i].queueFlags,
                 props[i].queueCount, present, rc, props[i].timestampValidBits);
        if (!found && rc == VK_SUCCESS && present && props[i].queueCount && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            g->family = i; found = true;
        }
    }
    free(props);
    return found;
}

static bool make_swapchain(Gpu *g) {
    VkSurfaceCapabilitiesKHR caps;
    diag_log("BEGIN swapchain capabilities/formats");
    if (!checked(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g->physical, g->surface, &caps), "surface capabilities")) return false;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        diag_log("FAIL transfer-destination swapchain images unavailable"); return false;
    }
    uint32_t count = 0;
    if (!checked(vkGetPhysicalDeviceSurfaceFormatsKHR(g->physical, g->surface, &count, NULL), "surface format count") || !count) return false;
    VkSurfaceFormatKHR *formats = calloc(count, sizeof *formats);
    if (!formats) return false;
    VkResult rc = vkGetPhysicalDeviceSurfaceFormatsKHR(g->physical, g->surface, &count, formats);
    if (rc != VK_SUCCESS) { free(formats); checked(rc, "surface formats"); return false; }
    VkSurfaceFormatKHR format = formats[0];
    for (uint32_t i = 0; i < count; i++) {
        diag_log("SURFACE format=%d colorspace=%d", formats[i].format, formats[i].colorSpace);
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) format = formats[i];
    }
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
    free(formats);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        u32 w, h;
        nwindowGetDimensions(nwindowGetDefault(), &w, &h);
        extent.width = w < caps.minImageExtent.width ? caps.minImageExtent.width : w;
        extent.height = h < caps.minImageExtent.height ? caps.minImageExtent.height : h;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount && images > caps.maxImageCount) images = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha))
        alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & (0u - caps.supportedCompositeAlpha));
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g->surface, .minImageCount = images, .imageFormat = format.format,
        .imageColorSpace = format.colorSpace, .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE
    };
    diag_log("BEGIN vkCreateSwapchainKHR extent=%ux%u requested_images=%u", extent.width, extent.height, images);
    if (!checked(vkCreateSwapchainKHR(g->device, &ci, NULL, &g->swapchain), "vkCreateSwapchainKHR")) return false;
    if (!checked(vkGetSwapchainImagesKHR(g->device, g->swapchain, &g->count, NULL), "swapchain image count")) return false;
    if (!g->count || g->count > IMAGE_LIMIT) { diag_log("FAIL swapchain image count=%u exceeds diagnostic limit", g->count); return false; }
    return checked(vkGetSwapchainImagesKHR(g->device, g->swapchain, &g->count, g->images), "swapchain images");
}

static void barrier(VkCommandBuffer cmd, VkImage image, bool to_present) {
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = to_present ? VK_ACCESS_TRANSFER_WRITE_BIT : 0,
        .dstAccessMask = to_present ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = to_present ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = to_present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    vkCmdPipelineBarrier(cmd, to_present ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         to_present ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
}

static bool frame(Gpu *g, unsigned number) {
    if (!frame_checked(vkWaitForFences(g->device, 1, &g->fence, VK_TRUE, 2000000000ULL), "frame fence (2 seconds)")) return false;
    uint32_t index = 0;
    VkResult rc = vkAcquireNextImageKHR(g->device, g->swapchain, 2000000000ULL, g->acquired, VK_NULL_HANDLE, &index);
    if (rc != VK_SUCCESS && rc != VK_SUBOPTIMAL_KHR) return checked(rc, "image acquire");
    if (index >= g->count) { diag_log("FAIL acquired image index out of range"); return false; }
    if (!frame_checked(vkResetCommandBuffer(g->command, 0), "reset command buffer")) return false;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (!frame_checked(vkBeginCommandBuffer(g->command, &begin), "begin command buffer")) return false;
    barrier(g->command, g->images[index], false);
    VkClearColorValue color = {{0.06f, 0.06f, 0.06f, 1.0f}};
    color.float32[(number / 60) % 3] = 0.85f;
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(g->command, g->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
    barrier(g->command, g->images[index], true);
    if (!frame_checked(vkEndCommandBuffer(g->command), "end command buffer")) return false;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &g->acquired,
        .pWaitDstStageMask = &wait_stage, .commandBufferCount = 1, .pCommandBuffers = &g->command,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &g->rendered[index]
    };
    if (!frame_checked(vkResetFences(g->device, 1, &g->fence), "reset frame fence")) return false;
    if (!frame_checked(vkQueueSubmit(g->queue, 1, &submit, g->fence), "queue submit")) return false;
    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &g->rendered[index],
        .swapchainCount = 1, .pSwapchains = &g->swapchain, .pImageIndices = &index
    };
    rc = vkQueuePresentKHR(g->queue, &present);
    return rc == VK_SUCCESS || rc == VK_SUBOPTIMAL_KHR || checked(rc, "queue present");
}

void probe_vulkan(void) {
    Gpu g = {0};
    bool ok = false, cleanup_ok = true;
    unsigned frames = 0;
    result(T_VISUAL, PENDING, "awaiting Vulkan presentation and tester confirmation");
    diag_log("BEGIN Vulkan stage; console buffers will be released; expect RED GREEN BLUE then results");
    screen_stop();
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_NN_VI_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "SR2Diag", .applicationVersion = 1, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    VK_TRY(vkCreateInstance(&ic, NULL, &g.instance));
    diag_log("BEGIN native window acquisition (one default NWindow reused)");
    NWindow *window = nwindowGetDefault();
    VkViSurfaceCreateInfoNN sc = {.sType = VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN, .window = window};
    VK_TRY(vkCreateViSurfaceNN(g.instance, &sc, NULL, &g.surface));
    uint32_t physical_count = 0;
    VK_TRY(vkEnumeratePhysicalDevices(g.instance, &physical_count, NULL));
    if (!physical_count) { diag_log("FAIL no Vulkan physical device"); goto cleanup; }
    VkPhysicalDevice *physical = calloc(physical_count, sizeof *physical);
    if (!physical) goto cleanup;
    VkResult enumeration = vkEnumeratePhysicalDevices(g.instance, &physical_count, physical);
    if (enumeration == VK_SUCCESS && physical_count) g.physical = physical[0];
    free(physical);
    if (!g.physical || !checked(enumeration, "physical devices")) goto cleanup;
    report_capabilities(&g);
    if (!choose_queue(&g)) { diag_log("FAIL no graphics/present queue"); goto cleanup; }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = g.family, .queueCount = 1, .pQueuePriorities = &priority};
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dc = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions};
    VK_TRY(vkCreateDevice(g.physical, &dc, NULL, &g.device));
    vkGetDeviceQueue(g.device, g.family, 0, &g.queue);
    if (!make_swapchain(&g)) goto cleanup;
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = g.family};
    VK_TRY(vkCreateCommandPool(g.device, &pool, NULL, &g.pool));
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = g.pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VK_TRY(vkAllocateCommandBuffers(g.device, &ca, &g.command));
    VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_TRY(vkCreateSemaphore(g.device, &sem, NULL, &g.acquired));
    /* One render-finished semaphore per image: do not reuse before presentation consumes it. */
    for (uint32_t i = 0; i < g.count; i++) VK_TRY(vkCreateSemaphore(g.device, &sem, NULL, &g.rendered[i]));
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VK_TRY(vkCreateFence(g.device, &fi, NULL, &g.fence));
    for (; frames < 180 && poll_input(); frames++) {
        if (frames % 60 == 0) diag_log("BEGIN presentation color=%u frame=%u", (frames / 60) % 3, frames);
        if (!frame(&g, frames)) goto cleanup;
    }
    ok = frames == 180;
cleanup:
    diag_log("BEGIN Vulkan cleanup; presented_frames=%u", frames);
    if (g.device) {
        diag_log("BEGIN vkDeviceWaitIdle (driver call may block; this checkpoint is flushed)");
        cleanup_ok = checked(vkDeviceWaitIdle(g.device), "vkDeviceWaitIdle");
        if (g.fence) vkDestroyFence(g.device, g.fence, NULL);
        if (g.acquired) vkDestroySemaphore(g.device, g.acquired, NULL);
        for (uint32_t i = 0; i < IMAGE_LIMIT; i++) if (g.rendered[i]) vkDestroySemaphore(g.device, g.rendered[i], NULL);
        if (g.pool) vkDestroyCommandPool(g.device, g.pool, NULL);
        if (g.swapchain) vkDestroySwapchainKHR(g.device, g.swapchain, NULL);
        diag_log("BEGIN vkDestroyDevice");
        vkDestroyDevice(g.device, NULL);
    }
    if (g.surface) vkDestroySurfaceKHR(g.instance, g.surface, NULL);
    if (g.instance) { diag_log("BEGIN vkDestroyInstance"); vkDestroyInstance(g.instance, NULL); }
    diag_log("END explicit Vulkan cleanup success=%d; shader/pipeline workloads NOT TESTED", cleanup_ok);
    if (!cleanup_ok) cleanup_failed = true;
    if (!screen_start()) { quitting = true; result(T_VULKAN, FAIL, "console restoration failed"); return; }
    result(T_VULKAN, !cleanup_ok ? FAIL : ok ? PASS : quitting ? SKIPPED : FAIL,
           ok && cleanup_ok ? "180 clear/present calls and explicit GPU cleanup passed" : quitting ? "cancelled; partial presentation only" : "Vulkan operation failed; inspect last flushed checkpoint");
    if (ok && cleanup_ok && !quitting) {
        Status visual = confirm("Did RED then GREEN then BLUE fill the screen? Y yes / B no / X skip");
        result(T_VISUAL, visual, visual == PASS ? "tester confirmed all three colors" : visual == FAIL ? "API presentation passed, tester saw incorrect output" : "visible output not confirmed");
    } else result(T_VISUAL, SKIPPED, "full presentation sequence not completed");
}
