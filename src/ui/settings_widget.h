#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QTabWidget>

namespace xrk {

class SettingsWidget : public QDialog {
    Q_OBJECT
public:
    explicit SettingsWidget(QWidget* parent = nullptr);
    ~SettingsWidget();

    void loadSettings();
    void saveSettings();

    uint16_t port() const;
    bool autoDiscovery() const;
    bool encryptionEnabled() const;
    QString deviceName() const;
    int fps() const;
    bool privacyScreenEnabled() const;
    bool autoGrantConsentEnabled() const;
    bool trueColorEnabled() const;

    // Relay
    bool relayEnabled() const;
    QString relayHost() const;
    uint16_t relayPort() const;
    QString relayToken() const;
    QString selectedLanguage() const;

    // Scan
    int scanTimeoutMs() const;

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onBrowseBackground();
    void onClearBackground();

private:
    void setupUI();

    QTabWidget* m_tabWidget = nullptr;

    // General tab
    QLineEdit* m_deviceNameEdit = nullptr;
    QSpinBox* m_portSpinBox = nullptr;
    QCheckBox* m_autoDiscoveryCheckBox = nullptr;
    QComboBox* m_languageCombo = nullptr;

    // Security tab
    QCheckBox* m_encryptionCheckBox = nullptr;
    QCheckBox* m_privacyScreenCheckBox = nullptr;
    QCheckBox* m_autoGrantConsentCheckBox = nullptr;

    // Video tab
    QCheckBox* m_trueColorCheckBox = nullptr;
    QSpinBox* m_fpsSpinBox = nullptr;

    // Network tab
    QSpinBox* m_scanTimeoutSpinBox = nullptr;
    QCheckBox* m_relayCheckBox = nullptr;
    QLineEdit* m_relayHostEdit = nullptr;
    QSpinBox* m_relayPortSpinBox = nullptr;
    QLineEdit* m_relayTokenEdit = nullptr;

    // Appearance tab
    QComboBox* m_themeCombo = nullptr;
    QLineEdit* m_customBgEdit = nullptr;
    QPushButton* m_browseBgButton = nullptr;
    QPushButton* m_clearBgButton = nullptr;

    QPushButton* m_okButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
};

} // namespace xrk
