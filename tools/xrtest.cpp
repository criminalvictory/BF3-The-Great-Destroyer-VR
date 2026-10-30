// Creates an OpenXR instance the same way the mod does, outside the game, with loader
// debug output on. For diagnosing "runtime unavailable" without restarting BF3.

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdio>
#include <cstring>

int main() {
    SetEnvironmentVariableA("XR_LOADER_DEBUG", "all");
    printf("Build: %s\n", sizeof(void*) == 8 ? "x64" : "x86");

    const char* extensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "bf3vr xrtest");
    strcpy_s(ci.applicationInfo.engineName, "bf3vr");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = extensions;

    XrInstance instance = XR_NULL_HANDLE;
    XrResult r = xrCreateInstance(&ci, &instance);
    printf("xrCreateInstance -> %d\n", static_cast<int>(r));
    if (XR_FAILED(r))
        return 1;

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance, &props);
    printf("Runtime: %s\n", props.runtimeName);

    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    printf("xrGetSystem -> %d\n", static_cast<int>(xrGetSystem(instance, &si, &system)));
    xrDestroyInstance(instance);
    return 0;
}
