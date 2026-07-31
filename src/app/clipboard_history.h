#pragma once

#include <QObject>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QString>
#include <QMutex>

namespace xrk {

struct ClipboardEntry {
    QString id;
    QString mimeType;
    QByteArray data;
    QString preview;
    QDateTime timestamp;
    bool isFavorite = false;

    QJsonObject toJson() const;
    static ClipboardEntry fromJson(const QJsonObject& obj);
};

class ClipboardHistory : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHistory(QObject* parent = nullptr);
    ~ClipboardHistory();

    void load();
    void save();

    ClipboardEntry addEntry(const QString& mimeType, const QByteArray& data);
    QList<ClipboardEntry> entries() const;
    QList<ClipboardEntry> search(const QString& keyword) const;
    ClipboardEntry entry(const QString& id) const;
    void removeEntry(const QString& id);
    void toggleFavorite(const QString& id);
    void clear();
    int count() const;

    void setMaxEntries(int max);
    int maxEntries() const;

signals:
    void entryAdded(const ClipboardEntry& entry);
    void entryRemoved(const QString& id);
    void historyChanged();

private:
    void pruneOldEntries();
    QString generateId() const;
    QString createPreview(const QString& mimeType, const QByteArray& data) const;
    QString filePath() const;

    QList<ClipboardEntry> m_entries;
    int m_maxEntries = 50;
    mutable QMutex m_mutex;
};

} // namespace xrk
