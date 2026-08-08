#include "transfer_task_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QDateTime>
#include <QCoreApplication>
#include <QFileInfo>

namespace xrk {

// ----------------------------------------------------------------------------
// TransferTaskCard
// ----------------------------------------------------------------------------

QString TransferTaskCard::formatBytes(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    double v = static_cast<double>(bytes);
    if (v < 1024.0 * 1024.0) return QString("%1 KB").arg(v / 1024.0, 0, 'f', 1);
    if (v < 1024.0 * 1024.0 * 1024.0) return QString("%1 MB").arg(v / 1024.0 / 1024.0, 0, 'f', 1);
    return QString("%1 GB").arg(v / 1024.0 / 1024.0 / 1024.0, 0, 'f', 2);
}

QString TransferTaskCard::formatSpeed(int bps) {
    if (bps <= 0) return "--";
    double v = static_cast<double>(bps);
    if (v < 1024.0 * 1024.0) return QString("%1 KB/s").arg(v / 1024.0, 0, 'f', 0);
    return QString("%1 MB/s").arg(v / 1024.0 / 1024.0, 0, 'f', 1);
}

QString TransferTaskCard::formatDuration(qint64 seconds) {
    if (seconds <= 0 || seconds > 365 * 24 * 3600) return "--";
    qint64 h = seconds / 3600;
    qint64 m = (seconds % 3600) / 60;
    qint64 s = seconds % 60;
    if (h > 0) return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    if (m > 0) return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    return QString("0:%1").arg(s, 2, 10, QChar('0'));
}

QString TransferTaskCard::statusText(TransferStatus status) {
    switch (status) {
        case TransferStatus::Queued:     return QCoreApplication::translate("Transfer", "排队中");
        case TransferStatus::Transferring: return QCoreApplication::translate("Transfer", "传输中");
        case TransferStatus::Paused:     return QCoreApplication::translate("Transfer", "已暂停");
        case TransferStatus::Completed:  return QCoreApplication::translate("Transfer", "已完成");
        case TransferStatus::Failed:     return QCoreApplication::translate("Transfer", "失败");
        case TransferStatus::Cancelled:  return QCoreApplication::translate("Transfer", "已取消");
    }
    return "";
}

TransferTaskCard::TransferTaskCard(IPMsgManager* manager, const IPMsgTransferTask& task, QWidget* parent)
    : QFrame(parent), m_manager(manager), m_fileId(task.fileId), m_task(task) {
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(
        "TransferTaskCard { background-color: #252525; border: 1px solid #333; border-radius: 8px; }"
        "QLabel { color: #E0E0E0; }");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 10, 12, 10);
    mainLayout->setSpacing(8);

    // Row 1: title + status + remove
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(8);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("font-size: 13px; font-weight: 600;");
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row1->addWidget(m_titleLabel, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("font-size: 12px; padding: 1px 8px; border-radius: 8px;");
    row1->addWidget(m_statusLabel);

    m_removeBtn = new QPushButton("✕", this);
    m_removeBtn->setFixedSize(24, 24);
    m_removeBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #AAA; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4A4A4A; color: #FFF; }");
    m_removeBtn->setCursor(Qt::PointingHandCursor);
    m_removeBtn->setToolTip(tr("移除记录"));
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (m_manager) m_manager->removeTransfer(m_fileId);
        emit requestRefresh();
    });
    row1->addWidget(m_removeBtn);

    mainLayout->addLayout(row1);

    // Progress bar
    m_progress = new QProgressBar(this);
    m_progress->setFixedHeight(8);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet(
        "QProgressBar { background-color: #1A1A1A; border: none; border-radius: 4px; }"
        "QProgressBar::chunk { background-color: #07C160; border-radius: 4px; }");
    mainLayout->addWidget(m_progress);

    // Row 2: info + actions
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(8);
    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet("font-size: 12px; color: #AAA;");
    m_infoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row2->addWidget(m_infoLabel, 1);

    QString btnStyle =
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 4px; "
        "padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #2A2A2A; }";

    m_pauseBtn = new QPushButton(tr("暂停"), this);
    m_pauseBtn->setStyleSheet(btnStyle);
    m_pauseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_manager) m_manager->pauseFile(m_fileId);
    });
    row2->addWidget(m_pauseBtn);

    m_resumeBtn = new QPushButton(tr("继续"), this);
    m_resumeBtn->setStyleSheet(btnStyle);
    m_resumeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resumeBtn, &QPushButton::clicked, this, [this]() {
        if (m_manager) m_manager->resumeFile(m_fileId);
    });
    row2->addWidget(m_resumeBtn);

    m_cancelBtn = new QPushButton(tr("取消"), this);
    m_cancelBtn->setStyleSheet(btnStyle);
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_manager) m_manager->cancelFile(m_fileId);
    });
    row2->addWidget(m_cancelBtn);

    mainLayout->addLayout(row2);

    rebuild();
}

