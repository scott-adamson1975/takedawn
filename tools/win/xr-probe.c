/* xr-probe.c: what does the active OpenXR runtime (SteamVR) offer, and does it accept a
 * Vulkan instance and device made the way the OpenXR spec says (XR_KHR_vulkan_enable2)?
 * Read-only: it draws nothing and creates no session.
 *
 *   xr-probe            stage 1: the runtime's extensions (does not need a headset)
 *   xr-probe --system   also stages 2 and 3: an OpenXR instance, the headset's system and its
 *                       recommended eye size, the Vulkan versions it wants, then a Vulkan
 *                       instance and device made THROUGH the runtime. Starts SteamVR if it is
 *                       not running, and wants the headset connected.
 *
 * The OpenXR loader and vulkan-1 are loaded at run time, as the game does for Vulkan.
 *
 * Build in Git Bash after tools/win/fetch-deps.sh and fetching the OpenXR headers (docs/vr-openxr-proposal.md):
 *   D=$HOME/.cache/bo3-windeps
 *   $D/llvm-mingw/bin/x86_64-w64-mingw32-clang -I$D/openxr/include -I$D/vulkan/include \
 *       -DXR_USE_GRAPHICS_API_VULKAN -DXR_USE_PLATFORM_WIN32 xr-probe.c -o xr-probe.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static PFN_xrGetInstanceProcAddr p_getproc;
static XrInstance inst;

#define LOAD(var, name) do { if (XR_FAILED(p_getproc(inst, name, (PFN_xrVoidFunction *)&var))) \
        { printf("  missing %s\n", name); return 0; } } while (0)

static const char *res(XrResult r)
{
    static char b[64];

    if (inst) {
        PFN_xrResultToString f = NULL;

        p_getproc(inst, "xrResultToString", (PFN_xrVoidFunction *)&f);
        if (f && XR_SUCCEEDED(f(inst, r, b)))
            return b;
    }
    snprintf(b, sizeof b, "XrResult %d", (int)r);
    return b;
}

static int stage1(PFN_xrEnumerateInstanceExtensionProperties enum_ext, int *has_vk2)
{
    uint32_t n = 0, i;
    XrExtensionProperties *p;
    static const char *want[] = {
        "XR_KHR_vulkan_enable2", "XR_KHR_vulkan_enable", "XR_KHR_composition_layer_depth",
        "XR_KHR_composition_layer_cylinder", "XR_KHR_composition_layer_equirect2",
        "XR_KHR_visibility_mask", "XR_KHR_win32_convert_performance_counter_time",
        "XR_EXT_hand_tracking", "XR_FB_display_refresh_rate", "XR_EXT_performance_settings",
        "XR_META_foveation_eye_tracked", "XR_FB_foveation", "XR_KHR_binding_modification",
        "XR_EXT_debug_utils", "XR_KHR_vulkan_swapchain_format_list",
    };

    if (XR_FAILED(enum_ext(NULL, 0, &n, NULL)) || !n) {
        printf("stage 1: the runtime lists no instance extensions\n");
        return 0;
    }
    p = calloc(n, sizeof *p);
    for (i = 0; i < n; i++)
        p[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    enum_ext(NULL, n, &n, p);
    printf("stage 1: %u instance extensions\n", n);
    for (i = 0; i < sizeof want / sizeof want[0]; i++) {
        uint32_t k, v = 0;

        for (k = 0; k < n; k++)
            if (!strcmp(p[k].extensionName, want[i]))
                v = p[k].extensionVersion;
        printf("  %-48s %s\n", want[i], v ? "yes" : "no");
        if (!strcmp(want[i], "XR_KHR_vulkan_enable2"))
            *has_vk2 = v != 0;
    }
    if (getenv("XR_PROBE_ALL")) {
        for (i = 0; i < n; i++)
            printf("    %s\n", p[i].extensionName);
    }
    free(p);
    return 1;
}

static int stage23(void)
{
    XrInstanceCreateInfo ci;
    XrSystemGetInfo gi;
    XrSystemId sys = 0;
    XrSystemProperties sp;
    XrResult r;
    static const char *exts[] = { "XR_KHR_vulkan_enable2" };
    PFN_xrEnumerateViewConfigurationViews enum_views = NULL;
    PFN_xrGetSystem get_sys = NULL;
    PFN_xrGetSystemProperties get_props = NULL;
    PFN_xrGetInstanceProperties get_iprops = NULL;
    PFN_xrGetVulkanGraphicsRequirements2KHR get_req = NULL;
    PFN_xrCreateVulkanInstanceKHR mk_inst = NULL;
    PFN_xrGetVulkanGraphicsDevice2KHR get_dev = NULL;
    PFN_xrCreateVulkanDeviceKHR mk_dev = NULL;
    XrViewConfigurationView views[2];
    uint32_t nv = 0;
    XrInstanceProperties ip;
    XrGraphicsRequirementsVulkan2KHR req;
    HMODULE vk;
    PFN_vkGetInstanceProcAddr vkgipa;

    memset(&ci, 0, sizeof ci);
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    strcpy(ci.applicationInfo.applicationName, "xr-probe");
    strcpy(ci.applicationInfo.engineName, "none");
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = exts;
    {
        PFN_xrCreateInstance mk = NULL;

        p_getproc(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&mk);
        r = mk(&ci, &inst);
    }
    if (XR_FAILED(r)) {
        printf("stage 2: xrCreateInstance failed: %s (%d)\n", res(r), (int)r);
        return 0;
    }
    LOAD(get_sys, "xrGetSystem");
    LOAD(get_props, "xrGetSystemProperties");
    LOAD(get_iprops, "xrGetInstanceProperties");
    LOAD(enum_views, "xrEnumerateViewConfigurationViews");
    LOAD(get_req, "xrGetVulkanGraphicsRequirements2KHR");
    LOAD(mk_inst, "xrCreateVulkanInstanceKHR");
    LOAD(get_dev, "xrGetVulkanGraphicsDevice2KHR");
    LOAD(mk_dev, "xrCreateVulkanDeviceKHR");

    memset(&ip, 0, sizeof ip);
    ip.type = XR_TYPE_INSTANCE_PROPERTIES;
    get_iprops(inst, &ip);
    printf("stage 2: runtime \"%s\" version %u.%u.%u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
           XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));

    memset(&gi, 0, sizeof gi);
    gi.type = XR_TYPE_SYSTEM_GET_INFO;
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = get_sys(inst, &gi, &sys);
    if (XR_FAILED(r)) {
        printf("  xrGetSystem: %s: no headset system (is the headset connected and SteamVR running?)\n", res(r));
        return 0;
    }
    memset(&sp, 0, sizeof sp);
    sp.type = XR_TYPE_SYSTEM_PROPERTIES;
    get_props(inst, sys, &sp);
    printf("  system \"%s\": max swapchain %ux%u, layers %u, orientation %s, position %s\n", sp.systemName,
           sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight,
           sp.graphicsProperties.maxLayerCount, sp.trackingProperties.orientationTracking ? "yes" : "no",
           sp.trackingProperties.positionTracking ? "yes" : "no");
    r = enum_views(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nv, NULL);
    if (XR_FAILED(r) || nv != 2)
        printf("  view count: %s, %u\n", res(r), nv);
    memset(views, 0, sizeof views);
    views[0].type = views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    r = enum_views(inst, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nv, views);
    if (XR_SUCCEEDED(r))
        printf("  %u views, recommended %ux%u (max %ux%u), samples %u\n", nv, views[0].recommendedImageRectWidth,
               views[0].recommendedImageRectHeight, views[0].maxImageRectWidth, views[0].maxImageRectHeight,
               views[0].recommendedSwapchainSampleCount);

    memset(&req, 0, sizeof req);
    req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
    r = get_req(inst, sys, &req);
    if (XR_FAILED(r)) {
        printf("  xrGetVulkanGraphicsRequirements2KHR: %s\n", res(r));
        return 0;
    }
    printf("  Vulkan wanted: %u.%u.%u to %u.%u.%u\n", XR_VERSION_MAJOR(req.minApiVersionSupported),
           XR_VERSION_MINOR(req.minApiVersionSupported), XR_VERSION_PATCH(req.minApiVersionSupported),
           XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported),
           XR_VERSION_PATCH(req.maxApiVersionSupported));

    /* Stage 3: a Vulkan instance and device made through the runtime. */
    vk = LoadLibraryA("vulkan-1.dll");
    vkgipa = vk ? (PFN_vkGetInstanceProcAddr)GetProcAddress(vk, "vkGetInstanceProcAddr") : NULL;
    if (!vkgipa) {
        printf("stage 3: vulkan-1.dll not available\n");
        return 0;
    }
    {
        VkApplicationInfo app;
        VkInstanceCreateInfo vci;
        XrVulkanInstanceCreateInfoKHR xi;
        VkInstance vki = VK_NULL_HANDLE;
        VkResult vr = VK_SUCCESS;
        XrVulkanGraphicsDeviceGetInfoKHR di;
        VkPhysicalDevice pd = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties props;
        PFN_vkGetPhysicalDeviceProperties gpdp;
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi;
        VkDeviceCreateInfo dci;
        XrVulkanDeviceCreateInfoKHR xd;
        VkDevice vkd = VK_NULL_HANDLE;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties gqf;
        uint32_t nq = 0, qf = 0, k;
        VkQueueFamilyProperties qp[16];
        PFN_vkDestroyInstance vkDestroyInst;
        PFN_vkDestroyDevice vkDestroyDev;

        memset(&app, 0, sizeof app);
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion = VK_API_VERSION_1_3;
        memset(&vci, 0, sizeof vci);
        vci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        vci.pApplicationInfo = &app;
        memset(&xi, 0, sizeof xi);
        xi.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
        xi.systemId = sys;
        xi.pfnGetInstanceProcAddr = vkgipa;
        xi.vulkanCreateInfo = &vci;
        r = mk_inst(inst, &xi, &vki, &vr);
        if (XR_FAILED(r) || vr != VK_SUCCESS) {
            printf("stage 3: xrCreateVulkanInstanceKHR: %s, VkResult %d\n", res(r), (int)vr);
            return 0;
        }
        printf("stage 3: Vulkan 1.3 instance made through the runtime: ok\n");
        memset(&di, 0, sizeof di);
        di.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
        di.systemId = sys;
        di.vulkanInstance = vki;
        r = get_dev(inst, &di, &pd);
        if (XR_FAILED(r) || !pd) {
            printf("  xrGetVulkanGraphicsDevice2KHR: %s\n", res(r));
            return 0;
        }
        gpdp = (PFN_vkGetPhysicalDeviceProperties)vkgipa(vki, "vkGetPhysicalDeviceProperties");
        gpdp(pd, &props);
        printf("  the runtime wants the GPU \"%s\" (Vulkan %u.%u.%u)\n", props.deviceName,
               VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
               VK_API_VERSION_PATCH(props.apiVersion));
        gqf = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkgipa(vki, "vkGetPhysicalDeviceQueueFamilyProperties");
        gqf(pd, &nq, NULL);
        if (nq > 16)
            nq = 16;
        gqf(pd, &nq, qp);
        for (k = 0; k < nq; k++)
            if (qp[k].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                qf = k;
                break;
            }
        memset(&qi, 0, sizeof qi);
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = qf;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        memset(&dci, 0, sizeof dci);
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qi;
        memset(&xd, 0, sizeof xd);
        xd.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
        xd.systemId = sys;
        xd.pfnGetInstanceProcAddr = vkgipa;
        xd.vulkanPhysicalDevice = pd;
        xd.vulkanCreateInfo = &dci;
        r = mk_dev(inst, &xd, &vkd, &vr);
        if (XR_FAILED(r) || vr != VK_SUCCESS)
            printf("  xrCreateVulkanDeviceKHR: %s, VkResult %d\n", res(r), (int)vr);
        else
            printf("  a Vulkan device (graphics queue family %u) made through the runtime: ok\n", qf);
        vkDestroyDev = vkd ? (PFN_vkDestroyDevice)vkgipa(vki, "vkDestroyDevice") : NULL;
        if (vkDestroyDev)
            vkDestroyDev(vkd, NULL);
        vkDestroyInst = (PFN_vkDestroyInstance)vkgipa(vki, "vkDestroyInstance");
        vkDestroyInst(vki, NULL);
    }
    return 1;
}

