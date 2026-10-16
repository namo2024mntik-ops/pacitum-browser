#pragma once
// Shared motion helpers (1.7.0). Timings follow the shared Pacitum motion tokens
// (/workspace/pacitum-animations/motion-tokens.css) so Qt matches the Firefox build.
// Every helper is instant when reduced motion is on (app setting, PACITUM_REDUCED_MOTION, or the
// Windows "Show animations in Windows" setting). Only small, raster child widgets or short-lived
// top-level ghosts are animated: never a widget that hosts a web view.
#include <QPointer>
class QWidget;
class QLabel;
class QString;

namespace Motion {
enum : int { Instant = 80, Fast = 120, Quick = 140, Base = 160, Exit = 180, Medium = 200, Enter = 240, Slow = 280 };
int ms(int token);                 // movement: 0 with reduced motion
int fadeMs(int token);             // opacity fades: capped at 120 ms with reduced motion
bool enabled();
bool targetVisible(QWidget* w);   // visibility a running slide is heading to
// Opacity 0→1 on a child widget (temporary QGraphicsOpacityEffect, removed at the end).
void fadeIn(QWidget* w, int token = Base);
// Slide a widget in/out by animating maximumHeight (0 ↔ target), hiding at the end of an exit.
// target <= 0 uses sizeHint().height().
void slideVisible(QWidget* w, bool show, int target = 0, int token = Base);
// Short "pop" for a small label: fades in from 0.4 (zoom percentage etc.).
void pop(QWidget* w);
// Fade (and gently shrink) a snapshot of a top-level that is being hidden. The snapshot is a
// separate click-through window that lives ≤ 180 ms, so it never blocks input or follows resizes.
void ghostOut(QWidget* topLevel, int token, qreal scaleTo);
// Profile card style hover: lift a child widget by dy px (animating pos); restores on leave.
void installHoverLift(QWidget* w, int dy = -3);
// Centred pill near the top of a window: fade in 160, hold, fade out 140 (zoom 900 hold = 1.2 s,
// fullscreen hint ~1.7 s hold = 2 s). Follows window resizes; click-through.
void toast(QWidget* w, bool show);
void bubble(QWidget* host, const QString& text, int holdMs, int topY);
}
