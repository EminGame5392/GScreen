#include "Specialization.h"
#include "Log.h"
#include <initguid.h>
#include <ntddvdeo.h> // GUID_MONITOR_OVERRIDE_PSEUDO_SPECIALIZED
#include <cwchar>

namespace gs {

typedef HRESULT(WINAPI* SLGetWindowsInformationDWORD_t)(PCWSTR, DWORD*);

bool ExclusiveModeLicensed() {
    HMODULE slc = LoadLibraryExW(L"slc.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!slc) return false;
    auto fn = (SLGetWindowsInformationDWORD_t)GetProcAddress(slc, "SLGetWindowsInformationDWORD");
    DWORD value = 0;
    bool ok = fn && SUCCEEDED(fn(L"Display-Specialized-Displays-Enabled", &value)) && value == 1;
    FreeLibrary(slc);
    return ok;
}

bool IsPanelRemovedFromDesktop(const DisplayInfo& panel) {
    DISPLAYCONFIG_GET_MONITOR_SPECIALIZATION g{};
    g.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_MONITOR_SPECIALIZATION;
    g.header.size = sizeof g;
    g.header.adapterId = panel.key.adapterId;
    g.header.id = panel.key.targetId;
    if (DisplayConfigGetDeviceInfo(&g.header) != ERROR_SUCCESS) return false;
    return g.isSpecializationEnabled != 0;
}

bool SetPanelRemovedFromDesktop(const DisplayInfo& panel, bool removed) {
    if (IsPanelRemovedFromDesktop(panel) == removed) return true;
    DISPLAYCONFIG_SET_MONITOR_SPECIALIZATION s{};
    s.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_MONITOR_SPECIALIZATION;
    s.header.size = sizeof s;
    s.header.adapterId = panel.key.adapterId;
    s.header.id = panel.key.targetId;
    s.isSpecializationEnabled = removed ? 1 : 0;
    s.specializationType = GUID_MONITOR_OVERRIDE_PSEUDO_SPECIALIZED;
    s.specializationSubType = GUID{};
    wcscpy_s(s.specializationApplicationName, L"GScreen");
    LONG r = DisplayConfigSetDeviceInfo(&s.header);
    if (r != ERROR_SUCCESS) LogError("Set monitor specialization({}) = {}", removed, r);
    return r == ERROR_SUCCESS;
}

} // namespace gs
