#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDateEdit>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>

namespace xrk {

class ChatSearchWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(float animOpacity READ animOpacity WRITE setAnimOpacity)

public:
    explicit ChatSearchWidget(QWidget* parent = nullptr);
    ~ChatSearchWidget();

    void activate();
    void deactivate();
    bool isActive() const { return m_active; }
    float animOpacity() const { return m_animOpacity; }
    void setAnimOpacity(float v);

    QDateTime fromDate() const;
    QDateTime toDate() const;
    bool hasDateFilter() const;

signals:
    void searchNext(const QString& keyword);
    void searchPrev(const QString& keyword);
    void searchClose();
    void searchChanged(const QString& keyword);

private:
    void setupUI();
    void animateShow();
    void animateHide();
    bool eventFilter(QObject* obj, QEvent* event) override;

    QLineEdit* m_input = nullptr;
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QLabel* m_countLabel = nullptr;
    QDateEdit* m_fromDate = nullptr;
    QDateEdit* m_toDate = nullptr;
    QCheckBox* m_dateFilterCheck = nullptr;

    bool m_active = false;
    float m_animOpacity = 0.0f;
    QPropertyAnimation* m_showAnim = nullptr;
    QPropertyAnimation* m_hideAnim = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
};

} // namespace xrk
