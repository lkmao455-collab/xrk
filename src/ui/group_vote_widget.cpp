#include "group_vote_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

namespace xrk {

GroupVoteWidget::GroupVoteWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setFixedSize(420, 520);
    setStyleSheet("background-color: #1e1e2e;");
    setupUI();
}

GroupVoteWidget::~GroupVoteWidget() {}

void GroupVoteWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    m_titleLabel = new QLabel(tr("Group Votes"), this);
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #e0e0e0;");
    m_closeBtn = new QPushButton(tr("X"), this);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setStyleSheet("QPushButton { border: none; color: #888; font-size: 16px; }"
                              "QPushButton:hover { color: #d4d4d4; }");
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(headerLayout);

    m_groupNameLabel = new QLabel(this);
    m_groupNameLabel->setStyleSheet("color: #4ec9b0; font-size: 13px;");
    mainLayout->addWidget(m_groupNameLabel);

    QFrame* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #3a3a5c;");
    mainLayout->addWidget(sep);

    QWidget* createSection = new QWidget(this);
    QVBoxLayout* createLayout = new QVBoxLayout(createSection);
    createLayout->setContentsMargins(0, 0, 0, 0);
    createLayout->setSpacing(6);

    m_voteTitleEdit = new QLineEdit(createSection);
    m_voteTitleEdit->setPlaceholderText(tr("Vote title..."));
    m_voteTitleEdit->setStyleSheet("QLineEdit { background: #2a2a3e; border: 1px solid #3a3a5c; "
                                   "border-radius: 4px; padding: 6px; color: #d4d4d4; font-size: 12px; }");
    createLayout->addWidget(m_voteTitleEdit);

    QHBoxLayout* optionLayout = new QHBoxLayout();
    m_optionEdit = new QLineEdit(createSection);
    m_optionEdit->setPlaceholderText(tr("Add option..."));
    m_optionEdit->setStyleSheet("QLineEdit { background: #2a2a3e; border: 1px solid #3a3a5c; "
                                "border-radius: 4px; padding: 6px; color: #d4d4d4; font-size: 12px; }");
    m_addOptionBtn = new QPushButton(tr("+"), createSection);
    m_addOptionBtn->setFixedSize(32, 32);
    m_addOptionBtn->setStyleSheet("QPushButton { background: #388a34; color: white; border-radius: 16px; "
                                  "font-size: 16px; }QPushButton:hover { background: #45a741; }");
    optionLayout->addWidget(m_optionEdit);
    optionLayout->addWidget(m_addOptionBtn);
    createLayout->addLayout(optionLayout);

    m_createVoteBtn = new QPushButton(tr("Create Vote"), createSection);
    m_createVoteBtn->setStyleSheet("QPushButton { background: #0e639c; color: white; padding: 6px 12px; "
                                   "border-radius: 4px; font-size: 12px; }"
                                   "QPushButton:hover { background: #1177bb; }");
    createLayout->addWidget(m_createVoteBtn);
    mainLayout->addWidget(createSection);

    m_voteList = new QListWidget(this);
    m_voteList->setStyleSheet(
        "QListWidget { background: #2a2a3e; border: none; border-radius: 6px; color: #d4d4d4; }"
        "QListWidget::item { padding: 10px; border-bottom: 1px solid #3a3a5c; }"
        "QListWidget::item:hover { background: #3a3a5c; }");
    connect(m_voteList, &QListWidget::itemClicked, this, &GroupVoteWidget::onVoteClicked);
    mainLayout->addWidget(m_voteList);

    m_detailWidget = new QWidget(this);
    QVBoxLayout* detailLayout = new QVBoxLayout(m_detailWidget);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(8);

    m_detailTitle = new QLabel(m_detailWidget);
    m_detailTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #e0e0e0;");
    detailLayout->addWidget(m_detailTitle);

    m_detailOptions = new QListWidget(m_detailWidget);
    m_detailOptions->setStyleSheet(
        "QListWidget { background: #2a2a3e; border: none; border-radius: 6px; color: #d4d4d4; }"
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #3a3a5c; }"
        "QListWidget::item:hover { background: #3a3a5c; }");
    detailLayout->addWidget(m_detailOptions);

    QHBoxLayout* detailBtnLayout = new QHBoxLayout();
    m_voteBtn = new QPushButton(tr("Vote"), m_detailWidget);
    m_voteBtn->setStyleSheet("QPushButton { background: #388a34; color: white; padding: 6px 16px; "
                             "border-radius: 4px; font-size: 12px; }"
                             "QPushButton:hover { background: #45a741; }");
    m_backBtn = new QPushButton(tr("Back"), m_detailWidget);
    m_backBtn->setStyleSheet("QPushButton { background: #555; color: white; padding: 6px 16px; "
                             "border-radius: 4px; font-size: 12px; }"
                             "QPushButton:hover { background: #666; }");
    detailBtnLayout->addWidget(m_voteBtn);
    detailBtnLayout->addStretch();
    detailBtnLayout->addWidget(m_backBtn);
    detailLayout->addLayout(detailBtnLayout);
    m_detailWidget->hide();
    mainLayout->addWidget(m_detailWidget);

    connect(m_addOptionBtn, &QPushButton::clicked, this, &GroupVoteWidget::onAddOptionClicked);
    connect(m_createVoteBtn, &QPushButton::clicked, this, &GroupVoteWidget::onCreateClicked);
    connect(m_voteBtn, &QPushButton::clicked, this, &GroupVoteWidget::onVoteButtonClicked);
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        m_detailWidget->hide();
        m_voteList->show();
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() { emit closed(); hide(); });
}

