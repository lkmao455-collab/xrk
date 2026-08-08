#pragma once

#include <QWidget>
#include <QFrame>
#include <QScrollArea>
#include <QMap>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include "app/ipmsg_manager.h"

namespace xrk {

// Forward declarations
class IPMsgManager;

// A single transfer-task card shown inside TransferTaskWidget.
// It owns its own action buttons and talks directly to the manager.
class TransferTaskCard : public QFrame {
    Q_OBJECT
public:
    explicit TransferTaskCard(IPMsgManager* manager, const IPMsgTransferTask& task, QWidget* parent = nullptr);

    // Refresh the card from the latest task snapshot.
    void updateTask(const IPMsgTransferTask& task);

signals:
    void requestRefresh();

private:
    void rebuild();
    static QString formatBytes(qint64 bytes);
    static QString formatSpeed(int bps);
    static QString formatDuration(qint64 seconds);
    static QString statusText(TransferStatus status);

    IPMsgManager* m_manager = nullptr;
    QString m_fileId;
    IPMsgTransferTask m_task;

    QLabel* m_titleLabel = nullptr;     // direction icon + filename + lock
    QLabel* m_statusLabel = nullptr;    // status badge text
    QProgressBar* m_progress = nullptr;
    QLabel* m_infoLabel = nullptr;       // size / speed / ETA
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_resumeBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
};

// Right-side slide-in panel that lists all live + historical IPMsg file
// transfers and lets the user pause / resume / cancel / clear them.
class TransferTaskWidget : public QWidget {
    Q_OBJECT
public:
    explicit TransferTaskWidget(IPMsgManager* manager, QWidget* parent = nullptr);

    // Pull the full task list from the manager and rebuild/refresh cards.
    void refresh();

public slots:
    void onTaskQueued(const QString& fileId);
    void onTaskStarted(const QString& fileId);
    void onTaskPaused(const QString& fileId);
    void onTaskCancelled(const QString& fileId);
    void onProgress(const QString& fileId, qint64 bytes, qint64 total, int speedBps);
    void onCompleted(const QString& fileId, const QString& filePath, bool integrityOk);
    void onError(const QString& fileId, const QString& error);
    void onRetryScheduled(const QString& fileId, int attempt, int delayMs);

private slots:
    void onClearFinished();

private:
    void ensureCard(const IPMsgTransferTask& task);
    void removeCard(const QString& fileId);
    static QString statusText(TransferStatus status);

    IPMsgManager* m_manager = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_listWidget = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QMap<QString, TransferTaskCard*> m_cards;
};

} // namespace xrk
