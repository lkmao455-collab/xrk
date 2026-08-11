#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QJsonArray>

namespace xrk {

class AuditLogger;

class AuditLogViewer : public QDialog {
    Q_OBJECT
public:
    explicit AuditLogViewer(AuditLogger* logger, QWidget* parent = nullptr);
    ~AuditLogViewer();

    void refreshLogs();

    // Snapshot mode: display a fixed set of entries (e.g. a host audit log
    // fetched over the network) instead of a live AuditLogger. Used by the
    // permission console's "审计日志" button.
    void setEntries(const QJsonArray& entries);

private slots:
    void onFilterChanged();
    void onExportClicked();
    void onClearClicked();
    void onEntryAdded(const QJsonObject& entry);

private:
    void setupUI();
    void populateTable(const QJsonArray& entries);
    void applyDarkTheme();

    AuditLogger* m_logger = nullptr;
    QJsonArray m_snapshot;       // fixed entries for snapshot mode (no live logger)
    QTableWidget* m_table = nullptr;
    QComboBox* m_typeFilter = nullptr;
    QDateTimeEdit* m_fromDate = nullptr;
    QDateTimeEdit* m_toDate = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_countLabel = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
};

} // namespace xrk
