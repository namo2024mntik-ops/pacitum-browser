#include "Widgets.h"
#include "ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionToolButton>
#include <QApplication>
#include <QMenu>
#include <QPropertyAnimation>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QLibrary>
#include <cstdlib>

static QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

AnimatedToolButton::AnimatedToolButton(QWidget* parent) : QToolButton(parent) {
    setAutoRaise(true);
    setAttribute(Qt::WA_Hover, true);
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){ m_level = v.toReal(); update(); });
}
void AnimatedToolButton::animateTo(qreal v) {
    // spec §3/§6: hover in 120 ms, out 160 ms, OutCubic; 0 with reduced motion
    const int ms = Pacitum::motionMs(v > m_level ? 120 : 160);
    m_anim.stop();
    if (ms == 0) { m_level = v; update(); return; }
    m_anim.setDuration(ms); m_anim.setStartValue(m_level); m_anim.setEndValue(v); m_anim.start();
}
void AnimatedToolButton::enterEvent(QEnterEvent* e) { if (isEnabled()) animateTo(1.0); QToolButton::enterEvent(e); }
void AnimatedToolButton::leaveEvent(QEvent* e) { if (!m_forcedHover) animateTo(0.0); QToolButton::leaveEvent(e); }

void AnimatedToolButton::paintBackground(QPainter& p) {
    const auto& pal = ThemeManager::current();
    QColor hover(m_hoverColor.isEmpty() ? pal.surface : m_hoverColor);
    QColor bg = hover; bg.setAlphaF(hover.alphaF() * m_level);
    if (!isEnabled()) return;
    if (isDown() || isChecked() || m_forcedDown) bg = m_hoverColor.isEmpty() ? QColor(pal.pressed) : hover.darker(110);
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
        QColor ring(ThemeManager::current().accent); ring.setAlphaF(0.55);
        p.setPen(QPen(ring, 2)); p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), qMax(0, m_radius - 1), qMax(0, m_radius - 1));
    }
}

