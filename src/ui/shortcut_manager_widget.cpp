#include "shortcut_manager_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QSettings>
#include <QKeySequenceEdit>

namespace xrk {

// --- ShortcutManager ---

ShortcutManager& ShortcutManager::instance() {
    static ShortcutManager inst;
    return inst;
}

ShortcutManager::ShortcutManager(QObject* parent) : QObject(parent) {
    loadFromSettings();
}

ShortcutManager::~ShortcutManager() {
    saveToSettings();
}

void ShortcutManager::registerShortcut(const QString& id, const QString& description, const QKeySequence& defaultKey) {
    if (!m_shortcuts.contains(id)) {
        m_shortcuts[id] = {id, description, defaultKey, defaultKey};
    }
}

QKeySequence ShortcutManager::getShortcut(const QString& id) const {
    return m_shortcuts.value(id).currentKey;
}

bool ShortcutManager::setShortcut(const QString& id, const QKeySequence& key) {
    if (!m_shortcuts.contains(id)) return false;
    m_shortcuts[id].currentKey = key;
    saveToSettings();
    emit shortcutChanged(id, key);
    return true;
}

void ShortcutManager::resetToDefaults() {
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        it.value().currentKey = it.value().defaultKey;
    }
    saveToSettings();
}

QList<ShortcutEntry> ShortcutManager::getAllShortcuts() const {
    return m_shortcuts.values();
}

void ShortcutManager::loadFromSettings() {
    QSettings s("XRK", "Shortcuts");
    s.beginGroup("shortcuts");
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        QString val = s.value(it.key(), it.value().defaultKey.toString()).toString();
        it.value().currentKey = QKeySequence(val);
    }
    s.endGroup();
}

void ShortcutManager::saveToSettings() {
    QSettings s("XRK", "Shortcuts");
    s.beginGroup("shortcuts");
    for (auto it = m_shortcuts.begin(); it != m_shortcuts.end(); ++it) {
        s.setValue(it.key(), it.value().currentKey.toString());
    }
    s.endGroup();
}

// --- ShortcutManagerWidget ---

ShortcutManagerWidget::ShortcutManagerWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
    refreshShortcuts();
}

ShortcutManagerWidget::~ShortcutManagerWidget() {}

void ShortcutManagerWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("功能"), tr("当前快捷键"), tr("默认快捷键")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ShortcutManagerWidget::onCellDoubleClicked);
    mainLayout->addWidget(m_table, 1);

    auto* bottomLayout = new QHBoxLayout();
    m_resetBtn = new QPushButton(tr("恢复默认"), this);
    connect(m_resetBtn, &QPushButton::clicked, this, &ShortcutManagerWidget::onResetClicked);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(bottomLayout);

    applyDarkTheme();
}

void ShortcutManagerWidget::refreshShortcuts() {
    auto shortcuts = ShortcutManager::instance().getAllShortcuts();
    m_table->setRowCount(shortcuts.size());
    for (int i = 0; i < shortcuts.size(); ++i) {
        const auto& entry = shortcuts[i];
        m_table->setItem(i, 0, new QTableWidgetItem(entry.description));
        m_table->setItem(i, 1, new QTableWidgetItem(entry.currentKey.toString()));
        m_table->setItem(i, 2, new QTableWidgetItem(entry.defaultKey.toString()));
        m_table->item(i, 0)->setData(Qt::UserRole, entry.id);
    }
}

void ShortcutManagerWidget::onResetClicked() {
    ShortcutManager::instance().resetToDefaults();
    refreshShortcuts();
}

void ShortcutManagerWidget::onCellDoubleClicked(int row, int column) {
    if (column != 1) return;
    QString id = m_table->item(row, 0)->data(Qt::UserRole).toString();
    m_editingId = id;

    auto* edit = new QKeySequenceEdit(this);
    connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, id](const QKeySequence& seq) {
        if (!seq.isEmpty()) {
            ShortcutManager::instance().setShortcut(id, seq);
            m_table->setItem(m_table->currentRow(), 1, new QTableWidgetItem(seq.toString()));
        }
    });
    m_table->setCellWidget(row, 1, edit);
    edit->setFocus();
}

void ShortcutManagerWidget::applyDarkTheme() {
    setStyleSheet(R"(
        ShortcutManagerWidget { background: #2b2b2b; color: #ddd; }
        QTableWidget { background: #1e1e1e; color: #ddd; gridline-color: #444; border: 1px solid #444; }
        QHeaderView::section { background: #3a3a3a; color: #ddd; border: 1px solid #444; padding: 4px; }
        QPushButton { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; padding: 5px 12px; }
        QPushButton:hover { background: #4a4a4a; }
    )");
}

} // namespace xrk
