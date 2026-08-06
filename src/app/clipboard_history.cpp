#include "clipboard_history.h"
#include "core/logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>

namespace xrk {

QJsonObject ClipboardEntry::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["mimeType"] = mimeType;
    obj["data"] = QString::fromUtf8(data.toBase64());
    obj["preview"] = preview;
    obj["timestamp"] = timestamp.toString(Qt::ISODate);
    obj["isFavorite"] = isFavorite;
    return obj;
}

ClipboardEntry ClipboardEntry::fromJson(const QJsonObject& obj) {
    ClipboardEntry entry;
    entry.id = obj["id"].toString();
    entry.mimeType = obj["mimeType"].toString();
    entry.data = QByteArray::fromBase64(obj["data"].toString().toUtf8());
    entry.preview = obj["preview"].toString();
    entry.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
    entry.isFavorite = obj["isFavorite"].toBool(false);
    return entry;
}

ClipboardHistory::ClipboardHistory(QObject* parent)
    : QObject(parent) {
}

ClipboardHistory::~ClipboardHistory() {
    save();
}

void ClipboardHistory::load() {
    QMutexLocker locker(&m_mutex);
    m_entries.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    m_maxEntries = root.value("maxEntries").toInt(50);

    QJsonArray arr = root.value("entries").toArray();
    for (const QJsonValue& val : arr) {
        m_entries.append(ClipboardEntry::fromJson(val.toObject()));
    }
}

void ClipboardHistory::save() {
    QMutexLocker locker(&m_mutex);

    QJsonObject root;
    root["maxEntries"] = m_maxEntries;

    QJsonArray arr;
    for (const auto& entry : m_entries) {
        arr.append(entry.toJson());
    }
    root["entries"] = arr;

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile file(filePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

ClipboardEntry ClipboardHistory::addEntry(const QString& mimeType, const QByteArray& data) {
    if (data.isEmpty()) return ClipboardEntry();

    QMutexLocker locker(&m_mutex);

    for (const auto& existing : m_entries) {
        if (existing.mimeType == mimeType && existing.data == data) {
            return existing;
        }
    }

    ClipboardEntry entry;
    entry.id = generateId();
    entry.mimeType = mimeType;
    entry.data = data;
    entry.preview = createPreview(mimeType, data);
    entry.timestamp = QDateTime::currentDateTime();
    entry.isFavorite = false;

    m_entries.prepend(entry);

    pruneOldEntries();

    locker.unlock();
    save();
    emit entryAdded(entry);
    emit historyChanged();

    return entry;
}

QList<ClipboardEntry> ClipboardHistory::entries() const {
    QMutexLocker locker(&m_mutex);
    return m_entries;
}

QList<ClipboardEntry> ClipboardHistory::search(const QString& keyword) const {
    QMutexLocker locker(&m_mutex);
    QList<ClipboardEntry> result;
    QString kw = keyword.toLower();

    for (const auto& entry : m_entries) {
        if (entry.preview.toLower().contains(kw) ||
            entry.mimeType.toLower().contains(kw)) {
            result.append(entry);
        }
    }
    return result;
}

ClipboardEntry ClipboardHistory::entry(const QString& id) const {
    QMutexLocker locker(&m_mutex);
    for (const auto& entry : m_entries) {
        if (entry.id == id) return entry;
    }
    return ClipboardEntry();
}

void ClipboardHistory::removeEntry(const QString& id) {
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            m_entries.removeAt(i);
            locker.unlock();
            save();
            emit entryRemoved(id);
            emit historyChanged();
            return;
        }
    }
}

void ClipboardHistory::toggleFavorite(const QString& id) {
    QMutexLocker locker(&m_mutex);
    for (auto& entry : m_entries) {
        if (entry.id == id) {
            entry.isFavorite = !entry.isFavorite;
            locker.unlock();
            save();
            emit historyChanged();
            return;
        }
    }
}

void ClipboardHistory::clear() {
    QMutexLocker locker(&m_mutex);
    m_entries.clear();
    locker.unlock();
    save();
    emit historyChanged();
}

int ClipboardHistory::count() const {
    QMutexLocker locker(&m_mutex);
    return m_entries.size();
}

void ClipboardHistory::setMaxEntries(int max) {
    m_maxEntries = qMax(10, max);
    pruneOldEntries();
    save();
}

int ClipboardHistory::maxEntries() const {
    return m_maxEntries;
}

void ClipboardHistory::pruneOldEntries() {
    while (m_entries.size() > m_maxEntries) {
        ClipboardEntry last = m_entries.last();
        if (last.isFavorite) {
            if (m_entries.size() <= m_maxEntries + 10) break;
            m_entries.move(m_entries.size() - 1, 0);
        } else {
            m_entries.removeLast();
        }
    }
}

QString ClipboardHistory::generateId() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ClipboardHistory::createPreview(const QString& mimeType, const QByteArray& data) const {
    if (mimeType == "text/uri-list") {
        QString text = QString::fromUtf8(data);
        QStringList urls = text.split("\n", Qt::SkipEmptyParts);
        if (urls.size() == 1) {
            return urls.first().trimmed();
        }
        return QString("[%1 files/items]").arg(urls.size());
    } else if (mimeType.startsWith("text/")) {
        QString text = QString::fromUtf8(data);
        text.replace("\n", " ");
        text.replace("\r", "");
        if (text.length() > 200) {
            text = text.left(200) + "...";
        }
        return text;
    } else if (mimeType == "image/png" || mimeType == "image/jpeg") {
        return "[Image " + QString::number(data.size()) + " bytes]";
    }
    return "[" + mimeType + " " + QString::number(data.size()) + " bytes]";
}

QString ClipboardHistory::filePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/clipboard_history.json";
}

} // namespace xrk
