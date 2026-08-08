#pragma once

#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QTimer>
#include <QMap>
#include <QElapsedTimer>
#include "core/types.h"

namespace xrk {

class GroupVoteWidget : public QWidget {
    Q_OBJECT
public:
    explicit GroupVoteWidget(QWidget* parent = nullptr);
    ~GroupVoteWidget();

    void setGroupId(const QString& groupId);
    void setGroupName(const QString& groupName);
    void setCurrentUserId(const QString& userId);
    void addVote(const GroupVote& vote);
    void addVoteOption(const QString& voteTitle, const QString& option, int voteCount);
    void setVotes(const QList<GroupVote>& votes);

signals:
    void voteCreated(const QString& groupId, const GroupVote& vote);
    void voteSubmitted(const QString& groupId, const QString& voteTitle, const QString& option);
    void closed();

private slots:
    void onCreateClicked();
    void onAddOptionClicked();
    void onVoteClicked(QListWidgetItem* item);
    void onVoteButtonClicked();

private:
    void setupUI();
    void refreshList();
    void showVoteDetail(const GroupVote& vote);

    QString m_groupId;
    QString m_groupName;
    QString m_currentUserId;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_groupNameLabel = nullptr;
    QListWidget* m_voteList = nullptr;
    QLineEdit* m_voteTitleEdit = nullptr;
    QLineEdit* m_optionEdit = nullptr;
    QPushButton* m_addOptionBtn = nullptr;
    QPushButton* m_createVoteBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QWidget* m_detailWidget = nullptr;
    QLabel* m_detailTitle = nullptr;
    QListWidget* m_detailOptions = nullptr;
    QPushButton* m_voteBtn = nullptr;
    QPushButton* m_backBtn = nullptr;

    QList<GroupVote> m_votes;
    QMap<QString, QStringList> m_voteOptions;
    QMap<QString, QMap<QString, int>> m_voteResults;
    QString m_currentVoteTitle;
};

} // namespace xrk
