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

namespace xrk {

class AuditLogger;

class AuditLogViewer : public QDialog {
    Q_OBJECT
public:
    explicit AuditLogViewer(AuditLogger* logger, QWidget* parent = nullptr);
    ~AuditLogViewer();

    void refreshLogs();

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
