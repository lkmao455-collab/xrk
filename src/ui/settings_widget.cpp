#include "settings_widget.h"
#include "core/translation_manager.h"
#include <QSettings>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QGroupBox>

namespace xrk {

SettingsWidget::SettingsWidget(QWidget* parent) : QDialog(parent) {
    setupUI();
    loadSettings();
}

SettingsWidget::~SettingsWidget() {
}

void SettingsWidget::loadSettings() {
    QSettings settings;
    m_deviceNameEdit->setText(settings.value("device/name", "").toString());
    m_portSpinBox->setValue(settings.value("network/port", 9999).toInt());
    m_autoDiscoveryCheckBox->setChecked(settings.value("network/auto_discovery", true).toBool());
    m_encryptionCheckBox->setChecked(settings.value("security/encryption_enabled", false).toBool());
    m_privacyScreenCheckBox->setChecked(settings.value("security/privacy_screen", false).toBool());
    m_trueColorCheckBox->setChecked(settings.value("video/true_color", false).toBool());
    m_fpsSpinBox->setValue(settings.value("performance/capture_fps", 60).toInt());
    m_relayCheckBox->setChecked(settings.value("relay/enabled", false).toBool());
    m_relayHostEdit->setText(settings.value("relay/host", "").toString());
    m_relayPortSpinBox->setValue(settings.value("relay/port", 9997).toInt());
    m_relayTokenEdit->setText(settings.value("relay/token", "").toString());

    QString currentLang = TranslationManager::instance().currentLanguage();
    int idx = m_languageCombo->findData(currentLang);
    if (idx >= 0) {
        m_languageCombo->setCurrentIndex(idx);
    }
}

void SettingsWidget::saveSettings() {
    QSettings settings;
    settings.setValue("device/name", m_deviceNameEdit->text());
    settings.setValue("network/port", m_portSpinBox->value());
    settings.setValue("network/auto_discovery", m_autoDiscoveryCheckBox->isChecked());
    settings.setValue("security/encryption_enabled", m_encryptionCheckBox->isChecked());
    settings.setValue("security/privacy_screen", m_privacyScreenCheckBox->isChecked());
    settings.setValue("video/true_color", m_trueColorCheckBox->isChecked());
    settings.setValue("performance/capture_fps", m_fpsSpinBox->value());
    settings.setValue("relay/enabled", m_relayCheckBox->isChecked());
    settings.setValue("relay/host", m_relayHostEdit->text());
    settings.setValue("relay/port", m_relayPortSpinBox->value());
    settings.setValue("relay/token", m_relayTokenEdit->text());
}

uint16_t SettingsWidget::port() const {
    return static_cast<uint16_t>(m_portSpinBox->value());
}

bool SettingsWidget::autoDiscovery() const {
    return m_autoDiscoveryCheckBox->isChecked();
}

bool SettingsWidget::encryptionEnabled() const {
    return m_encryptionCheckBox->isChecked();
}

QString SettingsWidget::deviceName() const {
    return m_deviceNameEdit->text();
}

int SettingsWidget::fps() const {
    return m_fpsSpinBox->value();
}

bool SettingsWidget::privacyScreenEnabled() const {
    return m_privacyScreenCheckBox->isChecked();
}

bool SettingsWidget::trueColorEnabled() const {
    return m_trueColorCheckBox->isChecked();
}

bool SettingsWidget::relayEnabled() const {
    return m_relayCheckBox->isChecked();
}

QString SettingsWidget::relayHost() const {
    return m_relayHostEdit->text();
}

uint16_t SettingsWidget::relayPort() const {
    return static_cast<uint16_t>(m_relayPortSpinBox->value());
}

QString SettingsWidget::relayToken() const {
    return m_relayTokenEdit->text();
}

QString SettingsWidget::selectedLanguage() const {
    return m_languageCombo->currentData().toString();
}

void SettingsWidget::onOkClicked() {
    saveSettings();
    accept();
}

void SettingsWidget::onCancelClicked() {
    reject();
}

void SettingsWidget::setupUI() {
    setWindowTitle(tr("设置"));
    setMinimumWidth(420);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Basic settings
    QFormLayout* formLayout = new QFormLayout();

    m_deviceNameEdit = new QLineEdit(this);
    formLayout->addRow(tr("设备名称:"), m_deviceNameEdit);

    m_portSpinBox = new QSpinBox(this);
    m_portSpinBox->setRange(1024, 65535);
    m_portSpinBox->setValue(9999);
    formLayout->addRow(tr("监听端口:"), m_portSpinBox);

    m_autoDiscoveryCheckBox = new QCheckBox(tr("自动发现设备"), this);
    m_autoDiscoveryCheckBox->setChecked(true);
    formLayout->addRow("", m_autoDiscoveryCheckBox);

    m_encryptionCheckBox = new QCheckBox(tr("启用加密"), this);
    formLayout->addRow("", m_encryptionCheckBox);

    m_privacyScreenCheckBox = new QCheckBox(tr("远程控制时锁屏"), this);
    formLayout->addRow("", m_privacyScreenCheckBox);

    m_trueColorCheckBox = new QCheckBox(tr("真彩 4:4:4 (更高色彩保真度)"), this);
    formLayout->addRow("", m_trueColorCheckBox);

    m_fpsSpinBox = new QSpinBox(this);
    m_fpsSpinBox->setRange(1, 240);
    m_fpsSpinBox->setValue(60);
    m_fpsSpinBox->setSuffix(" FPS");
    formLayout->addRow(tr("采集帧率:"), m_fpsSpinBox);

    // Language selector
    m_languageCombo = new QComboBox(this);
    QStringList langs = TranslationManager::instance().availableLanguages();
    for (const QString& lang : langs) {
        QLocale locale(lang);
        QString nativeName = locale.nativeLanguageName();
        QString englishName = QLocale::languageToString(locale.language());
        QString label = nativeName;
        if (nativeName.toLower() != englishName.toLower()) {
            label += " (" + englishName + ")";
        }
        m_languageCombo->addItem(label, lang);
    }
    formLayout->addRow(tr("语言:"), m_languageCombo);

    mainLayout->addLayout(formLayout);

    // Relay server settings
    QGroupBox* relayGroup = new QGroupBox(tr("中继服务器 (跨互联网连接)"), this);
    QVBoxLayout* relayLayout = new QVBoxLayout(relayGroup);

    m_relayCheckBox = new QCheckBox(tr("启用中继"), this);
    relayLayout->addWidget(m_relayCheckBox);

    QFormLayout* relayForm = new QFormLayout();
    m_relayHostEdit = new QLineEdit(this);
    m_relayHostEdit->setPlaceholderText(tr("中继服务器地址 (IP 或域名)"));
    relayForm->addRow(tr("服务器地址:"), m_relayHostEdit);

    m_relayPortSpinBox = new QSpinBox(this);
    m_relayPortSpinBox->setRange(1, 65535);
    m_relayPortSpinBox->setValue(9997);
    relayForm->addRow(tr("端口:"), m_relayPortSpinBox);

    m_relayTokenEdit = new QLineEdit(this);
    m_relayTokenEdit->setPlaceholderText(tr("中继密钥 (与中继服务器一致)"));
    m_relayTokenEdit->setEchoMode(QLineEdit::Password);
    relayForm->addRow(tr("密钥:"), m_relayTokenEdit);

    relayLayout->addLayout(relayForm);
    mainLayout->addWidget(relayGroup);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_okButton = new QPushButton(tr("确定"), this);
    connect(m_okButton, &QPushButton::clicked, this, &SettingsWidget::onOkClicked);
    buttonLayout->addWidget(m_okButton);

    m_cancelButton = new QPushButton(tr("取消"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, &SettingsWidget::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(buttonLayout);
}

} // namespace xrk
