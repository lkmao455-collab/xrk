#include "permission_console_dialog.h"
#include "app/remote_controller.h"
#include "audit_log_viewer.h"
#include "core/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDateTime>
#include <QGroupBox>

namespace xrk {

namespace {
// Display order + Chinese labels for the 15 capability bits.
struct CapEntry { Capability cap; const char* label; };
const CapEntry kCaps[] = {
    {Capability::ViewScreen,    "查看屏幕"},
    {Capability::ControlInput,  "控制键鼠"},
    {Capability::Clipboard,     "剪贴板"},
    {Capability::FileRead,      "文件读取"},
    {Capability::FileWrite,     "文件写入"},
    {Capability::ProcessView,   "进程查看"},
    {Capability::ProcessManage, "进程管理"},
    {Capability::Terminal,      "终端"},
    {Capability::SysInfo,       "系统信息"},
    {Capability::PowerControl,  "电源控制"},
    {Capability::Calls,         "语音通话"},
    {Capability::Chat,          "聊天"},
    {Capability::Annotation,    "标注"},
    {Capability::Record,        "录制"},
    {Capability::UserManage,    "用户管理"},
};

// Prompt for user details. Returns false if cancelled. When `editing` is true the
// username field is read-only and the password may be left blank to keep it.
bool promptUser(QWidget* parent, bool editing, QString& username,
                QString& password, int& level, bool& enabled) {
    QDialog dlg(parent);
    dlg.setWindowTitle(editing ? QObject::tr("编辑用户") : QObject::tr("新增用户"));
    auto* form = new QFormLayout(&dlg);

    auto* userEdit = new QLineEdit(username, &dlg);
    userEdit->setReadOnly(editing);
    form->addRow(QObject::tr("用户名:"), userEdit);

    auto* pwdEdit = new QLineEdit(&dlg);
    pwdEdit->setEchoMode(QLineEdit::Password);
    pwdEdit->setPlaceholderText(editing ? QObject::tr("留空则不修改密码") : QString());
    form->addRow(QObject::tr("密码:"), pwdEdit);

    auto* levelCombo = new QComboBox(&dlg);
    levelCombo->addItem(QObject::tr("只读 (Viewer)"), 1);
    levelCombo->addItem(QObject::tr("操作员 (Operator)"), 2);
    levelCombo->addItem(QObject::tr("管理员 (Admin)"), 3);
    int idx = levelCombo->findData(level);
    levelCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    form->addRow(QObject::tr("级别:"), levelCombo);

    auto* enabledCheck = new QCheckBox(QObject::tr("启用"), &dlg);
    enabledCheck->setChecked(enabled);
    form->addRow(QString(), enabledCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return false;
    if (!editing && userEdit->text().trimmed().isEmpty()) return false;

    username = userEdit->text().trimmed();
    password = pwdEdit->text();
    level = levelCombo->currentData().toInt();
    enabled = enabledCheck->isChecked();
    return true;
}
} // namespace

PermissionConsoleDialog::PermissionConsoleDialog(RemoteController* controller, QWidget* parent)
    : QDialog(parent), m_controller(controller) {
    setWindowTitle(tr("用户与权限管理"));
    resize(640, 480);
    setupUI();
    applyDarkTheme();

    if (m_controller) {
        connect(m_controller, &RemoteController::userListReceived,
                this, &PermissionConsoleDialog::onUserListReceived);
        connect(m_controller, &RemoteController::capabilityTogglesReceived,
                this, &PermissionConsoleDialog::onCapabilityTogglesReceived);
        connect(m_controller, &RemoteController::devicePermissionsReceived,
                this, &PermissionConsoleDialog::onDevicePermissionsReceived);
        connect(m_controller, &RemoteController::permissionDenied,
                this, &PermissionConsoleDialog::onPermissionDenied);
        connect(m_controller, &RemoteController::auditLogReceived,
                this, &PermissionConsoleDialog::onAuditLogReceived);
        connect(m_controller, &RemoteController::tempGrantReceived,
                this, &PermissionConsoleDialog::onTempGrantReceived);
    }
}

PermissionConsoleDialog::~PermissionConsoleDialog() = default;

QString PermissionConsoleDialog::levelName(int level) {
    switch (level) {
        case 0: return tr("无");
        case 1: return tr("只读");
        case 2: return tr("操作员");
        case 3: return tr("管理员");
        default: return tr("未知");
    }
}

void PermissionConsoleDialog::setupUI() {
    auto* root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildUsersTab(), tr("用户"));
    m_tabs->addTab(buildTogglesTab(), tr("能力开关"));
    m_tabs->addTab(buildDevicesTab(), tr("设备覆盖"));
    root->addWidget(m_tabs);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("perm-status");
    root->addWidget(m_statusLabel);

