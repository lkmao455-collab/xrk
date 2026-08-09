#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QDateTime>

namespace xrk {

class SecurityManager : public QObject {
    Q_OBJECT
public:
    explicit SecurityManager(QObject* parent = nullptr);
    ~SecurityManager();

    QString generateDeviceId();
    QString generateSessionToken();

    bool validateToken(const QString& token) const;
    void invalidateToken(const QString& token);

    QByteArray encrypt(const QByteArray& data, const QByteArray& key);
    QByteArray decrypt(const QByteArray& data, const QByteArray& key);

    QByteArray hashPassword(const QString& password);
    bool verifyPassword(const QString& password, const QByteArray& hash);

    void setEncryptionEnabled(bool enabled);
    bool isEncryptionEnabled() const;

    // E2EE - End-to-End Encryption
    struct E2EESession {
        QString deviceId;
        QByteArray peerPublicKey;
        QByteArray sharedSecret;
        QString sessionId;
        qint64 establishedAt;
        qint64 lastActivity;
        QString encryptionAlgorithm;
        QByteArray nonce;
        bool isActive;
    };

    QByteArray generateECDHKeyPair();
    QByteArray deriveKeyFromPassword(const QString& password, const QString& salt);

    bool createE2EESession(const QString& deviceId, const QByteArray& peerPublicKey, const QByteArray& sharedSecret);
    bool hasE2EESession(const QString& deviceId) const;
    E2EESession* getE2EESession(const QString& deviceId);
    QList<E2EESession> getAllE2EESessions() const;
    bool removeE2EESession(const QString& deviceId);
    bool updateSessionActivity(const QString& deviceId);
    QByteArray getSessionId(const QString& deviceId) const;

    // IP Blacklist
    void addBlacklistedIp(const QString& ip, const QString& reason = QString());
    void removeBlacklistedIp(const QString& ip);
    bool isIpBlacklisted(const QString& ip) const;
    QList<QPair<QString, QString>> blacklistedIps() const;

    // Rate limiting
    bool checkRateLimit(const QString& ip, int maxAttempts = 5, int windowSeconds = 300);
    void recordFailedAttempt(const QString& ip);
    void clearFailedAttempts(const QString& ip);
    int failedAttemptCount(const QString& ip) const;
    bool isIpLockedOut(const QString& ip, int maxAttempts = 5, int lockoutSeconds = 900) const;

signals:
    void authenticationFailed(const QString& deviceId);
    void tokenExpired(const QString& token);
    void e2eeSessionEstablished(const QString& deviceId);
    void e2eeSessionRemoved(const QString& deviceId);

private:
    QString generateRandomString(int length);

    bool m_encryptionEnabled = false;
    QHash<QString, QString> m_activeTokens;
    QMap<QString, E2EESession> m_e2eeSessions;

    // Rate limiting
    struct RateLimitEntry {
        QList<qint64> failedTimestamps; // timestamps of failed attempts
        bool blacklisted = false;
        QString blacklistReason;
    };
    QMap<QString, RateLimitEntry> m_rateLimits;
};

} // namespace xrk
