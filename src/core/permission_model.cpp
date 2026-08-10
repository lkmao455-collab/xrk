#include "permission_model.h"

#include <map>
#include <utility>

namespace xrk {

uint32_t PermissionModel::roleCapabilities(PermLevel level) {
    using C = Capability;
    constexpr uint32_t viewer = static_cast<uint32_t>(C::ViewScreen)
                               | static_cast<uint32_t>(C::Chat)
                               | static_cast<uint32_t>(C::SysInfo)
                               | static_cast<uint32_t>(C::Annotation);
    constexpr uint32_t op = viewer
                            | static_cast<uint32_t>(C::ControlInput)
                            | static_cast<uint32_t>(C::Clipboard)
                            | static_cast<uint32_t>(C::FileRead)
                            | static_cast<uint32_t>(C::FileWrite)
                            | static_cast<uint32_t>(C::ProcessView)
                            | static_cast<uint32_t>(C::Calls)
                            | static_cast<uint32_t>(C::Record);
    constexpr uint32_t admin = op
                              | static_cast<uint32_t>(C::ProcessManage)
                              | static_cast<uint32_t>(C::Terminal)
                              | static_cast<uint32_t>(C::PowerControl)
                              | static_cast<uint32_t>(C::UserManage);

    switch (level) {
        case PermLevel::Admin:    return admin;
        case PermLevel::Operator: return op;
        case PermLevel::Viewer:   return viewer;
        case PermLevel::None:
        default:                  return 0;
    }
}

bool PermissionModel::hasCapability(uint32_t mask, Capability cap) {
    return (mask & static_cast<uint32_t>(cap)) != 0;
}

uint32_t PermissionModel::evaluate(PermLevel level,
                                   uint32_t hostToggles,
                                   const std::optional<DeviceOverride>& override) {
    uint32_t mask = roleCapabilities(level);
    if (override.has_value()) {
        if (override->capMask >= 0) {
            // Explicit capability AND-mask on top of the role's caps.
            mask &= static_cast<uint32_t>(override->capMask);
        } else {
            // Level-only override: take the override level's role caps.
            mask = roleCapabilities(override->level);
        }
    }

    // Host-level toggles converge everything... except UserManage, which must
    // never be disabled by a toggle (otherwise an admin could lock out
    // user management for everyone, including themselves).
    const uint32_t userManage = mask & static_cast<uint32_t>(Capability::UserManage);
    uint32_t result = (mask & hostToggles) | userManage;
    return result & kAllCapabilities;
}

bool PermissionModel::hasCapability(PermLevel level,
                                    uint32_t hostToggles,
                                    Capability cap,
                                    const std::optional<DeviceOverride>& override) {
    return hasCapability(evaluate(level, hostToggles, override), cap);
}

const char* PermissionModel::capabilityName(Capability cap) {
    switch (cap) {
        case Capability::ViewScreen:    return "ViewScreen";
        case Capability::ControlInput:  return "ControlInput";
        case Capability::Clipboard:     return "Clipboard";
        case Capability::FileRead:      return "FileRead";
        case Capability::FileWrite:     return "FileWrite";
        case Capability::ProcessView:   return "ProcessView";
        case Capability::ProcessManage: return "ProcessManage";
        case Capability::Terminal:      return "Terminal";
        case Capability::SysInfo:       return "SysInfo";
        case Capability::PowerControl:  return "PowerControl";
        case Capability::Calls:         return "Calls";
        case Capability::Chat:          return "Chat";
        case Capability::Annotation:    return "Annotation";
        case Capability::Record:        return "Record";
        case Capability::UserManage:    return "UserManage";
        default:                        return "Unknown";
    }
}

bool PermissionModel::parseCapabilityName(const std::string& name, Capability& out) {
    static const std::map<std::string, Capability> table = {
        {"ViewScreen",    Capability::ViewScreen},
        {"ControlInput",  Capability::ControlInput},
        {"Clipboard",     Capability::Clipboard},
        {"FileRead",      Capability::FileRead},
        {"FileWrite",     Capability::FileWrite},
        {"ProcessView",   Capability::ProcessView},
        {"ProcessManage", Capability::ProcessManage},
        {"Terminal",      Capability::Terminal},
        {"SysInfo",       Capability::SysInfo},
        {"PowerControl",  Capability::PowerControl},
        {"Calls",         Capability::Calls},
        {"Chat",          Capability::Chat},
        {"Annotation",    Capability::Annotation},
        {"Record",        Capability::Record},
        {"UserManage",    Capability::UserManage},
    };
    auto it = table.find(name);
    if (it == table.end()) return false;
    out = it->second;
    return true;
}

} // namespace xrk
