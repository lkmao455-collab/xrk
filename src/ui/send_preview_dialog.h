#pragma once

#include <QDialog>
#include <QStringList>
#include <QList>

namespace xrk {

// A single entry in the send-preview list.
struct SendPreviewItem {
    QString path;   // absolute source path
    QString name;   // display name (file or folder name)
    qint64 size = 0;
    bool isDir = false;
};

// Confirmation dialog shown before sending one or more files / folders.
// Displays the item list with per-item and total size, plus the E2EE status,
// so the user can review a batch before it actually starts transferring.
class SendPreviewDialog : public QDialog {
    Q_OBJECT
public:
    // items: files and/or folders the user chose to send.
    // e2eeAvailable: whether an E2EE session exists for the target peer.
    SendPreviewDialog(const QString& peerName, const QList<SendPreviewItem>& items,
                      bool e2eeAvailable, QWidget* parent = nullptr);

    // The list of items the user confirmed (unchanged from input on accept).
    QList<SendPreviewItem> items() const { return m_items; }

private:
    static QString formatBytes(qint64 bytes);

    QList<SendPreviewItem> m_items;
};

} // namespace xrk