    m_auditLogBtn = new QPushButton(tr("审计日志"), this);
    connect(m_auditLogBtn, &QPushButton::clicked, this, &PermissionConsoleDialog::onShowAuditLog);

    auto* closeBtn = new QPushButton(tr("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    auto* bottom = new QHBoxLayout();
    bottom->addWidget(m_auditLogBtn);
    bottom->addStretch(1);
    bottom->addWidget(closeBtn);
    root->addLayout(bottom);
}

QWidget* PermissionConsoleDialog::buildUsersTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    m_userTable = new QTableWidget(0, 4, page);
    m_userTable->setHorizontalHeaderLabels({tr("用户名"), tr("级别"), tr("启用"), tr("最后登录")});
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_userTable);

    auto* btnRow = new QHBoxLayout();
    m_addUserBtn = new QPushButton(tr("新增"), page);
    m_editUserBtn = new QPushButton(tr("编辑"), page);
    m_removeUserBtn = new QPushButton(tr("删除"), page);
    connect(m_addUserBtn, &QPushButton::clicked, this, &PermissionConsoleDialog::onAddUser);
    connect(m_editUserBtn, &QPushButton::clicked, this, &PermissionConsoleDialog::onEditUser);
    connect(m_removeUserBtn, &QPushButton::clicked, this, &PermissionConsoleDialog::onRemoveUser);
    btnRow->addWidget(m_addUserBtn);
    btnRow->addWidget(m_editUserBtn);
    btnRow->addWidget(m_removeUserBtn);
    btnRow->addStretch(1);
    layout->addLayout(btnRow);

    return page;
}

QWidget* PermissionConsoleDialog::buildTogglesTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* hint = new QLabel(tr("关闭某项能力将对所有会话生效（用户管理不可关闭）。"), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* grid = new QGridLayout();
    int row = 0, col = 0;
    for (const CapEntry& e : kCaps) {
        auto* cb = new QCheckBox(tr(e.label), page);
        if (e.cap == Capability::UserManage) {
            cb->setChecked(true);
            cb->setEnabled(false);   // self-lock protection: never switchable off
            cb->setToolTip(tr("用户管理能力不可被开关关闭"));
        }
        connect(cb, &QCheckBox::toggled, this, [this, cap = e.cap](bool checked) {
            if (m_applyingToggles) return;   // ignore programmatic updates
            if (m_controller) m_controller->setCapabilityToggle(cap, checked);
        });
        m_toggleChecks.insert(e.cap, cb);
        grid->addWidget(cb, row, col);
        if (++col >= 3) { col = 0; ++row; }
    }
    layout->addLayout(grid);
    layout->addStretch(1);
    return page;
}

QWidget* PermissionConsoleDialog::buildDevicesTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    m_deviceTable = new QTableWidget(0, 4, page);
    m_deviceTable->setHorizontalHeaderLabels({tr("设备/IP"), tr("级别"), tr("能力掩码"), tr("备注")});
    m_deviceTable->horizontalHeader()->setStretchLastSection(true);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_deviceTable);

    auto* btnRow = new QHBoxLayout();
    m_setDeviceBtn = new QPushButton(tr("设置覆盖"), page);
    m_clearDeviceBtn = new QPushButton(tr("清除覆盖"), page);
    m_tempGrantBtn = new QPushButton(tr("临时授权"), page);
    m_tempGrantBtn->setToolTip(tr("在指定时长内临时提升该设备的权限，到期后自动回退"));
    connect(m_setDeviceBtn, &QPushButton::clicked, this, &PermissionConsoleDialog::onSetDevicePerm);
    connect(m_clearDeviceBtn, &QPushButton::clicked, this, &PermissionConsoleDialog::onClearDevicePerm);
    connect(m_tempGrantBtn, &QPushButton::clicked, this, &PermissionConsoleDialog::onSetTempGrant);
    btnRow->addWidget(m_setDeviceBtn);
    btnRow->addWidget(m_clearDeviceBtn);
    btnRow->addWidget(m_tempGrantBtn);
    btnRow->addStretch(1);
    layout->addLayout(btnRow);

    return page;
}