void TransferTaskCard::rebuild() {
    const bool isSend = (m_task.direction == TransferDirection::Send);
    const QString dirIcon = isSend ? "↑" : "↓";
    const QString dirColor = isSend ? "#07C160" : "#1E88E5";
    const QString lock = m_task.encrypted ? "  🔒" : "";
    m_titleLabel->setText(QString("<span style='color:%1; font-weight:700;'>%2</span> %3%4")
                              .arg(dirColor, dirIcon, m_task.fileName.toHtmlEscaped(), lock));

    // While a retry is pending the task sits in Queued with attempts > 1; show
    // a dedicated "retrying" badge instead of the generic "排队中".
    QString statusStr = statusText(m_task.status);
    if (m_task.attempts > 1 &&
        (m_task.status == TransferStatus::Queued || m_task.status == TransferStatus::Paused)) {
        statusStr = tr("重试中 (%1/%2)").arg(m_task.attempts).arg(m_manager ? m_manager->maxRetries() : 3);
    }
    m_statusLabel->setText(statusStr);
    QString badgeColor;
    switch (m_task.status) {
        case TransferStatus::Completed:   badgeColor = "#1E4620"; break;
        case TransferStatus::Failed:
        case TransferStatus::Cancelled:   badgeColor = "#4A1E1E"; break;
        case TransferStatus::Paused:      badgeColor = "#4A3A1E"; break;
        case TransferStatus::Transferring:badgeColor = "#15324A"; break;
        default:
            badgeColor = (m_task.attempts > 1) ? "#4A3A1E" : "#333333";
            break;
    }
    m_statusLabel->setStyleSheet(
        QString("font-size: 12px; padding: 1px 8px; border-radius: 8px; background-color: %1;").arg(badgeColor));

    const qint64 total = m_task.totalSize > 0 ? m_task.totalSize : 1;
    m_progress->setMaximum(1000);
    int pct = static_cast<int>(m_task.transferredSize * 1000 / total);
    pct = qBound(0, pct, 1000);
    m_progress->setValue(pct);

    QString eta = "--";
    if (m_task.status == TransferStatus::Transferring && m_task.speedBps > 0) {
        qint64 remain = (m_task.totalSize - m_task.transferredSize);
        if (remain > 0) eta = formatDuration(remain / m_task.speedBps);
    }
    m_infoLabel->setText(QString("%1 / %2    %3    ETA %4")
                             .arg(formatBytes(m_task.transferredSize),
                                  formatBytes(m_task.totalSize),
                                  formatSpeed(m_task.speedBps),
                                  eta));

    bool terminal = (m_task.status == TransferStatus::Completed ||
                     m_task.status == TransferStatus::Failed ||
                     m_task.status == TransferStatus::Cancelled);
    m_removeBtn->setVisible(terminal);
    m_pauseBtn->setVisible(!terminal && m_task.status == TransferStatus::Transferring);
    m_resumeBtn->setVisible(!terminal && m_task.status == TransferStatus::Paused);
    m_cancelBtn->setVisible(!terminal);
}

void TransferTaskCard::updateTask(const IPMsgTransferTask& task) {
    m_task = task;
    rebuild();
}

