#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMap>
#include <QColor>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QDateTime>

#include "app/ipmsg_manager.h"

namespace xrk {

class GroupStatisticsWidget : public QWidget {
    Q_OBJECT

public:
    explicit GroupStatisticsWidget(QWidget* parent = nullptr);
    ~GroupStatisticsWidget();

    void setGroup(const IPMsgGroup& group);
    void setManager(IPMsgManager* manager);
    void updateRealTimeStats();
    void startAnimation();
    void stopAnimation();
    void setUpdateInterval(int milliseconds);
    void enableAnimations(bool enabled);

signals:
    void statisticsUpdated();
    void memberJoined(const QString& memberId);
    void memberLeft(const QString& memberId);
    void roleChanged(const QString& memberId, int newRole);
    void anomalyDetected(const QString& type, const QString& message);

private:
    void setupStatsUI();
    void createStatItems();
    void updateMemberStats();
    void updateActivityStats();
    void updateCommunicationStats();
    void animateValueChange(QWidget* widget, const QString& newValue,
                           const QString& oldValue, const QColor& color = Qt::white);
    void setupAutoUpdateTimer();
    void createAnimatedStatItem(const QString& name, const QString& value,
                               const QString& icon, const QColor& color,
                               int index = 0);
    void updateGradientBackground();
    void createActivityChart();
    void createMemberDistribution();
    void createPerformanceMetrics();
    void createPredictiveInsights();
    void updateMemberData();
    void calculateEngagementScores();
    void detectActivityAnomalies();

    QWidget* createDistributionItem(const QString& icon, const QString& label,
                                   const QString& value, const QColor& color);
    QWidget* createMetricItem(const QString& icon, const QString& label,
                             const QString& value, const QColor& color);
    QString getAvatarColor(const QString& name) const;

    struct StatItem {
        QString name;
        QString value;
        QString icon;
        QColor color;
        QString suffix;
        QProgressBar* progressBar;
        QLabel* valueLabel;
        QPropertyAnimation* animation;
    };

    struct GroupStats {
        int totalMembers = 0;
        int onlineMembers = 0;
        int offlineMembers = 0;
        int admins = 0;
        int regularMembers = 0;
        qint64 lastActivity = 0;
        int messagesSent = 0;
        int filesShared = 0;
        double avgSessionTime = 0.0;
        QMap<QString, int> memberActivity;
        QMap<int, int> ageDistribution;
        QMap<QString, int> activityLevels;
    };

    struct MemberActivityData {
        QString memberId;
        QString memberName;
        QString avatarColor;
        qint64 lastActive = 0;
        int messagesSent = 0;
        int filesShared = 0;
        int screenTime = 0;
        double engagementScore = 0.0;
        QMap<QString, int> activityByHour;
    };

    // UI Components
    QVBoxLayout* m_statsLayout = nullptr;
    QHBoxLayout* m_statsRow1 = nullptr;
    QHBoxLayout* m_statsRow2 = nullptr;
    QHBoxLayout* m_statsRow3 = nullptr;
    QLabel* m_groupTitleLabel = nullptr;
    QLabel* m_groupSubtitleLabel = nullptr;
    QProgressBar* m_onlineProgressBar = nullptr;
    QLabel* m_onlineCountLabel = nullptr;
    QLabel* m_offlineCountLabel = nullptr;
    QLabel* m_totalMembersLabel = nullptr;
    QWidget* m_activityChartWidget = nullptr;
    QWidget* m_memberDistributionWidget = nullptr;
    QWidget* m_performanceMetricsWidget = nullptr;
    QWidget* m_insightsWidget = nullptr;

    // Animation and effects
    QTimer* m_updateTimer = nullptr;
    QPropertyAnimation* m_pulseAnimation = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;

    // Data management
    IPMsgGroup m_group;
    GroupStats m_stats;
    IPMsgManager* m_manager = nullptr;
    QMap<QString, QLabel*> m_statLabels;
    QMap<QString, MemberActivityData> m_memberData;

    // Animation states
    bool m_animationEnabled = true;
    int m_animationSpeed = 200;
    QColor m_gradientStartColor = QColor("#07C160");
    QColor m_gradientEndColor = QColor("#1E88E5");
};

} // namespace xrk