void PermissionConsoleDialog::refreshAll() {
    if (!m_controller) return;
    m_controller->requestUserList();
    m_controller->requestDevicePermissions();
    // Capability toggles arrive with the granted caps on auth; the host also
    // echoes them after any toggle change. A user-list refresh is enough to
    // confirm the session is admin; toggles repopulate on first change. To show
    // the current mask immediately, seed from the granted caps.
    onCapabilityTogglesReceived(m_controller->grantedCapabilities());
}

void PermissionConsoleDialog::onUserListReceived(const QList<UserRecord>& users) {
    m_userTable->setRowCount(users.size());
    for (int i = 0; i < users.size(); ++i) {
        const UserRecord& u = users[i];
        m_userTable->setItem(i, 0, new QTableWidgetItem(u.username));
        m_userTable->setItem(i, 1, new QTableWidgetItem(levelName(u.level)));
        m_userTable->setItem(i, 2, new QTableWidgetItem(u.enabled ? tr("是") : tr("否")));
        QString last = u.lastLogin > 0
            ? QDateTime::fromSecsSinceEpoch(u.lastLogin).toString("yyyy-MM-dd HH:mm")
            : tr("从未");
        m_userTable->setItem(i, 3, new QTableWidgetItem(last));
    }
    m_statusLabel->setText(tr("共 %1 个用户").arg(users.size()));
}

void PermissionConsoleDialog::onCapabilityTogglesReceived(quint32 toggles) {
    m_currentToggles = toggles;
    m_applyingToggles = true;
    for (auto it = m_toggleChecks.begin(); it != m_toggleChecks.end(); ++it) {
        if (it.key() == Capability::UserManage) continue;   // stays checked+disabled
        it.value()->setChecked(PermissionModel::hasCapability(toggles, it.key()));
    }
    m_applyingToggles = false;
}

void PermissionConsoleDialog::onDevicePermissionsReceived(const QList<DevicePermission>& devices) {
    m_deviceTable->setRowCount(devices.size());
    for (int i = 0; i < devices.size(); ++i) {
        const DevicePermission& d = devices[i];
        m_deviceTable->setItem(i, 0, new QTableWidgetItem(d.deviceId));
        m_deviceTable->setItem(i, 1, new QTableWidgetItem(levelName(d.level)));
        QString mask = d.capMask < 0 ? tr("角色默认")
                                     : ("0x" + QString::number(static_cast<uint32_t>(d.capMask), 16));
        m_deviceTable->setItem(i, 2, new QTableWidgetItem(mask));
        m_deviceTable->setItem(i, 3, new QTableWidgetItem(d.note));
    }
}

void PermissionConsoleDialog::onPermissionDenied(int capability, const QString& reason) {
    Capability cap = static_cast<Capability>(static_cast<uint32_t>(capability));
    QString capName = QString::fromUtf8(PermissionModel::capabilityName(cap));
    m_statusLabel->setText(tr("操作被拒绝 (%1): %2").arg(capName, reason));
}

