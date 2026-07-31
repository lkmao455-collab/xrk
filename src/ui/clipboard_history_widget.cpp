#include "clipboard_history_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QDateTime>

namespace xrk {

ClipboardHistoryWidget::ClipboardHistoryWidget(ClipboardHistory* history, QWidget* parent)
    : QWidget(parent), m_history(history) {
    setupUI();
    refresh();

    if (m_history) {
        connect(m_history, &ClipboardHistory::entryAdded,
                this, &ClipboardHistoryWidget::onEntryAdded);
        connect(m_history, &ClipboardHistory::historyChanged,
                this, &ClipboardHistoryWidget::refresh);
    }
}

ClipboardHistoryWidget::~ClipboardHistoryWidget() {
}

void ClipboardHistoryWidget::refresh() {
    if (!m_history) return;

    m_listWidget->clear();
    QList<ClipboardEntry> entries = m_history->entries();

    for (const auto& entry : entries) {
        QListWidgetItem* item = createItem(entry);
        m_listWidget->addItem(item);
    }

    m_countLabel->setText(QString("共 %1 条记录").arg(entries.size()));
}

QListWidgetItem* ClipboardHistoryWidget::createItem(const ClipboardEntry& entry) {
    QListWidgetItem* item = new QListWidgetItem();

    QString prefix = entry.isFavorite ? "★ " : "";
    QString mimeTypeIcon;
    if (entry.mimeType.startsWith("text/")) {
        mimeTypeIcon = "[Text]";
    } else if (entry.mimeType.startsWith("image/")) {
        mimeTypeIcon = "[Image]";
    } else {
        mimeTypeIcon = "[Other]";
    }

    QString timeStr = entry.timestamp.toString("MM-dd HH:mm:ss");
    QString text = QString("%1%2 %3\n   %4\n   %5")
        .arg(prefix, mimeTypeIcon, timeStr, entry.preview.left(80),
             entry.mimeType);

    item->setText(text);
    item->setData(Qt::UserRole, entry.id);
    item->setData(Qt::UserRole + 1, entry.mimeType);
    item->setData(Qt::UserRole + 2, entry.preview);
    item->setToolTip(QString("MIME: %1\nTime: %2\n\n%3")
        .arg(entry.mimeType, entry.timestamp.toString("yyyy-MM-dd HH:mm:ss"), entry.preview));

    if (entry.isFavorite) {
        item->setForeground(QColor(255, 215, 0));
    }

    return item;
}

void ClipboardHistoryWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item || !m_history) return;

    QString id = item->data(Qt::UserRole).toString();
    ClipboardEntry entry = m_history->entry(id);
    if (entry.id.isEmpty()) return;

    emit copyToClipboardRequested(entry);
    QApplication::clipboard()->setText(entry.preview);
}

void ClipboardHistoryWidget::onItemContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_listWidget->itemAt(pos);
    if (!item) return;

    QString id = item->data(Qt::UserRole).toString();

    QMenu menu(this);
    QAction* copyAction = menu.addAction("复制到剪贴板");
    QAction* favAction = menu.addAction(m_history->entry(id).isFavorite ? "取消收藏" : "设为收藏");
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("删除");

    QAction* selectedAction = menu.exec(m_listWidget->mapToGlobal(pos));

    if (selectedAction == copyAction) {
        ClipboardEntry entry = m_history->entry(id);
        if (!entry.id.isEmpty()) {
            QApplication::clipboard()->setText(entry.preview);
            emit copyToClipboardRequested(entry);
        }
    } else if (selectedAction == favAction) {
        m_history->toggleFavorite(id);
    } else if (selectedAction == deleteAction) {
        m_history->removeEntry(id);
    }
}

void ClipboardHistoryWidget::onSearchChanged(const QString& text) {
    if (!m_history) return;

    m_listWidget->clear();
    QList<ClipboardEntry> entries;

    if (text.isEmpty()) {
        entries = m_history->entries();
    } else {
        entries = m_history->search(text);
    }

    for (const auto& entry : entries) {
        QListWidgetItem* item = createItem(entry);
        m_listWidget->addItem(item);
    }

    m_countLabel->setText(QString("共 %1 条记录").arg(entries.size()));
}

void ClipboardHistoryWidget::onClearClicked() {
    auto ret = QMessageBox::question(this, "清空历史",
        "确定要清空所有剪贴板历史记录?",
        QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes && m_history) {
        m_history->clear();
    }
}

void ClipboardHistoryWidget::onEntryAdded(const ClipboardEntry& entry) {
    QListWidgetItem* item = createItem(entry);
    m_listWidget->insertItem(0, item);
    m_countLabel->setText(QString("共 %1 条记录").arg(m_listWidget->count()));
}

void ClipboardHistoryWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索剪贴板历史...");
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ClipboardHistoryWidget::onSearchChanged);
    searchLayout->addWidget(m_searchEdit);

    m_countLabel = new QLabel("共 0 条记录", this);
    searchLayout->addWidget(m_countLabel);
    layout->addLayout(searchLayout);

    m_listWidget = new QListWidget(this);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listWidget->setAlternatingRowColors(true);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &ClipboardHistoryWidget::onItemDoubleClicked);
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &ClipboardHistoryWidget::onItemContextMenu);
    layout->addWidget(m_listWidget);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_clearButton = new QPushButton("清空历史", this);
    connect(m_clearButton, &QPushButton::clicked,
            this, &ClipboardHistoryWidget::onClearClicked);
    buttonLayout->addWidget(m_clearButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);
}

} // namespace xrk