int main(int argc, char **argv)
{
    HMODULE xr;
    PFN_xrEnumerateInstanceExtensionProperties enum_ext;
    int has_vk2 = 0, want_system = argc > 1 && !strcmp(argv[1], "--system");
    const char *paths[] = { "openxr_loader.dll",
                            "C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll" };
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0, xr = NULL; i < 2 && !xr; i++) {
        xr = LoadLibraryA(paths[i]);
        if (xr)
            printf("OpenXR loader: %s\n", paths[i]);
    }
    if (!xr) {
        printf("No openxr_loader.dll found.\n");
        return 1;
    }
    p_getproc = (PFN_xrGetInstanceProcAddr)GetProcAddress(xr, "xrGetInstanceProcAddr");
    enum_ext = (PFN_xrEnumerateInstanceExtensionProperties)GetProcAddress(xr, "xrEnumerateInstanceExtensionProperties");
    if (!p_getproc || !enum_ext) {
        printf("The loader lacks xrGetInstanceProcAddr.\n");
        return 1;
    }
    if (!stage1(enum_ext, &has_vk2))
        return 1;
    if (want_system) {
        if (!has_vk2) {
            printf("No XR_KHR_vulkan_enable2: stage 2 and 3 skipped.\n");
            return 1;
        }
        return stage23() ? 0 : 1;
    }
    printf("\n(run with --system to create an instance and Vulkan device through the runtime; it starts SteamVR)\n");
    return 0;
}