void PermissionConsoleDialog::onShowAuditLog() {
    if (!m_controller) return;
    m_statusLabel->setText(tr("正在拉取主机审计日志..."));
    m_controller->requestAuditLog();
}

void PermissionConsoleDialog::onAuditLogReceived(const QJsonArray& entries) {
    m_statusLabel->setText(tr("审计日志: %1 条记录").arg(entries.size()));
    if (!m_auditViewer) {
        m_auditViewer = new AuditLogViewer(nullptr, this);
        m_auditViewer->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_auditViewer->setEntries(entries);
    m_auditViewer->show();
    m_auditViewer->raise();
    m_auditViewer->activateWindow();
}

void PermissionConsoleDialog::onAddUser() {
    QString username, password;
    int level = 1;
    bool enabled = true;
    if (!promptUser(this, false, username, password, level, enabled)) return;
    if (m_controller) m_controller->addUser(username, password, level, enabled);
}

void PermissionConsoleDialog::onEditUser() {
    int row = m_userTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("提示"), tr("请先选择一个用户"));
        return;
    }
    QString username = m_userTable->item(row, 0)->text();
    QString password;
    // Map the shown level name back to an ordinal.
    QString shown = m_userTable->item(row, 1)->text();
    int level = 1;
    for (int l = 0; l <= 3; ++l) { if (levelName(l) == shown) { level = l; break; } }
    bool enabled = (m_userTable->item(row, 2)->text() == tr("是"));

    if (!promptUser(this, true, username, password, level, enabled)) return;

    uint8_t fields = UserMutation::FieldLevel | UserMutation::FieldEnabled;
    if (!password.isEmpty()) fields |= UserMutation::FieldPassword;
    if (m_controller) m_controller->updateUser(username, fields, password, level, enabled);
}

void PermissionConsoleDialog::onRemoveUser() {
    int row = m_userTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("提示"), tr("请先选择一个用户"));
        return;
    }
    QString username = m_userTable->item(row, 0)->text();
    if (QMessageBox::question(this, tr("确认删除"),
            tr("确定删除用户 \"%1\"?").arg(username)) != QMessageBox::Yes) {
        return;
    }
    if (m_controller) m_controller->removeUser(username);
}

void PermissionConsoleDialog::onSetDevicePerm() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("设置设备覆盖"));
    auto* form = new QFormLayout(&dlg);

    QString presetId;
    int row = m_deviceTable->currentRow();
    if (row >= 0) presetId = m_deviceTable->item(row, 0)->text();
    auto* idEdit = new QLineEdit(presetId, &dlg);
    idEdit->setPlaceholderText(tr("设备 IP，例如 192.168.1.20"));
    form->addRow(tr("设备/IP:"), idEdit);

    auto* levelCombo = new QComboBox(&dlg);
    levelCombo->addItem(tr("只读 (Viewer)"), 1);
    levelCombo->addItem(tr("操作员 (Operator)"), 2);
    levelCombo->addItem(tr("管理员 (Admin)"), 3);
    form->addRow(tr("级别:"), levelCombo);

    auto* noteEdit = new QLineEdit(&dlg);
    form->addRow(tr("备注:"), noteEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    QString deviceId = idEdit->text().trimmed();
    if (deviceId.isEmpty()) return;

    DevicePermission perm;
    perm.deviceId = deviceId;
    perm.level = levelCombo->currentData().toInt();
    perm.capMask = -1;   // use role default for the chosen level
    perm.note = noteEdit->text().trimmed();
    if (m_controller) m_controller->setDevicePermission(perm);
}

void PermissionConsoleDialog::onClearDevicePerm() {
    int row = m_deviceTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("提示"), tr("请先选择一个设备"));
        return;
    }
    QString deviceId = m_deviceTable->item(row, 0)->text();
    if (m_controller) m_controller->clearDevicePermission(deviceId);
}

