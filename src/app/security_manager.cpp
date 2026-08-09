#include "security_manager.h"
#include "core/encryption.h"
#include <QUuid>
#include <QRandomGenerator>
#include <QDateTime>
#include <QCryptographicHash>

namespace xrk {

SecurityManager::SecurityManager(QObject* parent) : QObject(parent) {
}

SecurityManager::~SecurityManager() {
}

QString SecurityManager::generateDeviceId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString SecurityManager::generateSessionToken() {
    QString token = generateRandomString(32);
    // Register the freshly issued token so validateToken() can find it.
    // The value stores the issue timestamp (ISO 8601); it can later be used
    // for expiry checks, and invalidateToken() clears it.
    m_activeTokens.insert(token, QDateTime::currentDateTime().toString(Qt::ISODate));
    return token;
}

bool SecurityManager::validateToken(const QString& token) const {
    return m_activeTokens.contains(token);
}

void SecurityManager::invalidateToken(const QString& token) {
    m_activeTokens.remove(token);
}

QByteArray SecurityManager::encrypt(const QByteArray& data, const QByteArray& key) {
    if (!m_encryptionEnabled || key.isEmpty()) {
        return data;
    }
    
    // Derive a 32-byte AES key and 16-byte IV from the provided key
    QByteArray aesKey = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    QByteArray iv = QCryptographicHash::hash(key + QByteArray("iv"), QCryptographicHash::Sha256).left(16);
    
    Encryption cipher;
    if (!cipher.setKey(aesKey, iv)) {
        return data;
    }
    return cipher.encrypt(data);
}

QByteArray SecurityManager::decrypt(const QByteArray& data, const QByteArray& key) {
    if (!m_encryptionEnabled || key.isEmpty()) {
        return data;
    }
    
    QByteArray aesKey = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    QByteArray iv = QCryptographicHash::hash(key + QByteArray("iv"), QCryptographicHash::Sha256).left(16);
    
    Encryption cipher;
    if (!cipher.setKey(aesKey, iv)) {
        return data;
    }
    return cipher.decrypt(data);
}

QByteArray SecurityManager::hashPassword(const QString& password) {
    QByteArray data = password.toUtf8();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(data);
    return hash.result();
}

bool SecurityManager::verifyPassword(const QString& password, const QByteArray& hash) {
    return hashPassword(password) == hash;
}

void SecurityManager::setEncryptionEnabled(bool enabled) {
    m_encryptionEnabled = enabled;
}

bool SecurityManager::isEncryptionEnabled() const {
    return m_encryptionEnabled;
}

QString SecurityManager::generateRandomString(int length) {
    const QString chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString result;
    result.reserve(length);

    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < length; ++i) {
        result.append(chars[rng->bounded(chars.size())]);
    }

    return result;
}

QByteArray SecurityManager::generateECDHKeyPair() {
    QByteArray privateKey(32, 0);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 32; ++i) {
        privateKey[i] = static_cast<char>(rng->bounded(256));
    }
    return privateKey;
}

QByteArray SecurityManager::deriveKeyFromPassword(const QString& password, const QString& salt) {
    QByteArray data = password.toUtf8() + salt.toUtf8();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(data);
    return hash.result();
}

bool SecurityManager::createE2EESession(const QString& deviceId, const QByteArray& peerPublicKey, const QByteArray& sharedSecret) {
    E2EESession session;
    session.deviceId = deviceId;
    session.peerPublicKey = peerPublicKey;
    session.sharedSecret = sharedSecret;
    session.sessionId = generateDeviceId();
    session.establishedAt = QDateTime::currentSecsSinceEpoch();
    session.lastActivity = session.establishedAt;
    session.encryptionAlgorithm = "AES-256-GCM";
    session.nonce = QByteArray(12, 0);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 12; ++i) {
        session.nonce[i] = static_cast<char>(rng->bounded(256));
    }
    session.isActive = true;

    m_e2eeSessions.insert(deviceId, session);
    emit e2eeSessionEstablished(deviceId);
    return true;
}

