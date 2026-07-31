#include "session_manager.h"
#include "core/logger.h"
#include <QTimer>
#include <QUuid>

namespace xrk {

SessionManager::SessionManager(QObject* parent) : QObject(parent) {
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &SessionManager::onCleanupExpiredSessions);
    m_cleanupTimer->start(60000);
}

SessionManager::~SessionManager() {
    m_cleanupTimer->stop();
}

QString SessionManager::createSession(const QString& deviceId) {
    QString sessionId = generateSessionId();
    
    SessionInfo info;
    info.sessionId = sessionId;
    info.deviceId = deviceId;
    info.startTime = QDateTime::currentDateTime();
    info.active = true;
    
    m_sessions[sessionId] = info;
    
    emit sessionCreated(sessionId, deviceId);
    LOG_DEBUG("Session created: " + sessionId + " for device " + deviceId);
    
    return sessionId;
}

void SessionManager::closeSession(const QString& sessionId) {
    if (m_sessions.contains(sessionId)) {
        m_sessions[sessionId].active = false;
        emit sessionClosed(sessionId);
        LOG_DEBUG("Session closed: " + sessionId);
    }
}

void SessionManager::closeAllSessions() {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        it.value().active = false;
    }
    LOG_DEBUG("All sessions closed");
}

SessionInfo SessionManager::getSession(const QString& sessionId) const {
    return m_sessions.value(sessionId);
}

QList<SessionInfo> SessionManager::getActiveSessions() const {
    QList<SessionInfo> activeSessions;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().active) {
            activeSessions.append(it.value());
        }
    }
    return activeSessions;
}

bool SessionManager::hasActiveSession(const QString& deviceId) const {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().active && it.value().deviceId == deviceId) {
            return true;
        }
    }
    return false;
}

bool SessionManager::isSessionValid(const QString& sessionId) const {
    if (!m_sessions.contains(sessionId)) {
        return false;
    }
    return m_sessions[sessionId].active;
}

void SessionManager::onCleanupExpiredSessions() {
    QDateTime now = QDateTime::currentDateTime();
    QStringList expiredSessions;
    
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        qint64 elapsed = it.value().startTime.secsTo(now) * 1000;
        if (!it.value().active || elapsed > SESSION_EXPIRY_MS) {
            expiredSessions.append(it.key());
        }
    }
    
    for (const QString& sessionId : expiredSessions) {
        emit sessionExpired(sessionId);
        m_sessions.remove(sessionId);
    }
}

QString SessionManager::generateSessionId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace xrk
