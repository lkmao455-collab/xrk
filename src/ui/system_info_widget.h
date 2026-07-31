#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QTimer>
#include "core/types.h"

namespace xrk {

class RemoteController;

class SystemInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit SystemInfoWidget(QWidget* parent = nullptr);
    ~SystemInfoWidget();

    void setRemoteController(RemoteController* controller);
    void updateInfo(const SysInfo& info);
    void setConnected(bool connected);

private:
    void setupUI();
    QHBoxLayout* createMetricLayout(QLabel*& valueLabel, QProgressBar*& progressBar);

    RemoteController* m_controller = nullptr;
    QTimer* m_refreshTimer = nullptr;

    // CPU
    QLabel* m_cpuValue = nullptr;
    QProgressBar* m_cpuBar = nullptr;
    QLabel* m_cpuNameLabel = nullptr;

    // Memory
    QLabel* m_memValue = nullptr;
    QProgressBar* m_memBar = nullptr;
    QLabel* m_memDetail = nullptr;

    // Disk
    QLabel* m_diskValue = nullptr;
    QProgressBar* m_diskBar = nullptr;
    QLabel* m_diskDetail = nullptr;

    // System
    QLabel* m_osLabel = nullptr;
    QLabel* m_uptimeLabel = nullptr;
    QLabel* m_processLabel = nullptr;
    QLabel* m_networkLabel = nullptr;
};

} // namespace xrk
