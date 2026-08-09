#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "core/types.h"

namespace xrk {

class RemoteController;

class RemoteProcessWidget : public QWidget {
    Q_OBJECT
public:
    explicit RemoteProcessWidget(QWidget* parent = nullptr);
    ~RemoteProcessWidget();

    void setRemoteController(RemoteController* controller);
    void setConnected(bool connected);

public slots:
    void refreshList(const ProcessListResponse& response);
    void onKillClicked();
    void onStartClicked();

private:
    void setupUI();
    void applyDarkTheme();
    static QString formatBytes(qint64 bytes);

    RemoteController* m_controller = nullptr;
    QTimer* m_refreshTimer = nullptr;

    QTableWidget* m_table = nullptr;
    QLineEdit* m_startEdit = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_killButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QLabel* m_statusLabel = nullptr;
};

} // namespace xrk