bool SecurityManager::hasE2EESession(const QString& deviceId) const {
    return m_e2eeSessions.contains(deviceId);
}

SecurityManager::E2EESession* SecurityManager::getE2EESession(const QString& deviceId) {
    if (!m_e2eeSessions.contains(deviceId)) {
        return nullptr;
    }
    return &m_e2eeSessions.find(deviceId).value();
}

QList<SecurityManager::E2EESession> SecurityManager::getAllE2EESessions() const {
    return m_e2eeSessions.values();
}

bool SecurityManager::removeE2EESession(const QString& deviceId) {
    if (!m_e2eeSessions.contains(deviceId)) {
        return false;
    }
    m_e2eeSessions.remove(deviceId);
    emit e2eeSessionRemoved(deviceId);
    return true;
}

bool SecurityManager::updateSessionActivity(const QString& deviceId) {
    if (!m_e2eeSessions.contains(deviceId)) {
        return false;
    }
    E2EESession& session = m_e2eeSessions.find(deviceId).value();
    session.lastActivity = QDateTime::currentSecsSinceEpoch();
    return true;
}

QByteArray SecurityManager::getSessionId(const QString& deviceId) const {
    if (!m_e2eeSessions.contains(deviceId)) {
        return QByteArray();
    }
    return m_e2eeSessions.value(deviceId).sessionId.toUtf8();
}

// --- IP Blacklist ---

void SecurityManager::addBlacklistedIp(const QString& ip, const QString& reason) {
    m_rateLimits[ip].blacklisted = true;
    m_rateLimits[ip].blacklistReason = reason;
}

void SecurityManager::removeBlacklistedIp(const QString& ip) {
    m_rateLimits[ip].blacklisted = false;
    m_rateLimits[ip].blacklistReason.clear();
}

bool SecurityManager::isIpBlacklisted(const QString& ip) const {
    return m_rateLimits.value(ip).blacklisted;
}

QList<QPair<QString, QString>> SecurityManager::blacklistedIps() const {
    QList<QPair<QString, QString>> result;
    for (auto it = m_rateLimits.constBegin(); it != m_rateLimits.constEnd(); ++it) {
        if (it.value().blacklisted) {
            result.append({it.key(), it.value().blacklistReason});
        }
    }
    return result;
}

// --- Rate Limiting ---

void SecurityManager::recordFailedAttempt(const QString& ip) {
    m_rateLimits[ip].failedTimestamps.append(QDateTime::currentMSecsSinceEpoch());
}

void SecurityManager::clearFailedAttempts(const QString& ip) {
    m_rateLimits[ip].failedTimestamps.clear();
}

int SecurityManager::failedAttemptCount(const QString& ip) const {
    return m_rateLimits.value(ip).failedTimestamps.size();
}

bool SecurityManager::checkRateLimit(const QString& ip, int maxAttempts, int windowSeconds) {
    if (isIpBlacklisted(ip)) return false;
    RateLimitEntry& entry = m_rateLimits[ip];
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 windowStart = now - (windowSeconds * 1000LL);
    // Remove old entries
    while (!entry.failedTimestamps.isEmpty() && entry.failedTimestamps.first() < windowStart) {
        entry.failedTimestamps.removeFirst();
    }
    return entry.failedTimestamps.size() < maxAttempts;
}

bool SecurityManager::isIpLockedOut(const QString& ip, int maxAttempts, int lockoutSeconds) const {
    if (isIpBlacklisted(ip)) return true;
    const RateLimitEntry& entry = m_rateLimits.value(ip);
    if (entry.failedTimestamps.size() < maxAttempts) return false;
    qint64 lastFailure = entry.failedTimestamps.last();
    qint64 lockoutEnd = lastFailure + (lockoutSeconds * 1000LL);
    return QDateTime::currentMSecsSinceEpoch() < lockoutEnd;
}

} // namespace xrk
