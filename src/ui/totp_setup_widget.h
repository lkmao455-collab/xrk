#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QImage>

namespace xrk {

class TotpManager;

class TotpSetupWidget : public QDialog {
    Q_OBJECT
public:
    explicit TotpSetupWidget(TotpManager* totpManager, const QString& accountId, QWidget* parent = nullptr);
    ~TotpSetupWidget();

signals:
    void twoFAEnabled(const QString& accountId);

private slots:
    void onVerifyClicked();
    void onEnableClicked();
    void onDisableClicked();

private:
    void setupUI();
    QImage generateQrCodeImage(const QString& data, int size = 200);
    void applyDarkTheme();

    TotpManager* m_totpManager = nullptr;
    QString m_accountId;
    QString m_secret;

    QLabel* m_qrLabel = nullptr;
    QLabel* m_secretLabel = nullptr;
    QLineEdit* m_verifyInput = nullptr;
    QPushButton* m_verifyBtn = nullptr;
    QPushButton* m_enableBtn = nullptr;
    QPushButton* m_disableBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_backupCodesLabel = nullptr;
};

} // namespace xrk
