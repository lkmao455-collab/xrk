#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QMenu>
#include "app/clipboard_history.h"

namespace xrk {

class ClipboardHistoryWidget : public QWidget {
    Q_OBJECT
public:
    explicit ClipboardHistoryWidget(ClipboardHistory* history, QWidget* parent = nullptr);
    ~ClipboardHistoryWidget();

    void refresh();

signals:
    void entrySelected(const ClipboardEntry& entry);
    void copyToClipboardRequested(const ClipboardEntry& entry);

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);
    void onItemContextMenu(const QPoint& pos);
    void onSearchChanged(const QString& text);
    void onClearClicked();
    void onEntryAdded(const ClipboardEntry& entry);

private:
    void setupUI();
    QListWidgetItem* createItem(const ClipboardEntry& entry);

    ClipboardHistory* m_history = nullptr;
    QListWidget* m_listWidget = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_clearButton = nullptr;
    QLabel* m_countLabel = nullptr;
};

} // namespace xrk
