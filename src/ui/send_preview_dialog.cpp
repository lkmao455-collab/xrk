#include "send_preview_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QCoreApplication>

namespace xrk {

QString SendPreviewDialog::formatBytes(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    double v = static_cast<double>(bytes);
    if (v < 1024.0 * 1024.0) return QString("%1 KB").arg(v / 1024.0, 0, 'f', 1);
    if (v < 1024.0 * 1024.0 * 1024.0) return QString("%1 MB").arg(v / 1024.0 / 1024.0, 0, 'f', 1);
    return QString("%1 GB").arg(v / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
}

SendPreviewDialog::SendPreviewDialog(const QString& peerName, const QList<SendPreviewItem>& items,
                                     bool e2eeAvailable, QWidget* parent)
    : QDialog(parent), m_items(items) {
    setWindowTitle(tr("发送确认"));
    setMinimumWidth(460);
    setMinimumHeight(320);
    setStyleSheet(
        "QDialog { background-color: #1E1E1E; }"
        "QLabel { color: #E0E0E0; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    QLabel* title = new QLabel(tr("发送给：%1").arg(peerName), this);
    title->setStyleSheet("font-size: 14px; font-weight: 600; color: #07C160;");
    layout->addWidget(title);

    auto* list = new QListWidget(this);
    list->setStyleSheet(
        "QListWidget { background-color: #252525; border: 1px solid #333; border-radius: 6px; color: #E0E0E0; }"
        "QListWidget::item { padding: 4px; }");
    qint64 total = 0;
    int totalItems = 0;
    for (const SendPreviewItem& it : items) {
        total += it.size;
        totalItems += it.isDir ? 1 : 1;
        QString label = QString("%1   %2").arg(it.name, formatBytes(it.size));
        list->addItem(label);
    }
    layout->addWidget(list, 1);

    QLabel* summary = new QLabel(tr("共 %1 项，%2").arg(totalItems).arg(formatBytes(total)), this);
    summary->setStyleSheet("color: #AAA; font-size: 13px;");
    layout->addWidget(summary);

    QLabel* sec = new QLabel(this);
    if (e2eeAvailable) {
        sec->setText("🔒 " + tr("端到端加密传输（已协商密钥）"));
        sec->setStyleSheet("color: #07C160; font-size: 12px;");
    } else {
        sec->setText("⚠ " + tr("明文传输（尚未建立加密会话，将自动尝试协商）"));
        sec->setStyleSheet("color: #E6A23C; font-size: 12px;");
    }
    layout->addWidget(sec);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QString btnStyle =
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 6px; "
        "padding: 6px 18px; font-size: 13px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }";
    QPushButton* cancel = new QPushButton(tr("取消"), this);
    cancel->setStyleSheet(btnStyle);
    cancel->setCursor(Qt::PointingHandCursor);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancel);

    QPushButton* ok = new QPushButton(tr("发送"), this);
    ok->setStyleSheet(
        "QPushButton { background-color: #07C160; color: white; border: none; border-radius: 6px; "
        "padding: 6px 18px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background-color: #06AD56; }");
    ok->setCursor(Qt::PointingHandCursor);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(ok);
    layout->addLayout(btnLayout);
}

} // namespace xrk
