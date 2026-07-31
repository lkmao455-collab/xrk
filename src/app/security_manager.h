#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QHash>

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

signals:
    void authenticationFailed(const QString& deviceId);
    void tokenExpired(const QString& token);

private:
    QString generateRandomString(int length);
    
    bool m_encryptionEnabled = false;
    QHash<QString, QString> m_activeTokens;
};

} // namespace xrk
