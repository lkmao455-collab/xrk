#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHostAddress>

namespace xrk {

class Host;

class DeviceTrustWidget : public QDialog {
    Q_OBJECT
public:
    explicit DeviceTrustWidget(Host* host, QWidget* parent = nullptr);
    ~DeviceTrustWidget();

    void refreshTrustedList();

private slots:
    void onAddTrustedIp();
    void onRemoveTrustedIp();
    void onClearAll();

private:
    void setupUI();
    void applyDarkTheme();

    Host* m_host = nullptr;
    QTableWidget* m_table = nullptr;
    QLineEdit* m_ipInput = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QLabel* m_countLabel = nullptr;
};

} // namespace xrk
