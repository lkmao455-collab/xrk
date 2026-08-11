#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QJsonArray>
#include <QPointer>
#include "core/types.h"
#include "core/permission_model.h"

namespace xrk {

class RemoteController;
class AuditLogViewer;

// v1.8.0 RBAC admin console. Drives the host's user/permission management
// messages (210-218) over an authenticated session that holds UserManage.
// The host is the source of truth: every mutation is sent to the host and the
// tables are repopulated from the host's response rather than edited locally.
class PermissionConsoleDialog : public QDialog {
    Q_OBJECT
public:
    explicit PermissionConsoleDialog(RemoteController* controller, QWidget* parent = nullptr);
    ~PermissionConsoleDialog();

    // Ask the host for the current users / toggles / device overrides. Called on
    // open so the dialog always reflects live host state.
    void refreshAll();

private slots:
    // Host responses
    void onUserListReceived(const QList<UserRecord>& users);
    void onCapabilityTogglesReceived(quint32 toggles);
    void onDevicePermissionsReceived(const QList<DevicePermission>& devices);
    void onPermissionDenied(int capability, const QString& reason);
    void onAuditLogReceived(const QJsonArray& entries);

    // Audit log button
    void onShowAuditLog();

    // User tab actions
    void onAddUser();
    void onRemoveUser();
    void onEditUser();

    // Device tab actions
    void onSetDevicePerm();
    void onClearDevicePerm();
    void onSetTempGrant();
    void onTempGrantReceived(const TemporaryGrant& grant);

private:
    void setupUI();
    QWidget* buildUsersTab();
    QWidget* buildTogglesTab();
    QWidget* buildDevicesTab();
    void applyDarkTheme();
    static QString levelName(int level);

    RemoteController* m_controller = nullptr;

    QTabWidget* m_tabs = nullptr;

    // Users tab
    QTableWidget* m_userTable = nullptr;
    QPushButton* m_addUserBtn = nullptr;
    QPushButton* m_removeUserBtn = nullptr;
    QPushButton* m_editUserBtn = nullptr;

    // Capability toggles tab. One checkbox per capability bit; UserManage is
    // shown checked and disabled (host can never switch it off).
    QMap<Capability, QCheckBox*> m_toggleChecks;
    quint32 m_currentToggles = 0;
    bool m_applyingToggles = false;   // guard so programmatic setChecked doesn't resend

    // Device overrides tab
    QTableWidget* m_deviceTable = nullptr;
    QPushButton* m_setDeviceBtn = nullptr;
    QPushButton* m_clearDeviceBtn = nullptr;
    QPushButton* m_tempGrantBtn = nullptr;

    QLabel* m_statusLabel = nullptr;
    QPushButton* m_auditLogBtn = nullptr;
    QPointer<AuditLogViewer> m_auditViewer;
};

} // namespace xrk
