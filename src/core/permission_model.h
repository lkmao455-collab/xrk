#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace xrk {

// Permission level assigned to a connected session. Higher level => more
// capabilities. `None` means the session holds no granted permission (e.g.
// authenticated but not yet authorized, or a denied connection).
enum class PermLevel : uint8_t {
    None = 0,
    Viewer = 1,
    Operator = 2,
    Admin = 3,
};

// Granular capabilities. Stored as a bitmask (uint32_t). The top bit (UserManage)
// is special: host-level toggles can never switch it OFF, to avoid an admin
// locking themselves (and everyone) out of user management.
enum class Capability : uint32_t {
    ViewScreen    = 1u << 0,
    ControlInput  = 1u << 1,
    Clipboard     = 1u << 2,
    FileRead      = 1u << 3,
    FileWrite     = 1u << 4,
    ProcessView   = 1u << 5,
    ProcessManage = 1u << 6,
    Terminal      = 1u << 7,
    SysInfo       = 1u << 8,
    PowerControl  = 1u << 9,
    Calls         = 1u << 10,
    Chat          = 1u << 11,
    Annotation    = 1u << 12,
    Record        = 1u << 13,
    UserManage    = 1u << 14,
};

// All capability bits (bits 0..14).
constexpr uint32_t kAllCapabilities = (1u << 15) - 1;

// Per-device/per-contact override of the default role capabilities.
struct DeviceOverride {
    PermLevel level = PermLevel::None;
    // Explicit capability AND-mask. -1 means "use the role default for
    // `level`" (i.e. only override the level, not individual capabilities).
    int32_t capMask = -1;
};

// Pure, dependency-free permission evaluation used by both the Host (enforcement)
// and tests. Does NOT depend on Qt/Host.
class PermissionModel {
public:
    // Capability bitmask granted to a role by default (ignoring host toggles).
    static uint32_t roleCapabilities(PermLevel level);

    // Whether a capability bit is set in a mask.
    static bool hasCapability(uint32_t mask, Capability cap);

    // Effective capability mask for a session:
    //   base = device override level's caps (if override present and uses a
    //          level) else the session role's caps;
    //   if override carries an explicit capMask (>=0), AND it in;
    //   result = base & hostToggles, except UserManage which host toggles
    //   can never disable (self-lock protection).
    static uint32_t evaluate(PermLevel level,
                             uint32_t hostToggles,
                             const std::optional<DeviceOverride>& override = std::nullopt);

    // Convenience: evaluate then test a single capability.
    static bool hasCapability(PermLevel level,
                              uint32_t hostToggles,
                              Capability cap,
                              const std::optional<DeviceOverride>& override = std::nullopt);

    // Stable name used for KV keys (perm/toggle/<name>) and UI labels.
    static const char* capabilityName(Capability cap);
    static bool parseCapabilityName(const std::string& name, Capability& out);
};

} // namespace xrk
