#include "Widgets.h"
#include "ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionToolButton>
#include <QApplication>
#include <QMenu>
#include <QPropertyAnimation>
#include <QEnterEvent>

static QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

AnimatedToolButton::AnimatedToolButton(QWidget* parent) : QToolButton(parent) {
    setAutoRaise(true);
    setAttribute(Qt::WA_Hover, true);
    m_anim.setDuration(120);
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){ m_level = v.toReal(); update(); });
}
void AnimatedToolButton::animateTo(qreal v) {
    m_anim.stop(); m_anim.setStartValue(m_level); m_anim.setEndValue(v); m_anim.start();
}
void AnimatedToolButton::enterEvent(QEnterEvent* e) { if (isEnabled()) animateTo(1.0); QToolButton::enterEvent(e); }
void AnimatedToolButton::leaveEvent(QEvent* e) { animateTo(0.0); QToolButton::leaveEvent(e); }

void AnimatedToolButton::paintBackground(QPainter& p) {
    const auto& pal = ThemeManager::current();
    QColor hover(m_hoverColor.isEmpty() ? pal.surface : m_hoverColor);
    QColor bg = hover; bg.setAlphaF(hover.alphaF() * m_level);
    if (isDown() || isChecked()) bg = m_hoverColor.isEmpty() ? QColor(pal.surfaceHover) : hover.darker(110);
    if (bg.alpha() == 0) return;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(bg);
    if (m_radius > 0) p.drawRoundedRect(QRectF(rect()), m_radius, m_radius);
    else p.drawRect(rect());
}

void AnimatedToolButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintBackground(p);
    // icon / text
    QStyleOptionToolButton opt; initStyleOption(&opt);
    QIcon::Mode mode = isEnabled() ? QIcon::Normal : QIcon::Disabled;
    if (!icon().isNull()) {
        const QSize is = iconSize();
        QPixmap pm = icon().pixmap(is, devicePixelRatioF(), mode);
        p.drawPixmap(QRect(QPoint((width() - is.width()) / 2, (height() - is.height()) / 2), is), pm);
    } else if (!text().isEmpty()) {
        p.setPen(QColor(ThemeManager::current().text));
        p.drawText(rect(), Qt::AlignCenter, text());
    }
    if (hasFocus() && focusPolicy() != Qt::NoFocus && opt.state & QStyle::State_HasFocus && opt.state & QStyle::State_KeyboardFocusChange) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(ThemeManager::current().accent), 2)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), m_radius, m_radius);
    }
}

WindowButton::WindowButton(Kind k, QWidget* parent) : AnimatedToolButton(parent), m_kind(k) {
    setFixedSize(46, 40); setRadius(0); setFocusPolicy(Qt::NoFocus);
    if (k == Close) setHoverColor(QStringLiteral("#E81123"));
    setToolTip(k == Minimize ? QStringLiteral("Minimize") : k == Maximize ? QStringLiteral("Maximize") : QStringLiteral("Close"));
}
void WindowButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintBackground(p);
    const auto& pal = ThemeManager::current();
    QColor fg = (m_kind == Close && (hoverLevel() > 0.5 || isDown())) ? QColor(Qt::white) : QColor(pal.text);
    // Icon pack glyphs (window-minimize / window-maximize / window-restore / close), 16 px centred.
    const QString name = m_kind == Minimize ? QStringLiteral("window-minimize")
                       : m_kind == Close ? QStringLiteral("close")
                       : (m_max ? QStringLiteral("window-restore") : QStringLiteral("window-maximize"));
    const QPixmap pm = ThemeManager::icon(name, fg.name(), 16).pixmap(QSize(16, 16), devicePixelRatioF());
    p.drawPixmap(QPointF((width() - 16) / 2.0, (height() - 16) / 2.0), pm);
}

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true); setCursor(Qt::PointingHandCursor); setFixedSize(36, 20);
    m_anim.setDuration(120); m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){ m_pos = v.toReal(); update(); });
}
void ToggleSwitch::checkStateSet() { m_anim.stop(); m_pos = isChecked() ? 1.0 : 0.0; update(); }
void ToggleSwitch::nextCheckState() {
    QAbstractButton::nextCheckState();
    m_anim.stop(); m_anim.setStartValue(m_pos); m_anim.setEndValue(isChecked() ? 1.0 : 0.0); m_anim.start();
}
void ToggleSwitch::paintEvent(QPaintEvent*) {
    const auto& pal = ThemeManager::current();
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(mix(QColor(pal.disabled), QColor(pal.accent), m_pos));
    p.drawRoundedRect(QRectF(0, 0, 36, 20), 10, 10);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(2 + 16 * m_pos, 2, 16, 16));
}

SwatchButton::SwatchButton(const QString& color, QWidget* parent) : QAbstractButton(parent), m_color(color) {
    setCheckable(true); setFixedSize(30, 30); setCursor(Qt::PointingHandCursor); setToolTip(color);
}
void SwatchButton::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(QColor(m_color));
    p.drawEllipse(QRectF(4, 4, 22, 22));
    if (isChecked()) { p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(ThemeManager::current().text), 2)); p.drawEllipse(QRectF(1, 1, 28, 28)); }
}

namespace Pacitum {
QIcon spinnerIcon(int angle, const QString& color) {
    const int px = 32;
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(color), 4.0); pen.setCapStyle(Qt::RoundCap); p.setPen(pen);
    p.drawArc(QRectF(4, 4, px - 8, px - 8), -angle * 16, 270 * 16);
    p.end();
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}

namespace {
class MenuFader : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) {
            if (auto* m = qobject_cast<QMenu*>(o)) {
                m->setWindowOpacity(0.0);
                auto* a = new QPropertyAnimation(m, "windowOpacity", m);
                a->setDuration(120); a->setStartValue(0.0); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::OutCubic);
                a->start(QAbstractAnimation::DeleteWhenStopped);
            }
        }
        return false;
    }
};
}
void installMenuFader() {
    static MenuFader* f = nullptr;
    if (!f) { f = new MenuFader(qApp); qApp->installEventFilter(f); }
}
}
