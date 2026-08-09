#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QKeySequence>
#include <QMap>

namespace xrk {

struct ShortcutEntry {
    QString id;
    QString description;
    QKeySequence currentKey;
    QKeySequence defaultKey;
};

class ShortcutManager : public QObject {
    Q_OBJECT
public:
    explicit ShortcutManager(QObject* parent = nullptr);
    ~ShortcutManager();

    static ShortcutManager& instance();

    void registerShortcut(const QString& id, const QString& description, const QKeySequence& defaultKey);
    QKeySequence getShortcut(const QString& id) const;
    bool setShortcut(const QString& id, const QKeySequence& key);
    void resetToDefaults();
    QList<ShortcutEntry> getAllShortcuts() const;

    void loadFromSettings();
    void saveToSettings();

signals:
    void shortcutChanged(const QString& id, const QKeySequence& key);

private:
    QMap<QString, ShortcutEntry> m_shortcuts;
};

class ShortcutManagerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ShortcutManagerWidget(QWidget* parent = nullptr);
    ~ShortcutManagerWidget();

    void refreshShortcuts();

private slots:
    void onResetClicked();
    void onCellDoubleClicked(int row, int column);

private:
    void setupUI();
    void applyDarkTheme();

    QTableWidget* m_table = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QString m_editingId;
};

} // namespace xrk
