#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(call) do { VkResult r_ = (call); printf("%s = %d\n", #call, r_); fflush(stdout); if (r_ != VK_SUCCESS) return 1; } while (0)

static int submit(VkDevice d, VkQueue q, VkCommandBuffer cmd, const char * name) {
    VkFence f = VK_NULL_HANDLE;
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
    if (cmd) { si.commandBufferCount = 1; si.pCommandBuffers = &cmd; }
    printf("submit %s queue=%p cmd=%p\n", name, (void *)q, (void *)cmd); fflush(stdout);
    if (!q) return 2;
    CHECK(vkCreateFence(d, &fi, NULL, &f));
    VkResult r = vkQueueSubmit(q, 1, &si, f);
    printf("vkQueueSubmit = %d\n", r); fflush(stdout);
    if (r == VK_SUCCESS) { r = vkWaitForFences(d, 1, &f, VK_TRUE, 5000000000ULL); printf("vkWaitForFences = %d\n", r); fflush(stdout); }
    if (r == VK_SUCCESS) vkDestroyFence(d, f, NULL);
    return r == VK_SUCCESS ? 0 : 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    VkInstance i = VK_NULL_HANDLE;
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    CHECK(vkCreateInstance(&ii, NULL, &i));
    uint32_t n = 0; CHECK(vkEnumeratePhysicalDevices(i, &n, NULL));
    if (!n) return 1;
    VkPhysicalDevice * pd = calloc(n, sizeof(*pd)); CHECK(vkEnumeratePhysicalDevices(i, &n, pd));
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pd[0], &props);
    printf("device=%s api=%u.%u.%u\n", props.deviceName, VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
    uint32_t count = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd[0], &count, NULL);
    VkQueueFamilyProperties * families = calloc(count, sizeof(*families)); vkGetPhysicalDeviceQueueFamilyProperties(pd[0], &count, families);
    uint32_t family = UINT32_MAX;
    for (uint32_t j = 0; j < count; ++j) { printf("family=%u flags=%x count=%u\n", j, families[j].queueFlags, families[j].queueCount); if (family == UINT32_MAX && (families[j].queueFlags & VK_QUEUE_COMPUTE_BIT) && families[j].queueCount) family = j; }
    if (family == UINT32_MAX) return 1;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    VkDevice d = VK_NULL_HANDLE; CHECK(vkCreateDevice(pd[0], &di, NULL, &d));
    VkQueue q1 = VK_NULL_HANDLE, q2 = VK_NULL_HANDLE;
    vkGetDeviceQueue(d, family, 0, &q1);
    VkDeviceQueueInfo2 qinfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2, .queueFamilyIndex = family, .queueIndex = 0 };
    vkGetDeviceQueue2(d, &qinfo, &q2);
    printf("queue legacy=%p queue2=%p equal=%d flags=0\n", (void *)q1, (void *)q2, q1 == q2);
    if (submit(d, q1, VK_NULL_HANDLE, "legacy-empty")) return 2;
    if (q2 && submit(d, q2, VK_NULL_HANDLE, "queue2-empty")) return 2;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family };
    CHECK(vkCreateCommandPool(d, &pi, NULL, &pool));
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    CHECK(vkAllocateCommandBuffers(d, &ai, &cb));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cb, &bi)); CHECK(vkEndCommandBuffer(cb));
    if (submit(d, q1, cb, "legacy-command")) return 2;
    if (q2 && submit(d, q2, cb, "queue2-command")) return 2;
    vkDestroyCommandPool(d, pool, NULL); vkDestroyDevice(d, NULL); vkDestroyInstance(i, NULL); free(families); free(pd);
    puts("PASS"); return 0;
}
