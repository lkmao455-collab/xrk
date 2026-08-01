#include "address_book.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <algorithm>

namespace xrk {

QJsonObject AddressBookEntry::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["ip"] = ip;
    obj["port"] = port;
    obj["mac"] = mac;
    obj["group"] = group;
    obj["notes"] = notes;
    obj["accessCode"] = accessCode;
    obj["favorite"] = favorite;
    obj["lastConnected"] = lastConnected.toString(Qt::ISODate);
    QJsonArray tagArr;
    for (const QString& t : tags) tagArr.append(t);
    obj["tags"] = tagArr;
    obj["lastSeen"] = lastSeen.toString(Qt::ISODate);
    return obj;
}

AddressBookEntry AddressBookEntry::fromJson(const QJsonObject& obj) {
    AddressBookEntry entry;
    entry.id = obj["id"].toString();
    entry.name = obj["name"].toString();
    entry.ip = obj["ip"].toString();
    entry.port = static_cast<uint16_t>(obj["port"].toInt(9999));
    entry.mac = obj["mac"].toString();
    entry.group = obj["group"].toString();
    entry.notes = obj["notes"].toString();
    entry.accessCode = obj["accessCode"].toString();
    entry.favorite = obj["favorite"].toBool(false);
    entry.lastConnected = QDateTime::fromString(obj["lastConnected"].toString(), Qt::ISODate);
    const QJsonArray tagArr = obj["tags"].toArray();
    for (const QJsonValue& v : tagArr) entry.tags.append(v.toString());
    entry.lastSeen = QDateTime::fromString(obj["lastSeen"].toString(), Qt::ISODate);
    return entry;
}

AddressBook::AddressBook() {
}

void AddressBook::load() {
    m_entries.clear();
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) return;
    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        m_entries.append(AddressBookEntry::fromJson(val.toObject()));
    }
}

void AddressBook::save() {
    QJsonArray arr;
    for (const auto& entry : m_entries) {
        arr.append(entry.toJson());
    }

    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile file(filePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
        file.close();
    }
}

QList<AddressBookEntry> AddressBook::allEntries() const {
    return m_entries;
}

AddressBookEntry AddressBook::entry(const QString& id) const {
    for (const auto& e : m_entries) {
        if (e.id == id) return e;
    }
    return AddressBookEntry();
}

void AddressBook::addEntry(const AddressBookEntry& entry) {
    AddressBookEntry e = entry;
    if (e.id.isEmpty()) {
        e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_entries.append(e);
    save();
}

void AddressBook::updateEntry(const AddressBookEntry& entry) {
    for (auto& e : m_entries) {
        if (e.id == entry.id) {
            e = entry;
            save();
            return;
        }
    }
}

void AddressBook::removeEntry(const QString& id) {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            m_entries.removeAt(i);
            break;
        }
    }
    save();
}

bool AddressBook::contains(const QString& id) const {
    for (const auto& e : m_entries) {
        if (e.id == id) return true;
    }
    return false;
}

QStringList AddressBook::groups() const {
    QStringList list;
    for (const auto& e : m_entries) {
        if (!e.group.isEmpty() && !list.contains(e.group))
            list.append(e.group);
    }
    list.sort();
    return list;
}

QList<AddressBookEntry> AddressBook::entriesByGroup(const QString& group) const {
    QList<AddressBookEntry> result;
    for (const auto& e : m_entries) {
        if (e.group == group) result.append(e);
    }
    return result;
}

QList<AddressBookEntry> AddressBook::favorites() const {
    QList<AddressBookEntry> result;
    for (const auto& e : m_entries) {
        if (e.favorite) result.append(e);
    }
    return result;
}

QList<AddressBookEntry> AddressBook::search(const QString& keyword) const {
    QList<AddressBookEntry> result;
    QString kw = keyword.toLower();
    for (const auto& e : m_entries) {
        if (e.name.toLower().contains(kw) || e.ip.contains(kw) ||
            e.group.toLower().contains(kw) || e.notes.toLower().contains(kw)) {
            bool tagHit = false;
            for (const QString& t : e.tags) {
                if (t.toLower().contains(kw)) { tagHit = true; break; }
            }
            if (tagHit) result.append(e);
        }
    }
    return result;
}

QList<AddressBookEntry> AddressBook::recent(int n) const {
    QList<AddressBookEntry> list = m_entries;
    std::sort(list.begin(), list.end(),
        [](const AddressBookEntry& a, const AddressBookEntry& b) {
            return a.lastConnected > b.lastConnected;
        });
    if (n >= 0 && list.size() > n) list.erase(list.begin() + n, list.end());
    return list;
}

bool AddressBook::renameGroup(const QString& oldName, const QString& newName) {
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) return false;
    bool changed = false;
    for (auto& e : m_entries) {
        if (e.group == oldName) { e.group = newName; changed = true; }
    }
    if (changed) save();
    return changed;
}

bool AddressBook::removeGroup(const QString& name) {
    if (name.isEmpty()) return false;
    bool changed = false;
    for (auto& e : m_entries) {
        if (e.group == name) { e.group.clear(); changed = true; }
    }
    if (changed) save();
    return changed;
}

// ── CSV helpers ──

