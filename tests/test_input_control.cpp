#include <gtest/gtest.h>
#include "hw/input_control.h"

#ifdef _WIN32
namespace xrk {
namespace {

// Mouse event flags (winuser.h MOUSEEVENTF_*). Defined as literals because the
// test TU does not reliably expose the MOUSEEVENTF_* macros via Qt's headers.
constexpr uint32_t ME_LEFTDOWN = 0x0002;
constexpr uint32_t ME_LEFTUP = 0x0004;
constexpr uint32_t ME_RIGHTDOWN = 0x0008;
constexpr uint32_t ME_RIGHTUP = 0x0010;
constexpr uint32_t ME_WHEEL = 0x0800;

// The injection must be layout/IME independent: typed characters are carried
// as UTF-16 code units, and non-character keys keep the VK path with the
// correct extended-key flag. These helpers fully determine the INPUT[] that
// keyEvent() sends to SendInput, so testing them proves the cross-layout fix.

TEST(InputControlKeyTest, UnicodeUnitsSingleChar) {
    InputControl ic;
    QVector<uint16_t> u = ic.unicodeUnits(QStringLiteral("A"));
    ASSERT_EQ(u.size(), 1);
    EXPECT_EQ(u[0], 0x0041u);
}

TEST(InputControlKeyTest, UnicodeUnitsCJKIsLayoutIndependent) {
    InputControl ic;
    QVector<uint16_t> u = ic.unicodeUnits(QStringLiteral("你"));
    ASSERT_EQ(u.size(), 1);
    EXPECT_EQ(u[0], 0x4F60u);  // U+4F60
}

TEST(InputControlKeyTest, UnicodeUnitsSurrogatePair) {
    InputControl ic;
    // U+1F600 GRINNING FACE -> surrogate pair 0xD83D 0xDE00
    QVector<uint16_t> u = ic.unicodeUnits(QStringLiteral("😀"));
    ASSERT_EQ(u.size(), 2);
    EXPECT_EQ(u[0], 0xD83Du);
    EXPECT_EQ(u[1], 0xDE00u);
}

TEST(InputControlKeyTest, UnicodeUnitsStrayLowSurrogateSkipped) {
    InputControl ic;
    QString s;
    s.append(QChar::lowSurrogate(0xDE00));  // lone low surrogate
    s.append(QChar('A'));
    QVector<uint16_t> u = ic.unicodeUnits(s);
    ASSERT_EQ(u.size(), 1);
    EXPECT_EQ(u[0], 0x0041u);
}

TEST(InputControlKeyTest, ExtendedFlagForArrowsButNotLetters) {
    InputControl ic;
    EXPECT_TRUE(ic.isExtendedKey(0x25));   // VK_LEFT
    EXPECT_TRUE(ic.isExtendedKey(0xA3));   // VK_RCONTROL
    EXPECT_TRUE(ic.isExtendedKey(0x5B));   // VK_LWIN
    EXPECT_FALSE(ic.isExtendedKey(0x0D));  // VK_RETURN (main Enter, not extended)
    EXPECT_FALSE(ic.isExtendedKey(0x41));  // VK_A
    EXPECT_FALSE(ic.isExtendedKey(0x31));  // VK_1
}

TEST(InputControlMouseTest, ButtonFlagsMapCorrectly) {
    InputControl ic;
    EXPECT_EQ(ic.mouseButtonFlags(MouseButton::LEFT, true),
              static_cast<uint32_t>(ME_LEFTDOWN));
    EXPECT_EQ(ic.mouseButtonFlags(MouseButton::LEFT, false),
              static_cast<uint32_t>(ME_LEFTUP));
    EXPECT_EQ(ic.mouseButtonFlags(MouseButton::RIGHT, true),
              static_cast<uint32_t>(ME_RIGHTDOWN));
    EXPECT_EQ(ic.mouseButtonFlags(MouseButton::RIGHT, false),
              static_cast<uint32_t>(ME_RIGHTUP));
    // Middle button uses the non-left/right codes (0x0020 / 0x0040).
    EXPECT_EQ(ic.mouseButtonFlags(MouseButton::MIDDLE, true), 0x0020u);
    EXPECT_EQ(ic.mouseButtonFlags(MouseButton::MIDDLE, false), 0x0040u);
}

TEST(InputControlMouseTest, NormalizedPosIsProportionalAndBounded) {
    InputControl ic;
    EXPECT_EQ(ic.mouseNormalizedPos(0, 0), QPoint(0, 0));
    // Proportional and monotonic: (200,200) must scale ~2x vs (100,100) and
    // stay inside the absolute-coordinate range. Screen-size independent.
    QPoint a = ic.mouseNormalizedPos(100, 100);
    QPoint b = ic.mouseNormalizedPos(200, 200);
    EXPECT_GT(a.x(), 0); EXPECT_GT(a.y(), 0);
    EXPECT_GT(b.x(), a.x()); EXPECT_GT(b.y(), a.y());
    EXPECT_NEAR(static_cast<double>(a.x()) * 2.0, b.x(), 1.0);
    EXPECT_NEAR(static_cast<double>(a.y()) * 2.0, b.y(), 1.0);
    EXPECT_LE(a.x(), 65535); EXPECT_LE(a.y(), 65535);
    EXPECT_LE(b.x(), 65535); EXPECT_LE(b.y(), 65535);
}

TEST(InputControlMouseTest, ScrollFlagsAndData) {
    InputControl ic;
    EXPECT_EQ(ic.mouseScrollFlags(), static_cast<uint32_t>(ME_WHEEL));
    EXPECT_EQ(ic.mouseScrollData(120), 120);
    EXPECT_EQ(ic.mouseScrollData(-120), -120);
}

} // namespace
} // namespace xrk

#endif // _WIN32
