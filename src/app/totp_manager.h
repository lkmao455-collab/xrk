#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QTimer>

namespace xrk {

class TotpManager : public QObject {
    Q_OBJECT
public:
    explicit TotpManager(QObject* parent = nullptr);
    ~TotpManager();

    // Generate a new TOTP secret (base32 encoded)
    QString generateSecret();

    // Generate current TOTP code from secret
    QString generateCode(const QString& secret, qint64 timeMs = 0);

    // Verify a TOTP code (with window of +/- 1 step)
    bool verifyCode(const QString& secret, const QString& code, int window = 1);

    // Generate provisioning URI for QR code
    QString provisioningUri(const QString& secret, const QString& accountName,
                            const QString& issuer = "XRK");

    // Setup/teardown 2FA
    bool enable2FA(const QString& accountId, const QString& secret);
    bool disable2FA(const QString& accountId);
    bool is2FAEnabled(const QString& accountId) const;
    QString getSecret(const QString& accountId) const;

    // Generate backup codes
    QStringList generateBackupCodes();
    bool verifyBackupCode(const QString& accountId, const QString& code);
    QStringList getBackupCodes(const QString& accountId) const;

    // Time-based utilities
    qint64 currentTimestamp() const;
    int remainingSeconds() const;

signals:
    void twoFAEnabled(const QString& accountId);
    void twoFADisabled(const QString& accountId);

private:
    QByteArray hmacSha1(const QByteArray& key, const QByteArray& message);
    QByteArray base32Decode(const QString& encoded);
    QString base32Encode(const QByteArray& data);
    int dynamicTruncation(const QByteArray& hmac);

    QMap<QString, QString> m_secrets; // accountId -> secret
    QMap<QString, QStringList> m_backupCodes; // accountId -> backup codes
};

} // namespace xrk
