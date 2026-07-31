#include <gtest/gtest.h>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QStringList>

// Verifies that every SVG/PNG icon referenced at runtime is actually present
// in the compiled resource bundle AND decodable by the Qt SVG icon engine,
// and that it rasterizes to something visibly non-empty (not a blank icon).
// A silent failure here (icon not found / empty) is exactly the long-standing
// "buttons have no visible icon" problem reported by the user.
TEST(SvgIconTest, AllNavIconsLoadAndRender) {
    QStringList icons = {"home", "desktop", "files", "terminal",
                         "chat", "monitor", "clipboard", "settings"};
    for (const QString& n : icons) {
        QIcon ic(":/icons/" + n + ".svg");
        ASSERT_FALSE(ic.isNull()) << "SVG icon not found/decoded: " << n.toStdString();

        // Render at a typical toolbar size and count non-transparent pixels.
        QPixmap pm = ic.pixmap(QSize(32, 32));
        ASSERT_FALSE(pm.isNull()) << "SVG icon pixmap blank: " << n.toStdString();
        QImage img = pm.toImage();
        int visible = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (qAlpha(img.pixel(x, y)) > 0) ++visible;
            }
        }
        EXPECT_GT(visible, 0) << "SVG icon renders nothing visible: " << n.toStdString();
    }

    // Window icon (PNG) referenced from main.cpp.
    QIcon win(":/icons/xrk_window.png");
    EXPECT_FALSE(win.isNull()) << "xrk_window.png not found/decoded";
}