WindowButton::WindowButton(Kind k, QWidget* parent) : AnimatedToolButton(parent), m_kind(k) {
    setFixedSize(46, 42); setRadius(0);
    setFocusPolicy(Qt::TabFocus);   // reachable with Tab; Space/Enter activates
    const QString name = k == Minimize ? QStringLiteral("Minimize") : k == Maximize ? QStringLiteral("Maximize") : QStringLiteral("Close");
    setToolTip(name); setAccessibleName(name);
}
void WindowButton::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { animateClick(); return; }
    AnimatedToolButton::keyPressEvent(e);
}
void WindowButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto& pal = ThemeManager::current();
    const bool down = isDown() || m_forcedDown;
    const qreal lvl = hoverLevel();
    // Windows caption-button states: hover = surface / #E81123 (close), pressed = surfaceHover / #F1707A (close)
    QColor bg;
    if (m_kind == Close) { bg = down ? QColor(0xF1, 0x70, 0x7A) : QColor(0xE8, 0x11, 0x23); if (!down) bg.setAlphaF(lvl); }
    else { bg = down ? QColor(pal.pressed) : QColor(pal.surface); if (!down) bg.setAlphaF(bg.alphaF() * lvl); }
    if (bg.alpha() > 0) p.fillRect(rect(), bg);
    const bool inactive = window() && !window()->isActiveWindow();
    QColor fg = (m_kind == Close && (lvl > 0.5 || down)) ? QColor(Qt::white) : QColor(inactive ? pal.textSecondary : pal.text);
    // Icon pack glyphs (window-minimize / window-maximize / window-restore / close), 16 px centred.
    const QString name = m_kind == Minimize ? QStringLiteral("window-minimize")
                       : m_kind == Close ? QStringLiteral("close")
                       : (m_max ? QStringLiteral("window-restore") : QStringLiteral("window-maximize"));
    const int gs = m_kind == Maximize ? 14 : 16;   // spec §4.2: maximise/restore glyph 14, others 16
    const QPixmap pm = ThemeManager::icon(name, fg.name(), gs).pixmap(QSize(gs, gs), devicePixelRatioF());
    p.drawPixmap(QPointF((width() - gs) / 2.0, (height() - gs) / 2.0), pm);
    if (hasFocus()) {   // keyboard focus ring (Tab only, see focusPolicy)
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(pal.accent), 2)); p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }
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
// Translucent popups need a compositor. Windows/macOS/Wayland always composite; on X11 ask the
// server whether a compositing manager owns _NET_WM_CM_S<n> (libxcb is resolved at runtime, it is
// already loaded by Qt's xcb platform plugin, so there is no new build dependency).
static bool canCompositePopups() {
#if defined(Q_OS_LINUX)
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    cached = 1;
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        cached = 0;
        if (auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
            QLibrary xcb(QStringLiteral("xcb"), 1);
            struct Cookie { unsigned int sequence; };
            struct AtomReply { quint8 rt, pad; quint16 seq; quint32 len; quint32 atom; };
            struct OwnerReply { quint8 rt, pad; quint16 seq; quint32 len; quint32 owner; };
            using InternFn = Cookie (*)(void*, quint8, quint16, const char*);
            using InternReplyFn = AtomReply* (*)(void*, Cookie, void**);
            using OwnerFn = Cookie (*)(void*, quint32);
            using OwnerReplyFn = OwnerReply* (*)(void*, Cookie, void**);
            auto intern = reinterpret_cast<InternFn>(xcb.resolve("xcb_intern_atom"));
            auto internReply = reinterpret_cast<InternReplyFn>(xcb.resolve("xcb_intern_atom_reply"));
            auto owner = reinterpret_cast<OwnerFn>(xcb.resolve("xcb_get_selection_owner"));
            auto ownerReply = reinterpret_cast<OwnerReplyFn>(xcb.resolve("xcb_get_selection_owner_reply"));
            if (intern && internReply && owner && ownerReply && x11->connection()) {
                void* c = x11->connection();
                const QByteArray name = "_NET_WM_CM_S0";
                if (AtomReply* a = internReply(c, intern(c, 0, quint16(name.size()), name.constData()), nullptr)) {
                    if (OwnerReply* o = ownerReply(c, owner(c, a->atom), nullptr)) { cached = o->owner ? 1 : 0; free(o); }
                    free(a);
                }
            }
        }
    }
    return cached == 1;
#else
    return true;
#endif
}

PopupMenu::PopupMenu(QWidget* parent) : QMenu(parent) {
    m_shadow = canCompositePopups();
    if (!m_shadow) return;   // spec fallback: plain card with 1 px outline (app QSS), no shadow
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QStringLiteral("QMenu{background:transparent;border:none;margin:%1px;padding:6px;}").arg(kShadow));
}
void PopupMenu::paintEvent(QPaintEvent* e) {
    if (m_shadow) {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const auto& pal = ThemeManager::current();
        const QRectF card = QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow);
        const bool dark = QColor(pal.toolbar).lightness() < 128;
        // fake 0 16px 40px blur: stacked rounded rects, offset downwards, decreasing alpha
        const int layers = kShadow;
        const qreal peak = dark ? 0.55 : 0.16;
        p.setPen(Qt::NoPen);
        for (int i = layers; i >= 1; --i) {
            const qreal t = qreal(i) / layers;
            QColor c = dark ? QColor(0, 0, 0) : QColor(0x10, 0x18, 0x28);
            c.setAlphaF(peak * 0.11 * (1.0 - t) * (1.0 - t) + 0.002);
            const qreal grow = i * 1.0;
            p.setBrush(c);
            p.drawRoundedRect(card.adjusted(-grow, -grow * 0.5, grow, grow).translated(0, qMin<qreal>(6, i * 0.5)), 14 + grow, 14 + grow);
        }
        p.setBrush(QColor(pal.toolbar));
        p.setPen(QPen(QColor(pal.border), 1));
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    }
    QMenu::paintEvent(e);
}

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
                if (Pacitum::reducedMotion()) return false;
                m->setWindowOpacity(0.0);
                auto* a = new QPropertyAnimation(m, "windowOpacity", m);
                const int ms = Pacitum::motionMs(140);   // spec §5.5: menu open 140 ms OutCubic (opacity only)
                if (ms == 0) return false;
                a->setDuration(ms); a->setStartValue(0.0); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::OutCubic);
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

