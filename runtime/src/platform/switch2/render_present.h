/* Keep render pacing separate from supported Vulkan/display presentation.
 * FIFO matches PC VSync when the current effective interval equals the cap.
 * Other caps prefer immediate, then mailbox; FIFO is the guaranteed fallback. */
#pragma once
static VkPresentModeKHR native_present_mode(const VkPresentModeKHR* modes, uint32_t n,
                                          int target, uint32_t numerator, uint32_t denominator) {
    if (!target || (target > 0 && numerator && denominator == (uint32_t)target * numerator))
        return VK_PRESENT_MODE_FIFO_KHR;
    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < n; i++)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) chosen = modes[i];
    for (uint32_t i = 0; i < n; i++)
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) chosen = modes[i];
    return chosen;
}
