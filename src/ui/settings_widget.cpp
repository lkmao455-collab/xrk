#include "settings_widget.h"
#include "core/translation_manager.h"
#include "core/theme_manager.h"
#include <QSettings>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QLabel>
#include <QCoreApplication>

namespace xrk {

SettingsWidget::SettingsWidget(QWidget* parent) : QDialog(parent) {
    setupUI();
    loadSettings();
}

SettingsWidget::~SettingsWidget() {
}

void SettingsWidget::loadSettings() {
    QSettings settings;

    // General
    m_deviceNameEdit->setText(settings.value("device/name", "").toString());
    m_portSpinBox->setValue(settings.value("network/port", 9999).toInt());
    m_autoDiscoveryCheckBox->setChecked(settings.value("network/auto_discovery", true).toBool());

    QString currentLang = TranslationManager::instance().currentLanguage();
    int idx = m_languageCombo->findData(currentLang);
    if (idx >= 0) {
        m_languageCombo->setCurrentIndex(idx);
    }

    // Security
    m_encryptionCheckBox->setChecked(settings.value("security/encryption_enabled", false).toBool());
    m_privacyScreenCheckBox->setChecked(settings.value("security/privacy_screen", false).toBool());
    m_autoGrantConsentCheckBox->setChecked(settings.value("security/auto_grant_consent", false).toBool());

    // Video
    m_trueColorCheckBox->setChecked(settings.value("video/true_color", false).toBool());
    m_fpsSpinBox->setValue(settings.value("performance/capture_fps", 60).toInt());

    // Network
    m_scanTimeoutSpinBox->setValue(settings.value("scan/timeout_ms", 5000).toInt() / 1000);
    m_relayCheckBox->setChecked(settings.value("relay/enabled", false).toBool());
    m_relayHostEdit->setText(settings.value("relay/host", "").toString());
    m_relayPortSpinBox->setValue(settings.value("relay/port", 9997).toInt());
    m_relayTokenEdit->setText(settings.value("relay/token", "").toString());

    // Appearance
    QString currentTheme = ThemeManager::instance().currentThemeId();
    int themeIdx = m_themeCombo->findData(currentTheme);
    if (themeIdx >= 0) {
        m_themeCombo->setCurrentIndex(themeIdx);
    }

    QString customBg = ThemeManager::instance().customBackgroundPath();
    if (!customBg.isEmpty()) {
        m_customBgEdit->setText(customBg);
    }
}

void SettingsWidget::saveSettings() {
    QSettings settings;

    // General
    settings.setValue("device/name", m_deviceNameEdit->text());
    settings.setValue("network/port", m_portSpinBox->value());
    settings.setValue("network/auto_discovery", m_autoDiscoveryCheckBox->isChecked());

    // Security
    settings.setValue("security/encryption_enabled", m_encryptionCheckBox->isChecked());
    settings.setValue("security/privacy_screen", m_privacyScreenCheckBox->isChecked());
    settings.setValue("security/auto_grant_consent", m_autoGrantConsentCheckBox->isChecked());

    // Video
    settings.setValue("video/true_color", m_trueColorCheckBox->isChecked());
    settings.setValue("performance/capture_fps", m_fpsSpinBox->value());

    // Network
    settings.setValue("scan/timeout_ms", m_scanTimeoutSpinBox->value() * 1000);
    settings.setValue("relay/enabled", m_relayCheckBox->isChecked());
    settings.setValue("relay/host", m_relayHostEdit->text());
    settings.setValue("relay/port", m_relayPortSpinBox->value());
    settings.setValue("relay/token", m_relayTokenEdit->text());

    // Appearance
    QString themeId = m_themeCombo->currentData().toString();
    ThemeManager::instance().applyTheme(themeId);
    settings.setValue("theme/current", themeId);

    QString customBgPath = m_customBgEdit->text().trimmed();
    if (!customBgPath.isEmpty()) {
        ThemeManager::instance().setCustomBackground(customBgPath);
    } else {
        ThemeManager::instance().clearCustomBackground();
    }
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

bool SettingsWidget::autoGrantConsentEnabled() const {
    return m_autoGrantConsentCheckBox->isChecked();
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

int SettingsWidget::scanTimeoutMs() const {
    return m_scanTimeoutSpinBox->value() * 1000;
}

void SettingsWidget::onOkClicked() {
    saveSettings();
    accept();
}

void SettingsWidget::onCancelClicked() {
    reject();
}

void SettingsWidget::onBrowseBackground() {
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("选择背景图片"),
        QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.gif)"));

    if (!filePath.isEmpty()) {
        m_customBgEdit->setText(filePath);
    }
}

void SettingsWidget::onClearBackground() {
    m_customBgEdit->clear();
    ThemeManager::instance().clearCustomBackground();
}

void SettingsWidget::setupUI() {
    setWindowTitle(tr("设置"));
    setMinimumSize(480, 420);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_tabWidget = new QTabWidget(this);

    // ==================== Tab 1: General ====================
    QWidget* generalTab = new QWidget();
    QFormLayout* generalLayout = new QFormLayout(generalTab);
    generalLayout->setSpacing(12);
    generalLayout->setContentsMargins(16, 16, 16, 16);

    m_deviceNameEdit = new QLineEdit(generalTab);
    m_deviceNameEdit->setPlaceholderText(tr("留空使用计算机名"));
    generalLayout->addRow(tr("设备名称:"), m_deviceNameEdit);

    m_portSpinBox = new QSpinBox(generalTab);
    m_portSpinBox->setRange(1024, 65535);
    m_portSpinBox->setValue(9999);
    generalLayout->addRow(tr("监听端口:"), m_portSpinBox);

    m_autoDiscoveryCheckBox = new QCheckBox(tr("自动发现设备"), generalTab);
    m_autoDiscoveryCheckBox->setChecked(true);
    generalLayout->addRow("", m_autoDiscoveryCheckBox);

    m_languageCombo = new QComboBox(generalTab);
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
    generalLayout->addRow(tr("语言:"), m_languageCombo);

    m_startWithWindowsCheckBox = new QCheckBox(tr("开机自启动"), generalTab);
    generalLayout->addRow("", m_startWithWindowsCheckBox);

    // Read current registry state
    QSettings autoStart("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                       QSettings::NativeFormat);
    QString appName = QCoreApplication::applicationName();
    m_startWithWindowsCheckBox->setChecked(autoStart.contains(appName));

    // Connect to actually write registry when toggled
    connect(m_startWithWindowsCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings autoStart("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                           QSettings::NativeFormat);
        QString appName = QCoreApplication::applicationName();
        if (checked) {
            autoStart.setValue(appName, QCoreApplication::applicationFilePath().replace('/', '\\'));
        } else {
            autoStart.remove(appName);
        }
    });

    m_minimizeToTrayCheckBox = new QCheckBox(tr("关闭时最小化到托盘"), generalTab);
    m_minimizeToTrayCheckBox->setChecked(true);
    generalLayout->addRow("", m_minimizeToTrayCheckBox);

    m_tabWidget->addTab(generalTab, tr("基本"));

    // ==================== Tab 2: Security ====================
    QWidget* securityTab = new QWidget();
    QFormLayout* securityLayout = new QFormLayout(securityTab);
    securityLayout->setSpacing(12);
    securityLayout->setContentsMargins(16, 16, 16, 16);

    m_encryptionCheckBox = new QCheckBox(tr("启用加密传输"), securityTab);
    securityLayout->addRow("", m_encryptionCheckBox);

    m_privacyScreenCheckBox = new QCheckBox(tr("远程控制时锁定主机屏幕"), securityTab);
    securityLayout->addRow("", m_privacyScreenCheckBox);

    m_autoGrantConsentCheckBox = new QCheckBox(tr("自动允许受信任连接 (无需确认)"), securityTab);
    securityLayout->addRow("", m_autoGrantConsentCheckBox);

    // Password strength indicator
    m_passwordStrengthBar = new QProgressBar(securityTab);
    m_passwordStrengthBar->setRange(0, 100);
    m_passwordStrengthBar->setValue(0);
    m_passwordStrengthBar->setFormat(tr("密码强度: %p%"));
    m_passwordStrengthBar->setFixedHeight(20);
    securityLayout->addRow(tr("访问密码强度:"), m_passwordStrengthBar);

    m_notificationSoundCheckBox = new QCheckBox(tr("启用消息通知音"), securityTab);
    m_notificationSoundCheckBox->setChecked(true);
    securityLayout->addRow("", m_notificationSoundCheckBox);

    securityLayout->addRow(new QLabel("", securityTab));

    m_tabWidget->addTab(securityTab, tr("安全"));

    // ==================== Tab 3: Video ====================
    QWidget* videoTab = new QWidget();
    QFormLayout* videoLayout = new QFormLayout(videoTab);
    videoLayout->setSpacing(12);
    videoLayout->setContentsMargins(16, 16, 16, 16);

    m_fpsSpinBox = new QSpinBox(videoTab);
    m_fpsSpinBox->setRange(1, 240);
    m_fpsSpinBox->setValue(60);
    m_fpsSpinBox->setSuffix(" FPS");
    videoLayout->addRow(tr("采集帧率:"), m_fpsSpinBox);

    m_trueColorCheckBox = new QCheckBox(tr("真彩 4:4:4 (更高色彩保真度)"), videoTab);
    videoLayout->addRow("", m_trueColorCheckBox);

    videoLayout->addRow(new QLabel("", videoTab));

    m_tabWidget->addTab(videoTab, tr("视频"));

    // ==================== Tab 4: Network ====================
    QWidget* networkTab = new QWidget();
    QVBoxLayout* networkMainLayout = new QVBoxLayout(networkTab);
    networkMainLayout->setContentsMargins(16, 16, 16, 16);

    // Scan settings group
    QGroupBox* scanGroup = new QGroupBox(tr("局域网扫描"), networkTab);
    QFormLayout* scanLayout = new QFormLayout(scanGroup);

    m_scanTimeoutSpinBox = new QSpinBox(scanGroup);
    m_scanTimeoutSpinBox->setRange(1, 30);
    m_scanTimeoutSpinBox->setValue(5);
    m_scanTimeoutSpinBox->setSuffix(tr(" 秒"));
    m_scanTimeoutSpinBox->setToolTip(tr("每个主机的TCP连接超时时间，增大可提高扫描可靠性"));
    scanLayout->addRow(tr("扫描超时:"), m_scanTimeoutSpinBox);

    networkMainLayout->addWidget(scanGroup);

    // Relay settings group
    QGroupBox* relayGroup = new QGroupBox(tr("中继服务器 (跨互联网连接)"), networkTab);
    QVBoxLayout* relayLayout = new QVBoxLayout(relayGroup);

    m_relayCheckBox = new QCheckBox(tr("启用中继"), relayGroup);
    relayLayout->addWidget(m_relayCheckBox);

    QFormLayout* relayForm = new QFormLayout();
    m_relayHostEdit = new QLineEdit(relayGroup);
    m_relayHostEdit->setPlaceholderText(tr("中继服务器地址 (IP 或域名)"));
    relayForm->addRow(tr("服务器地址:"), m_relayHostEdit);

    m_relayPortSpinBox = new QSpinBox(relayGroup);
    m_relayPortSpinBox->setRange(1, 65535);
    m_relayPortSpinBox->setValue(9997);
    relayForm->addRow(tr("端口:"), m_relayPortSpinBox);

    m_relayTokenEdit = new QLineEdit(relayGroup);
    m_relayTokenEdit->setPlaceholderText(tr("中继密钥 (与中继服务器一致)"));
    m_relayTokenEdit->setEchoMode(QLineEdit::Password);
    relayForm->addRow(tr("密钥:"), m_relayTokenEdit);

    relayLayout->addLayout(relayForm);
    networkMainLayout->addWidget(relayGroup);

    networkMainLayout->addStretch();

    m_tabWidget->addTab(networkTab, tr("网络"));

    // ==================== Tab 5: Appearance ====================
    QWidget* appearanceTab = new QWidget();
    QVBoxLayout* appearanceLayout = new QVBoxLayout(appearanceTab);
    appearanceLayout->setContentsMargins(16, 16, 16, 16);

    QFormLayout* appearanceForm = new QFormLayout();

    m_themeCombo = new QComboBox(appearanceTab);
    QMap<QString, QString> themeNames = {
        {"pink", tr("女生主题")},
        {"otaku", tr("宅男主题")},
        {"student", tr("学生主题")},
        {"teacher", tr("教师主题")},
        {"boss", tr("老板主题")},
        {"taoist", tr("道士主题")},
        {"wukong", tr("悟空主题")},
        {"baby", tr("宝贝主题")}
    };
    QMapIterator<QString, QString> i(themeNames);
    while (i.hasNext()) {
        i.next();
        m_themeCombo->addItem(i.value(), i.key());
    }
    appearanceForm->addRow(tr("主题:"), m_themeCombo);

    appearanceLayout->addLayout(appearanceForm);

    // Custom background group
    QGroupBox* bgGroup = new QGroupBox(tr("自定义背景图片"), appearanceTab);
    QVBoxLayout* bgLayout = new QVBoxLayout(bgGroup);

    QHBoxLayout* bgPathLayout = new QHBoxLayout();
    m_customBgEdit = new QLineEdit(appearanceTab);
    m_customBgEdit->setPlaceholderText(tr("选择自定义背景图片..."));
    bgPathLayout->addWidget(m_customBgEdit);

    m_browseBgButton = new QPushButton(tr("浏览"), appearanceTab);
    connect(m_browseBgButton, &QPushButton::clicked, this, &SettingsWidget::onBrowseBackground);
    bgPathLayout->addWidget(m_browseBgButton);

    m_clearBgButton = new QPushButton(tr("清除"), appearanceTab);
    connect(m_clearBgButton, &QPushButton::clicked, this, &SettingsWidget::onClearBackground);
    bgPathLayout->addWidget(m_clearBgButton);

    bgLayout->addLayout(bgPathLayout);
    appearanceLayout->addWidget(bgGroup);

    appearanceLayout->addStretch();

    m_tabWidget->addTab(appearanceTab, tr("外观"));

    mainLayout->addWidget(m_tabWidget);

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

bool SettingsWidget::startWithWindows() const {
    return m_startWithWindowsCheckBox && m_startWithWindowsCheckBox->isChecked();
}

bool SettingsWidget::minimizeToTray() const {
    return m_minimizeToTrayCheckBox && m_minimizeToTrayCheckBox->isChecked();
}

bool SettingsWidget::notificationSoundEnabled() const {
    return m_notificationSoundCheckBox && m_notificationSoundCheckBox->isChecked();
}

} // namespace xrk