// ---------------------------------------------------------------------------
InfoBar::InfoBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("InfoBarWrap"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QHBoxLayout(this); outer->setContentsMargins(8, 8, 8, 8); outer->setSpacing(0);
    m_card = new QWidget(this); m_card->setObjectName(QStringLiteral("InfoBar"));
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    m_card->setFixedHeight(44);
    outer->addWidget(m_card);
    auto* h = new QHBoxLayout(m_card); h->setContentsMargins(14, 0, 6, 0); h->setSpacing(10);
    m_icon = new QLabel(m_card); m_icon->setFixedSize(18, 18);
    m_text = new QLabel(m_card); m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_secondary = new QPushButton(m_card); m_secondary->hide(); m_secondary->setCursor(Qt::PointingHandCursor);
    m_primary = new QPushButton(m_card); m_primary->setObjectName(QStringLiteral("PrimaryButton")); m_primary->setCursor(Qt::PointingHandCursor);
    m_primary->setIconSize(QSize(16, 16)); m_primary->hide();
    m_close = new QToolButton(m_card); m_close->setFixedSize(30, 30); m_close->setIconSize(QSize(16, 16));
    m_close->setToolTip(QStringLiteral("Close")); m_close->setAccessibleName(QStringLiteral("Close"));
    h->addWidget(m_icon); h->addWidget(m_text); h->addStretch(1);
    h->addWidget(m_secondary); h->addWidget(m_primary); h->addWidget(m_close);
    connect(m_primary, &QPushButton::clicked, this, &InfoBar::primaryClicked);
    connect(m_secondary, &QPushButton::clicked, this, &InfoBar::secondaryClicked);
    connect(m_close, &QToolButton::clicked, this, [this]{ hideAnimated(); emit closed(); });
    setMaximumHeight(0); hide();
}
void InfoBar::setMessage(const QString& t) { m_text->setText(t); }
void InfoBar::setPrimary(const QString& t, const QString& icon) { m_primary->setText(t); m_primaryIcon = icon; m_primary->setVisible(!t.isEmpty()); refresh(); }
void InfoBar::setSecondary(const QString& t) { m_secondary->setText(t); m_secondary->setVisible(!t.isEmpty()); }
void InfoBar::refresh() {
    const auto& p = ThemeManager::current();
    const bool warn = m_kind == Warning;
    m_icon->setPixmap(ThemeManager::icon(warn ? QStringLiteral("warning") : QStringLiteral("info"), warn ? p.warning : p.accent, 18)
                      .pixmap(QSize(18, 18), devicePixelRatioF()));
    m_close->setIcon(ThemeManager::icon(QStringLiteral("close"), p.textSecondary, 16));
    if (!m_primaryIcon.isEmpty()) m_primary->setIcon(ThemeManager::icon(m_primaryIcon, p.onAccent, 16));
}
void InfoBar::animateTo(int h, bool hideAtEnd) {
    if (m_anim) { m_anim->stop(); m_anim->deleteLater(); m_anim = nullptr; }
    const int ms = Pacitum::motionMs(160);
    if (ms == 0) { setMaximumHeight(h); setVisible(!hideAtEnd); return; }
    m_anim = new QPropertyAnimation(this, "maximumHeight", this);
    m_anim->setDuration(ms); m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->setStartValue(maximumHeight()); m_anim->setEndValue(h);
    connect(m_anim, &QPropertyAnimation::finished, this, [this, hideAtEnd]{ if (hideAtEnd) hide(); });
    m_anim->start();
}
void InfoBar::showAnimated() { refresh(); if (isVisible() && maximumHeight() >= 60) return; setMaximumHeight(0); show(); animateTo(60, false); }
void InfoBar::hideAnimated() { if (!isVisible()) return; animateTo(0, true); }
