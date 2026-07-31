#include <gtest/gtest.h>
#include "ui/file_transfer_widget.h"
#include "app/file_transfer_manager.h"
#include <QApplication>
#include <QTreeView>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>

using namespace xrk;

// Receives the local tree's (private) signals so the test can assert on them.
class SignalCatcher : public QObject {
    Q_OBJECT
public:
    bool lastActive = false;
    int activeCount = 0;
    int dropCount = 0;

public slots:
    void onActive(bool a) { lastActive = a; ++activeCount; }
    void onDropped(const QMimeData*, const QModelIndex&) { ++dropCount; }
};

// Verifies the drag-to-download highlight interaction by dispatching synthetic
// Qt drag events to the local tree and asserting the "dropping" state and the
// drag/active/dropped signals fire. (The per-row highlight is painted by a
// delegate driven by the same state, but indexAt() needs a real laid-out
// viewport, so it is exercised manually on the desktop.)
class FileTransferDragTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<FileTransferManager>(nullptr);
        widget = std::make_unique<FileTransferWidget>(manager.get());
        tree = widget->findChild<QTreeView*>("local-tree");
        ASSERT_NE(tree, nullptr);
        catcher = new SignalCatcher;
        QObject::connect(tree, SIGNAL(dragActiveChanged(bool)), catcher, SLOT(onActive(bool)));
        QObject::connect(tree, SIGNAL(remoteDropped(const QMimeData*, const QModelIndex&)),
                catcher, SLOT(onDropped(const QMimeData*, const QModelIndex&)));
        mime = new QMimeData;
        mime->setData("application/x-xrk-remote-path",
                      "C:\\remote\\file.txt\nfile.txt\n0");
    }

    void TearDown() override {
        delete mime;
        delete catcher;
        widget.reset();
        manager.reset();
    }

    std::unique_ptr<FileTransferManager> manager;
    std::unique_ptr<FileTransferWidget> widget;
    QTreeView* tree = nullptr;
    SignalCatcher* catcher = nullptr;
    QMimeData* mime = nullptr;
};

TEST_F(FileTransferDragTest, DragEnterActivatesHighlight) {
    QDragEnterEvent ev(QPoint(10, 10), Qt::CopyAction, mime,
                       Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &ev);

    EXPECT_TRUE(tree->property("dropping").toBool());
    EXPECT_EQ(catcher->activeCount, 1);
    EXPECT_TRUE(catcher->lastActive);
}

TEST_F(FileTransferDragTest, DragLeaveClearsHighlight) {
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &enter);

    QDragLeaveEvent leave;
    QApplication::sendEvent(tree->viewport(), &leave);

    EXPECT_FALSE(tree->property("dropping").toBool());
    EXPECT_EQ(catcher->activeCount, 2);
    EXPECT_FALSE(catcher->lastActive);
}

TEST_F(FileTransferDragTest, DropEmitsRemoteDroppedAndClearsHighlight) {
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &enter);

    QDropEvent drop(QPoint(10, 10), Qt::CopyAction, mime,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &drop);

    EXPECT_EQ(catcher->dropCount, 1);
    // Drop must clear the highlight state.
    EXPECT_FALSE(tree->property("dropping").toBool());
    EXPECT_EQ(catcher->activeCount, 2);
    EXPECT_FALSE(catcher->lastActive);
}

TEST_F(FileTransferDragTest, NonMatchingMimeIsIgnored) {
    QMimeData other;
    other.setText("just some text");

    QDragEnterEvent ev(QPoint(10, 10), Qt::CopyAction, &other,
                       Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &ev);

    EXPECT_FALSE(tree->property("dropping").toBool());
    EXPECT_EQ(catcher->activeCount, 0);
}

#include "test_file_transfer_drag.moc"