namespace {
// Quote a field if it contains a comma, quote or newline; double internal quotes.
QString csvField(const QString& in) {
    if (in.contains(',') || in.contains('"') || in.contains('\n') || in.contains('\r')) {
        QString s = in;
        s.replace('"', "\"\"");
        return "\"" + s + "\"";
    }
    return in;
}

// Parse one CSV line honouring quoted fields (RFC 4180-ish).
QStringList parseCsvLine(const QString& line) {
    QStringList fields;
    QString cur;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        QChar c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cur += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                fields.append(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    fields.append(cur);
    return fields;
}
} // namespace

bool AddressBook::exportCsv(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    // Header row.
    out << csvField("name") << "," << csvField("ip") << "," << csvField("port") << ","
        << csvField("mac") << "," << csvField("group") << "," << csvField("accessCode") << ","
        << csvField("notes") << "," << csvField("favorite") << "," << csvField("tags") << ","
        << csvField("lastConnected") << "\n";
    for (const auto& e : m_entries) {
        out << csvField(e.name) << ","
            << csvField(e.ip) << ","
            << csvField(QString::number(e.port)) << ","
            << csvField(e.mac) << ","
            << csvField(e.group) << ","
            << csvField(e.accessCode) << ","
            << csvField(e.notes) << ","
            << csvField(e.favorite ? "1" : "0") << ","
            << csvField(e.tags.join(";")) << ","
            << csvField(e.lastConnected.toString(Qt::ISODate)) << "\n";
    }
    return true;
}

int AddressBook::importCsv(const QString& path, bool merge) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    auto readLine = [&]() -> QString {
        QString s;
        while (!in.atEnd()) {
            QString part = in.readLine();
            // Support quoted fields spanning multiple lines.
            if (part.count('"') % 2 == 0) return s.isEmpty() ? part : s + "\n" + part;
            s += (s.isEmpty() ? part : "\n" + part);
        }
        return s;
    };

    QString header = readLine();
    if (header.isEmpty()) return 0;
    QStringList cols = parseCsvLine(header);
    auto idx = [&](const QString& name) { return cols.indexOf(name); };
    int iName = idx("name"), iIp = idx("ip"), iPort = idx("port"), iMac = idx("mac"),
        iGroup = idx("group"), iCode = idx("accessCode"), iNotes = idx("notes"),
        iFav = idx("favorite"), iTags = idx("tags"), iLast = idx("lastConnected");

    int imported = 0;
    QString line;
    while (!(line = readLine()).isEmpty()) {
        QStringList f = parseCsvLine(line);
        if (f.size() <= iIp || f[iIp].trimmed().isEmpty()) continue;

        AddressBookEntry e;
        if (iName >= 0) e.name = f[iName].trimmed();
        e.ip = f[iIp].trimmed();
        if (iPort >= 0) {
            bool okp = false;
            int p = f[iPort].toInt(&okp);
            e.port = static_cast<uint16_t>(okp ? p : 9999);
        }
        if (iMac >= 0) e.mac = f[iMac].trimmed();
        if (iGroup >= 0) e.group = f[iGroup].trimmed();
        if (iCode >= 0) e.accessCode = f[iCode].trimmed();
        if (iNotes >= 0) e.notes = f[iNotes].trimmed();
        if (iFav >= 0) e.favorite = (f[iFav].trimmed() == "1");
        if (iTags >= 0) {
            for (const QString& t : f[iTags].split(';', Qt::SkipEmptyParts))
                e.tags.append(t.trimmed());
        }
        if (iLast >= 0) e.lastConnected = QDateTime::fromString(f[iLast].trimmed(), Qt::ISODate);

        if (merge) {
            bool found = false;
            for (auto& cur : m_entries) {
                if (cur.ip == e.ip) {
                    cur.name = e.name.isEmpty() ? cur.name : e.name;
                    cur.port = e.port;
                    cur.mac = e.mac;
                    cur.group = e.group.isEmpty() ? cur.group : e.group;
                    cur.accessCode = e.accessCode;
                    cur.notes = e.notes;
                    cur.favorite = e.favorite;
                    if (!e.tags.isEmpty()) cur.tags = e.tags;
                    if (e.lastConnected.isValid()) cur.lastConnected = e.lastConnected;
                    found = true;
                    break;
                }
            }
            if (!found) {
                e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                m_entries.append(e);
            }
        } else {
            e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_entries.append(e);
        }
        ++imported;
    }
    save();
    return imported;
}

bool AddressBook::exportJson(const QString& path) const {
    QJsonArray arr;
    for (const auto& e : m_entries) arr.append(e.toJson());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(arr).toJson());
    file.close();
    return true;
}

int AddressBook::importJson(const QString& path, bool merge) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) return 0;

    int imported = 0;
    for (const QJsonValue& val : doc.array()) {
        AddressBookEntry e = AddressBookEntry::fromJson(val.toObject());
        if (e.ip.isEmpty() && e.name.isEmpty()) continue;
        if (merge && !e.id.isEmpty() && contains(e.id)) {
            updateEntry(e);
        } else {
            if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_entries.append(e);
        }
        ++imported;
    }
    save();
    return imported;
}

QString AddressBook::filePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/addressbook.json";
}

} // namespace xrk
