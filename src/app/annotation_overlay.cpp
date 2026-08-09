#include "annotation_overlay.h"

#include <QPainter>
#include <QPainterPath>
#include <QGuiApplication>
#include <QScreen>

namespace xrk {

AnnotationOverlay::AnnotationOverlay(QWidget* parent) : QWidget(parent) {
    // Input-pass-through: the local user must still be able to use the machine
    // while the controller's guidance strokes float on top.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   Qt::Tool | Qt::WindowTransparentForInput | Qt::BypassWindowManagerHint |
                   Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet("background: transparent;");

    // Cover the entire virtual desktop (all physical monitors combined) so the
    // strokes can be mapped 1:1 from captured-frame coordinates.
    QRect screenGeom;
    for (QScreen* screen : QGuiApplication::screens()) {
        screenGeom = screenGeom.united(screen->geometry());
    }
    setGeometry(screenGeom);
}

void AnnotationOverlay::setStrokes(const AnnotationUpdate& update) {
    m_update = update;
    QWidget::update();
}

void AnnotationOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int fw = m_update.frameWidth;
    const int fh = m_update.frameHeight;
    if (fw <= 0 || fh <= 0) return;

    const double sx = width() / static_cast<double>(fw);
    const double sy = height() / static_cast<double>(fh);

    for (const AnnotationStroke& st : m_update.strokes) {
        if (st.points.isEmpty()) continue;
        QPen pen(st.color, st.width);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);

        if (st.points.size() == 1) {
            painter.drawPoint(QPointF(st.points.first().x() * sx, st.points.first().y() * sy));
            continue;
        }
        QPainterPath path;
        path.moveTo(st.points.first().x() * sx, st.points.first().y() * sy);
        for (int i = 1; i < st.points.size(); ++i) {
            path.lineTo(st.points.at(i).x() * sx, st.points.at(i).y() * sy);
        }
        painter.drawPath(path);
    }
}

} // namespace xrk