void GroupVoteWidget::setGroupId(const QString& groupId) { m_groupId = groupId; }
void GroupVoteWidget::setGroupName(const QString& groupName) {
    m_groupName = groupName;
    m_groupNameLabel->setText(tr("Group: %1").arg(groupName));
}
void GroupVoteWidget::setCurrentUserId(const QString& userId) { m_currentUserId = userId; }

void GroupVoteWidget::addVote(const GroupVote& vote) {
    m_votes.prepend(vote);
    m_voteOptions[vote.voteTitle] = vote.options;
    refreshList();
}

void GroupVoteWidget::addVoteOption(const QString& voteTitle, const QString& option, int voteCount) {
    m_voteResults[voteTitle][option] = voteCount;
}

void GroupVoteWidget::setVotes(const QList<GroupVote>& votes) {
    m_votes = votes;
    for (const auto& v : votes)
        m_voteOptions[v.voteTitle] = v.options;
    refreshList();
}

void GroupVoteWidget::refreshList() {
    m_voteList->clear();
    for (const auto& v : m_votes) {
        int totalVotes = 0;
        if (m_voteResults.contains(v.voteTitle)) {
            for (auto it = m_voteResults[v.voteTitle].begin(); it != m_voteResults[v.voteTitle].end(); ++it)
                totalVotes += it.value();
        }
        QString text = QString("<b>\xf0\x9f\x91\x8d %1</b><br>"
                               "<span style='color:#888;'>%2 options | %3 votes</span>")
            .arg(v.voteTitle.toHtmlEscaped())
            .arg(v.options.size())
            .arg(totalVotes);
        QListWidgetItem* item = new QListWidgetItem(text, m_voteList);
        item->setData(Qt::UserRole, v.voteTitle);
        item->setSizeHint(QSize(0, 56));
    }
}

void GroupVoteWidget::onVoteClicked(QListWidgetItem* item) {
    QString voteTitle = item->data(Qt::UserRole).toString();
    for (const auto& v : m_votes) {
        if (v.voteTitle == voteTitle) {
            showVoteDetail(v);
            break;
        }
    }
}

void GroupVoteWidget::showVoteDetail(const GroupVote& vote) {
    m_currentVoteTitle = vote.voteTitle;
    m_voteList->hide();
    m_detailWidget->show();

    m_detailTitle->setText(vote.voteTitle);
    m_detailOptions->clear();

    int totalVotes = 0;
    if (m_voteResults.contains(vote.voteTitle)) {
        for (auto it = m_voteResults[vote.voteTitle].begin(); it != m_voteResults[vote.voteTitle].end(); ++it)
            totalVotes += it.value();
    }

    for (const QString& option : vote.options) {
        int count = m_voteResults.contains(vote.voteTitle) ?
                    m_voteResults[vote.voteTitle].value(option, 0) : 0;
        double pct = totalVotes > 0 ? (100.0 * count / totalVotes) : 0;
        QString text = QString("%1 <b>%2</b> (%3 votes, %4%)")
            .arg(option.toHtmlEscaped())
            .arg(QString::number(pct, 'f', 0))
            .arg(count)
            .arg(QString::number(pct, 'f', 1));
        QListWidgetItem* item = new QListWidgetItem(text, m_detailOptions);
        item->setData(Qt::UserRole, option);
    }
}

void GroupVoteWidget::onCreateClicked() {
    QString title = m_voteTitleEdit->text().trimmed();
    if (title.isEmpty()) return;

    GroupVote vote;
    vote.groupId = m_groupId;
    vote.voteTitle = title;
    vote.creatorId = m_currentUserId;
    vote.timestamp = QDateTime::currentMSecsSinceEpoch();

    if (m_voteOptions.contains(title))
        vote.options = m_voteOptions[title];

    emit voteCreated(m_groupId, vote);
    m_voteTitleEdit->clear();
    m_optionEdit->clear();
}

void GroupVoteWidget::onAddOptionClicked() {
    QString option = m_optionEdit->text().trimmed();
    if (option.isEmpty()) return;
    QString title = m_voteTitleEdit->text().trimmed();
    if (title.isEmpty()) title = "New Vote";
    m_voteOptions[title].append(option);
    m_optionEdit->clear();
}

void GroupVoteWidget::onVoteButtonClicked() {
    QListWidgetItem* item = m_detailOptions->currentItem();
    if (!item) return;
    QString option = item->data(Qt::UserRole).toString();
    emit voteSubmitted(m_groupId, m_currentVoteTitle, option);
}

} // namespace xrk
