#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QString>

namespace xrk {

class IPMsgManager;

class KeyVerificationDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyVerificationDialog(IPMsgManager* manager, const QString& deviceId,
                                   const QString& peerName, QWidget* parent = nullptr);
    ~KeyVerificationDialog();

    static void showVerification(IPMsgManager* manager, const QString& deviceId,
                                 const QString& peerName, QWidget* parent = nullptr);

signals:
    void verified(const QString& deviceId);
    void unverified(const QString& deviceId);

private slots:
    void onVerifyClicked();
    void onCompareClicked();

private:
    void setupUI();
    void updateStatusDisplay();
    QColor generateColorFromFingerprint(const QByteArray& fingerprint) const;
    QWidget* createFingerprintBlock(const QString& group, const QColor& color);

    IPMsgManager* m_manager;
    QString m_deviceId;
    QString m_peerName;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_fingerprintLabel = nullptr;
    QPushButton* m_verifyButton = nullptr;
    QPushButton* m_compareButton = nullptr;
    QWidget* m_fingerprintVisual = nullptr;
    bool m_isVerified = false;
};

} // namespace xrk