// ----------------------------------------------------------------------------
// TransferTaskWidget
// ----------------------------------------------------------------------------

TransferTaskWidget::TransferTaskWidget(IPMsgManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header
    auto* header = new QWidget(this);
    header->setFixedHeight(40);
    header->setStyleSheet("QWidget { background-color: #252525; border-bottom: 1px solid #333; }");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(8);

    QLabel* title = new QLabel(tr("📁 传输任务"), header);
    title->setStyleSheet("color: #07C160; font-size: 14px; font-weight: 600;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    QPushButton* clearBtn = new QPushButton(tr("清除已完成"), header);
    clearBtn->setFixedHeight(26);
    clearBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 4px; "
        "padding: 0 10px; font-size: 12px; }"
        "QPushButton:hover { background-color: #4A4A4A; }");
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, this, &TransferTaskWidget::onClearFinished);
    headerLayout->addWidget(clearBtn);

    QPushButton* closeBtn = new QPushButton(QIcon(":/icons/export.svg"), "", header);
    closeBtn->setFixedSize(28, 28);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: #3A3A3A; color: #E0E0E0; border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4A4A4A; }");
    closeBtn->setIconSize(QSize(16, 16));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, [this]() { setVisible(false); });
    headerLayout->addWidget(closeBtn);

    layout->addWidget(header);

    // Scrollable list
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setStyleSheet("QScrollArea { background-color: #1E1E1E; border: none; }");
    m_listWidget = new QWidget(m_scroll);
    auto* listLayout = new QVBoxLayout(m_listWidget);
    listLayout->setContentsMargins(12, 12, 12, 12);
    listLayout->setSpacing(10);
    listLayout->addStretch();
    m_scroll->setWidget(m_listWidget);
    layout->addWidget(m_scroll, 1);

    m_emptyLabel = new QLabel(tr("暂无传输任务"), m_listWidget);
    m_emptyLabel->setStyleSheet("color: #666; font-size: 13px; padding: 20px;");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    listLayout->insertWidget(0, m_emptyLabel);

    if (m_manager) {
        connect(m_manager, &IPMsgManager::fileTaskQueued, this, &TransferTaskWidget::onTaskQueued);
        connect(m_manager, &IPMsgManager::fileTaskStarted, this, &TransferTaskWidget::onTaskStarted);
        connect(m_manager, &IPMsgManager::fileTaskPaused, this, &TransferTaskWidget::onTaskPaused);
        connect(m_manager, &IPMsgManager::fileTaskCancelled, this, &TransferTaskWidget::onTaskCancelled);
        connect(m_manager, &IPMsgManager::fileProgress, this,
                [this](const QString& fileId, qint64 bytes, qint64 total, int speed) {
                    onProgress(fileId, bytes, total, speed);
                });
        connect(m_manager, &IPMsgManager::fileCompleted, this,
                [this](const QString& fileId, const QString& path, bool ok) {
                    onCompleted(fileId, path, ok);
                });
        connect(m_manager, &IPMsgManager::fileError, this,
                [this](const QString& fileId, const QString& err) { onError(fileId, err); });
        connect(m_manager, &IPMsgManager::fileRetryScheduled, this,
                [this](const QString& fileId, int attempt, int delayMs) {
                    onRetryScheduled(fileId, attempt, delayMs);
                });
    }

    refresh();
}

void TransferTaskWidget::ensureCard(const IPMsgTransferTask& task) {
    TransferTaskCard* card = m_cards.value(task.fileId);
    if (!card) {
        card = new TransferTaskCard(m_manager, task, m_listWidget);
        connect(card, &TransferTaskCard::requestRefresh, this, &TransferTaskWidget::refresh);
        // Insert before the trailing stretch.
        m_listWidget->layout()->removeItem(m_listWidget->layout()->itemAt(m_listWidget->layout()->count() - 1));
        static_cast<QVBoxLayout*>(m_listWidget->layout())->insertWidget(0, card);
        static_cast<QVBoxLayout*>(m_listWidget->layout())->addStretch();
        m_cards.insert(task.fileId, card);
    } else {
        card->updateTask(task);
    }
    m_emptyLabel->setVisible(m_cards.isEmpty());
}

