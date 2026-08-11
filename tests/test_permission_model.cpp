#include <gtest/gtest.h>

#include "core/permission_model.h"

using namespace xrk;

TEST(PermissionModelTest, RoleDefaults) {
    // Viewer gets view/chat/sysinfo/annotation only.
    uint32_t viewer = PermissionModel::roleCapabilities(PermLevel::Viewer);
    EXPECT_TRUE(PermissionModel::hasCapability(viewer, Capability::ViewScreen));
    EXPECT_TRUE(PermissionModel::hasCapability(viewer, Capability::Chat));
    EXPECT_TRUE(PermissionModel::hasCapability(viewer, Capability::SysInfo));
    EXPECT_TRUE(PermissionModel::hasCapability(viewer, Capability::Annotation));
    EXPECT_FALSE(PermissionModel::hasCapability(viewer, Capability::ControlInput));
    EXPECT_FALSE(PermissionModel::hasCapability(viewer, Capability::FileWrite));
    EXPECT_FALSE(PermissionModel::hasCapability(viewer, Capability::Terminal));
    EXPECT_FALSE(PermissionModel::hasCapability(viewer, Capability::PowerControl));
}

TEST(PermissionModelTest, RoleDefaultsOperator) {
    uint32_t op = PermissionModel::roleCapabilities(PermLevel::Operator);
    EXPECT_TRUE(PermissionModel::hasCapability(op, Capability::ControlInput));
    EXPECT_TRUE(PermissionModel::hasCapability(op, Capability::Clipboard));
    EXPECT_TRUE(PermissionModel::hasCapability(op, Capability::FileRead));
    EXPECT_TRUE(PermissionModel::hasCapability(op, Capability::FileWrite));
    EXPECT_TRUE(PermissionModel::hasCapability(op, Capability::ProcessView));
    EXPECT_TRUE(PermissionModel::hasCapability(op, Capability::Calls));
    EXPECT_TRUE(PermissionModel::hasCapability(op, Capability::Record));
    // Operator still lacks admin-only caps.
    EXPECT_FALSE(PermissionModel::hasCapability(op, Capability::ProcessManage));
    EXPECT_FALSE(PermissionModel::hasCapability(op, Capability::Terminal));
    EXPECT_FALSE(PermissionModel::hasCapability(op, Capability::PowerControl));
    EXPECT_FALSE(PermissionModel::hasCapability(op, Capability::UserManage));
}

TEST(PermissionModelTest, RoleDefaultsAdmin) {
    uint32_t admin = PermissionModel::roleCapabilities(PermLevel::Admin);
    EXPECT_TRUE(PermissionModel::hasCapability(admin, Capability::ProcessManage));
    EXPECT_TRUE(PermissionModel::hasCapability(admin, Capability::Terminal));
    EXPECT_TRUE(PermissionModel::hasCapability(admin, Capability::PowerControl));
    EXPECT_TRUE(PermissionModel::hasCapability(admin, Capability::UserManage));
    // Admin inherits everything an Operator has.
    EXPECT_TRUE(PermissionModel::hasCapability(admin, Capability::ControlInput));
    EXPECT_TRUE(PermissionModel::hasCapability(admin, Capability::FileWrite));
}

TEST(PermissionModelTest, NoneHasNoCapabilities) {
    EXPECT_EQ(PermissionModel::roleCapabilities(PermLevel::None), 0u);
    EXPECT_FALSE(PermissionModel::hasCapability(
        PermLevel::None, 0xFFFFFFFFu, Capability::ViewScreen));
}

TEST(PermissionModelTest, HostTogglesConverge) {
    // Turn off Terminal and PowerControl globally.
    uint32_t toggles = kAllCapabilities
                      & ~static_cast<uint32_t>(Capability::Terminal)
                      & ~static_cast<uint32_t>(Capability::PowerControl);

    // An Admin loses Terminal/PowerControl when the host disables them...
    uint32_t eff = PermissionModel::evaluate(PermLevel::Admin, toggles);
    EXPECT_FALSE(PermissionModel::hasCapability(eff, Capability::Terminal));
    EXPECT_FALSE(PermissionModel::hasCapability(eff, Capability::PowerControl));
    // ...but keeps everything else including UserManage.
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::ControlInput));
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::UserManage));
}

TEST(PermissionModelTest, UserManageNeverDisabledByToggle) {
    // Even with ALL toggles off, a user that has UserManage keeps it.
    uint32_t eff = PermissionModel::evaluate(PermLevel::Admin, 0u);
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::UserManage));
    // But other caps are gone.
    EXPECT_FALSE(PermissionModel::hasCapability(eff, Capability::ControlInput));
}

TEST(PermissionModelTest, DeviceLevelOverride) {
    // Device override promotes a Viewer connection to Operator-level caps.
    DeviceOverride ov;
    ov.level = PermLevel::Operator;
    ov.capMask = -1; // use role default for the override level
    uint32_t eff = PermissionModel::evaluate(PermLevel::Viewer, kAllCapabilities, ov);
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::ControlInput));
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::FileWrite));
}

TEST(PermissionModelTest, DeviceCapMaskOverrideDemotes) {
    // Device override with an explicit capMask can further restrict an Admin.
    DeviceOverride ov;
    ov.level = PermLevel::Admin;
    ov.capMask = static_cast<int32_t>(Capability::ViewScreen)
               | static_cast<int32_t>(Capability::Chat);
    uint32_t eff = PermissionModel::evaluate(PermLevel::Admin, kAllCapabilities, ov);
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::ViewScreen));
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::Chat));
    EXPECT_FALSE(PermissionModel::hasCapability(eff, Capability::ControlInput));
    EXPECT_FALSE(PermissionModel::hasCapability(eff, Capability::PowerControl));
}

TEST(PermissionModelTest, DeviceOverrideWithHostToggle) {
    // Override level + host toggle both apply.
    DeviceOverride ov;
    ov.level = PermLevel::Operator; // has Terminal? no. has ControlInput yes.
    uint32_t toggles = kAllCapabilities
                      & ~static_cast<uint32_t>(Capability::FileWrite);
    uint32_t eff = PermissionModel::evaluate(PermLevel::Viewer, toggles, ov);
    EXPECT_TRUE(PermissionModel::hasCapability(eff, Capability::ControlInput));
    EXPECT_FALSE(PermissionModel::hasCapability(eff, Capability::FileWrite));
}

TEST(PermissionModelTest, CapabilityNameRoundTrip) {
    Capability cap;
    EXPECT_TRUE(PermissionModel::parseCapabilityName("Terminal", cap));
    EXPECT_EQ(cap, Capability::Terminal);
    EXPECT_STREQ(PermissionModel::capabilityName(Capability::Terminal), "Terminal");
    EXPECT_FALSE(PermissionModel::parseCapabilityName("Nope", cap));
}
