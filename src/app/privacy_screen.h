#pragma once

#include <QObject>
#include <QWidget>
#include <QLabel>

namespace xrk {

class PrivacyScreen : public QObject {
    Q_OBJECT
public:
    explicit PrivacyScreen(QObject* parent = nullptr);
    ~PrivacyScreen();

    void show();
    void hide();
    bool isVisible() const;

private:
    void setupOverlay();

    QWidget* m_overlay = nullptr;
    QLabel* m_label = nullptr;
    bool m_visible = false;
};

} // namespace xrk
