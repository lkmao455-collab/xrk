#include "address_book.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>

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
            result.append(e);
        }
    }
    return result;
}

QString AddressBook::filePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/addressbook.json";
}

} // namespace xrk