void TransferTaskWidget::removeCard(const QString& fileId) {
    TransferTaskCard* card = m_cards.take(fileId);
    if (card) {
        card->deleteLater();
        m_emptyLabel->setVisible(m_cards.isEmpty());
    }
}

void TransferTaskWidget::refresh() {
    if (!m_manager) return;
    QList<IPMsgTransferTask> tasks = m_manager->getTransferTasks();
    QSet<QString> liveIds;
    for (const IPMsgTransferTask& t : tasks) {
        liveIds.insert(t.fileId);
        ensureCard(t);
    }
    // Surface persisted, incomplete transfers as resumable after a restart.
    // Synthesize a Paused task card so the existing "继续" button can resume it.
    for (const IPMsgTransferState& st : m_manager->getResumableTransfers()) {
        if (liveIds.contains(st.fileId)) continue;
        IPMsgTransferTask rt;
        rt.fileId = st.fileId;
        rt.direction = st.isSender ? TransferDirection::Send : TransferDirection::Receive;
        rt.fileName = QFileInfo(st.filePath).fileName();
        rt.filePath = st.filePath;
        rt.totalSize = st.totalSize;
        rt.transferredSize = st.lastOffset;
        rt.md5 = st.md5;
        rt.isDirectory = st.isDirectory;
        rt.status = TransferStatus::Paused; // reveals the "继续" action
        rt.startTime = QDateTime::currentMSecsSinceEpoch();
        rt.lastSampleTime = rt.startTime;
        ensureCard(rt);
    }
    m_emptyLabel->setVisible(m_cards.isEmpty());
}

void TransferTaskWidget::onTaskQueued(const QString& fileId) { Q_UNUSED(fileId); refresh(); }
void TransferTaskWidget::onTaskStarted(const QString& fileId) { Q_UNUSED(fileId); refresh(); }
void TransferTaskWidget::onTaskPaused(const QString& fileId) { Q_UNUSED(fileId); refresh(); }
void TransferTaskWidget::onTaskCancelled(const QString& fileId) { Q_UNUSED(fileId); refresh(); }

void TransferTaskWidget::onProgress(const QString& fileId, qint64 bytes, qint64 total, int speedBps) {
    Q_UNUSED(total);
    TransferTaskCard* card = m_cards.value(fileId);
    if (!card && m_manager) {
        refresh();
        card = m_cards.value(fileId);
    }
    if (card && m_manager) {
        QList<IPMsgTransferTask> tasks = m_manager->getTransferTasks();
        for (const IPMsgTransferTask& t : tasks) {
            if (t.fileId == fileId) {
                IPMsgTransferTask upd = t;
                upd.transferredSize = bytes;
                upd.speedBps = speedBps;
                card->updateTask(upd);
                break;
            }
        }
    }
}

void TransferTaskWidget::onCompleted(const QString& fileId, const QString& filePath, bool integrityOk) {
    Q_UNUSED(filePath);
    Q_UNUSED(integrityOk);
    refresh();
}

void TransferTaskWidget::onError(const QString& fileId, const QString& error) {
    Q_UNUSED(error);
    Q_UNUSED(fileId);
    refresh();
}

void TransferTaskWidget::onRetryScheduled(const QString& fileId, int attempt, int delayMs) {
    Q_UNUSED(fileId);
    Q_UNUSED(attempt);
    Q_UNUSED(delayMs);
    refresh();
}

void TransferTaskWidget::onClearFinished() {
    if (!m_manager) return;
    QList<IPMsgTransferTask> tasks = m_manager->getTransferTasks();
    for (const IPMsgTransferTask& t : tasks) {
        if (t.status == TransferStatus::Completed ||
            t.status == TransferStatus::Failed ||
            t.status == TransferStatus::Cancelled) {
            m_manager->removeTransfer(t.fileId);
        }
    }
    refresh();
}

} // namespace xrk