void PermissionConsoleDialog::onSetTempGrant() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("临时授权"));
    auto* form = new QFormLayout(&dlg);

    QString presetId;
    int row = m_deviceTable->currentRow();
    if (row >= 0 && m_deviceTable->item(row, 0)) presetId = m_deviceTable->item(row, 0)->text();
    auto* idEdit = new QLineEdit(presetId, &dlg);
    idEdit->setPlaceholderText(tr("设备 IP，例如 192.168.1.20"));
    form->addRow(tr("设备/IP:"), idEdit);

    auto* levelCombo = new QComboBox(&dlg);
    levelCombo->addItem(tr("只读 (Viewer)"), 1);
    levelCombo->addItem(tr("操作员 (Operator)"), 2);
    levelCombo->addItem(tr("管理员 (Admin)"), 3);
    levelCombo->setCurrentIndex(1);
    form->addRow(tr("临时级别:"), levelCombo);

    auto* minutesSpin = new QSpinBox(&dlg);
    minutesSpin->setRange(0, 24 * 60);
    minutesSpin->setValue(30);
    minutesSpin->setSuffix(tr(" 分钟"));
    minutesSpin->setSpecialValueText(tr("撤销临时授权"));
    form->addRow(tr("有效时长:"), minutesSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    QString deviceId = idEdit->text().trimmed();
    if (deviceId.isEmpty() || !m_controller) return;

    int minutes = minutesSpin->value();
    if (minutes <= 0) {
        // 0 minutes means "revoke": the host drops the grant when expiresAt < 0.
        m_controller->clearTempGrant(deviceId);
        if (m_statusLabel) m_statusLabel->setText(tr("已请求撤销 %1 的临时授权").arg(deviceId));
        return;
    }

    TemporaryGrant grant;
    grant.deviceId = deviceId;
    grant.level = levelCombo->currentData().toInt();
    grant.capMask = -1;   // role default for the chosen level
    grant.expiresAt = QDateTime::currentMSecsSinceEpoch() + qint64(minutes) * 60000LL;
    m_controller->requestTempGrant(grant);
    if (m_statusLabel) {
        m_statusLabel->setText(tr("已请求为 %1 授予 %2 分钟的 %3 权限")
                                   .arg(deviceId).arg(minutes).arg(levelName(grant.level)));
    }
}

void PermissionConsoleDialog::onTempGrantReceived(const TemporaryGrant& grant) {
    if (!m_statusLabel) return;
    if (grant.expiresAt <= 0) {
        m_statusLabel->setText(tr("临时授权已撤销: %1").arg(grant.deviceId));
    } else {
        QString until = QDateTime::fromMSecsSinceEpoch(grant.expiresAt).toString("HH:mm:ss");
        m_statusLabel->setText(tr("临时授权生效: %1 → %2，有效期至 %3")
                                   .arg(grant.deviceId, levelName(grant.level), until));
    }
    // The host recomputes effective permissions, so re-pull the override table.
    if (m_controller) m_controller->requestDevicePermissions();
}

void PermissionConsoleDialog::applyDarkTheme() {
    setStyleSheet(
        "QDialog{background:#1e1f22;color:#e0e0e0;}"
        "QTabWidget::pane{border:1px solid #333;}"
        "QTableWidget{background:#26272b;color:#e0e0e0;gridline-color:#333;}"
        "QHeaderView::section{background:#2f3035;color:#ccc;padding:4px;border:none;}"
        "QPushButton{background:#3a3d44;color:#e0e0e0;border:1px solid #4a4d55;"
        "padding:5px 12px;border-radius:4px;}"
        "QPushButton:hover{background:#454951;}"
        "QCheckBox{color:#e0e0e0;padding:3px;}"
        "QLabel#perm-status{color:#9aa0a6;padding:4px 0;}");
}

} // namespace xrk
