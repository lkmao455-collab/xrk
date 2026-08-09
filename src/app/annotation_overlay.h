#pragma once

#include <QWidget>
#include "core/types.h"

namespace xrk {

// Transparent, input-pass-through overlay shown on the controlled machine's
// physical screens to display the controller's live annotation strokes.
class AnnotationOverlay : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationOverlay(QWidget* parent = nullptr);

    // Replace the displayed stroke set. frameWidth/Height come from the
    // controller's captured frame so we can scale strokes to the physical
    // screen (v1 assumes frame == primary monitor native resolution).
    void setStrokes(const AnnotationUpdate& update);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    AnnotationUpdate m_update;
};

} // namespace xrk
