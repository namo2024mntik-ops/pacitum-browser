#include "TabWidget.h"
#include "ThemeManager.h"
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QResizeEvent>
#include "Widgets.h"
#include <QProxyStyle>
#include <QStyleOptionTab>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>

// ---------------------------------------------------------------------------
// Floating-tab style: Qt's QTabBar keeps doing layout, drag-reorder (with its own
// neighbour slide animation) and scrolling; only the tab painting is replaced.
namespace {
QColor qc(const QString& s) {
    if (s.startsWith(QLatin1String("rgba("))) {
        const auto parts = s.mid(5, s.size() - 6).split(QLatin1Char(','));
        if (parts.size() == 4) return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), qRound(parts[3].toDouble() * 255));
    }
    return QColor(s);
}
QColor mixc(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}
constexpr int kMinStripTabs = 60;
constexpr int kGap = 4, kTabH = 32, kTop = 5, kMaxW = 232, kMinW = 56, kPinW = 40, kPinSep = 9;

class FloatingTabStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawControl(ControlElement ce, const QStyleOption* opt, QPainter* p, const QWidget* w) const override {
        if (auto* bar = qobject_cast<const PacitumTabBar*>(w); bar && bar->floating()) {
            if (ce == CE_TabBarTab) { if (auto* t = qstyleoption_cast<const QStyleOptionTab*>(opt)) bar->paintFloatingTab(p, *t); return; }
            if (ce == CE_TabBarTabShape || ce == CE_TabBarTabLabel) return;
        }
        QProxyStyle::drawControl(ce, opt, p, w);
    }
    void drawPrimitive(PrimitiveElement pe, const QStyleOption* opt, QPainter* p, const QWidget* w) const override {
        if ((pe == PE_FrameTabBarBase || pe == PE_IndicatorTabTear || pe == PE_FrameFocusRect) && qobject_cast<const PacitumTabBar*>(w)) return;
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }
    QRect subElementRect(SubElement se, const QStyleOption* opt, const QWidget* w) const override {
        if (auto* bar = qobject_cast<const PacitumTabBar*>(w); bar && bar->floating()) {
            if (se == SE_TabBarTabRightButton) return bar->closeButtonRect(opt->rect);
            if (se == SE_TabBarTabLeftButton) return {};
        }
        return QProxyStyle::subElementRect(se, opt, w);
    }
    int pixelMetric(PixelMetric m, const QStyleOption* opt, const QWidget* w) const override {
        if (qobject_cast<const PacitumTabBar*>(w)) {
            switch (m) {
            case PM_TabBarTabShiftHorizontal: case PM_TabBarTabShiftVertical: case PM_TabBarTabOverlap: case PM_TabBarBaseOverlap: return 0;
            case PM_TabBarScrollButtonWidth: return 24;
            default: break;
            }
        }
        return QProxyStyle::pixelMetric(m, opt, w);
    }
    int styleHint(StyleHint h, const QStyleOption* opt, const QWidget* w, QStyleHintReturn* r) const override {
        if (h == SH_Widget_Animation_Duration && qobject_cast<const PacitumTabBar*>(w)) return Pacitum::motionMs(160);   // reorder slide
        return QProxyStyle::styleHint(h, opt, w, r);
    }
};
}

TabCloseButton::TabCloseButton(QWidget* parent) : QAbstractButton(parent) {
    setFixedSize(22, 22); setCursor(Qt::ArrowCursor); setFocusPolicy(Qt::NoFocus);
    setToolTip(QStringLiteral("Close tab")); setAccessibleName(QStringLiteral("Close tab"));
    setAttribute(Qt::WA_Hover);
}
void TabCloseButton::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto& pal = ThemeManager::current();
    if (isDown()) { p.setPen(Qt::NoPen); p.setBrush(qc(pal.pressed)); p.drawEllipse(rect()); }
    else if (underMouse()) { p.setPen(Qt::NoPen); p.setBrush(qc(pal.surfaceHover)); p.drawEllipse(rect()); }
    const QPixmap pm = ThemeManager::icon(QStringLiteral("close"), underMouse() ? pal.text : pal.textSecondary, 14).pixmap(QSize(14, 14), devicePixelRatioF());
    p.drawPixmap(QPointF(4, 4), pm);
}

// PacitumTabBar
PacitumTabBar::PacitumTabBar(bool vertical, QWidget* parent) : QTabBar(parent), m_vertical(vertical) {
    setElideMode(Qt::ElideRight); setExpanding(false); setTabsClosable(true);
    setDrawBase(false); setIconSize(QSize(16,16)); setUsesScrollButtons(true);
    if (vertical) setMinimumWidth(200);
}
void PacitumTabBar::enableFloatingTabs() {
    if (m_floating) return;
    m_floating = true;
    setTabsClosable(false);          // own 22 px close buttons, see updateCloseButtons()
    setStyle(new FloatingTabStyle(QApplication::style()));
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setElideMode(Qt::ElideNone);
    setFocusPolicy(Qt::TabFocus);
    for (int i = 0; i < count(); ++i) tabInserted(i);
    m_spin = new QVariantAnimation(this);
    m_spin->setStartValue(0.0); m_spin->setEndValue(360.0); m_spin->setDuration(800); m_spin->setLoopCount(-1);
    connect(m_spin, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_spinAngle = v.toReal();
        for (int i = 0; i < count(); ++i)                          // repaint only the 16 px favicon rects
            if (m_flags.value(idOf(i)).loading) { const QRect r = tabRect(i); update(QRect(r.x() + (isTabPinned(i) ? 10 : 10), r.y() + kTop + 6, 20, 20)); }
    });
}
void PacitumTabBar::setVerticalMode(bool v) {
    m_vertical = v;
    setMinimumWidth(v ? 200 : 0);
}
void PacitumTabBar::setTabPinned(int index, bool pinned) {
    if (pinned) m_pinned.insert(index); else m_pinned.remove(index);
    if (m_floating) { relayout(); updateCloseButtons(); }
}
quint64 PacitumTabBar::idOf(int index) const {
    return tabData(index).isValid() ? tabData(index).toULongLong() : 0;
}
int PacitumTabBar::indexOfId(quint64 id) const {
    for (int i = 0; i < count(); ++i) if (idOf(i) == id) return i;
    return -1;
}
quint64 PacitumTabBar::ensureId(int index) {
    quint64 id = idOf(index);
    if (!id) { id = m_nextId++; setTabData(index, id); }
    return id;
}
void PacitumTabBar::relayout() { setIconSize(iconSize()); updateGeometry(); }   // forces QTabBar to re-layout tabs

void PacitumTabBar::setAvailableWidth(int w) { if (w != m_avail) { m_avail = w; relayout(); } }

int PacitumTabBar::normalTabWidth() const {
    int pinned = 0;
    for (int i = 0; i < count(); ++i) if (m_pinned.contains(i)) ++pinned;
    const int normal = count() - pinned;
    if (m_frozenWidth > 0) return m_frozenWidth;
    if (normal <= 0) return kMaxW + kGap;
    const int avail = m_avail - pinned * (kPinW + kGap) - (pinned ? kPinSep : 0);
    if (m_avail <= 0) return kMaxW + kGap;
    return qBound(kMinW + kGap, avail / normal, kMaxW + kGap);
}

QSize PacitumTabBar::tabSizeHint(int index) const {
    QSize s = QTabBar::tabSizeHint(index);
    if (m_vertical || shape() != QTabBar::RoundedNorth) return s;
    int w;
    if (m_floating) {
        if (m_pinned.contains(index)) w = kPinW + kGap + (!m_pinned.contains(index + 1) && index + 1 < count() ? kPinSep : 0);
        else w = normalTabWidth();
    } else {
        w = 220;
        if (m_pinned.contains(index)) w = 40;
        else if (m_avail > 0 && count() > 0) w = qBound(72, m_avail / count(), 220);
    }
    const qreal prog = m_progress.value(idOf(index), 1.0);
    return QSize(qMax(1, int(w * prog)), m_floating ? kTop * 2 + kTabH : 32);
}
QSize PacitumTabBar::minimumTabSizeHint(int index) const { return tabSizeHint(index); }

QRect PacitumTabBar::closeButtonRect(const QRect& r) const {
    // 22 px circle, 6 px from the tab's right edge (the 4 px gap is outside the tab body)
    return QRect(r.right() - kGap - 6 - 22 + 1, r.y() + kTop + (kTabH - 22) / 2, 22, 22);
}
QRect PacitumTabBar::audioRect(int index) const {
    const QRect r = tabRect(index);
    const bool closeShown = tabButton(index, QTabBar::RightSide) && tabButton(index, QTabBar::RightSide)->isVisible();
    const int right = closeShown ? closeButtonRect(r).left() - 4 : r.right() - kGap - 8;
    return QRect(right - 16, r.y() + kTop + 8, 16, 16);
}

void PacitumTabBar::tabInserted(int index) {
    QTabBar::tabInserted(index);
    if (!m_pinned.isEmpty()) {
        QSet<int> shifted;
        for (int p : std::as_const(m_pinned)) shifted.insert(p >= index ? p + 1 : p);
        m_pinned = shifted;
    }
    if (!m_floating) return;
    ensureId(index);
    if (!tabButton(index, QTabBar::RightSide)) {
        auto* b = new TabCloseButton(this);
        connect(b, &QAbstractButton::clicked, this, [this, b] {
            for (int i = 0; i < count(); ++i)
                if (tabButton(i, QTabBar::RightSide) == b) {
                    if (underMouse()) m_frozenWidth = tabRect(i).width() > kMinW ? normalTabWidth() : 0;   // next close button stays under the cursor
                    emit tabCloseRequested(i); return;
                }
        });
        setTabButton(index, QTabBar::RightSide, b);
    }
    updateCloseButtons();
}
void PacitumTabBar::tabRemoved(int index) {
    QTabBar::tabRemoved(index);
    QSet<int> shifted;
    for (int p : std::as_const(m_pinned)) { if (p < index) shifted.insert(p); else if (p > index) shifted.insert(p - 1); }
    m_pinned = shifted;
    if (m_floating) {
        QSet<quint64> alive; for (int i = 0; i < count(); ++i) alive.insert(idOf(i));
        for (auto it = m_flags.begin(); it != m_flags.end();) it = alive.contains(it.key()) ? std::next(it) : m_flags.erase(it);
        updateSpinner(); updateCloseButtons();
    }
}
void PacitumTabBar::tabLayoutChange() { QTabBar::tabLayoutChange(); if (m_floating) updateCloseButtons(); }

void PacitumTabBar::updateCloseButtons() {
    for (int i = 0; i < count(); ++i) {
        QWidget* b = tabButton(i, QTabBar::RightSide);
        if (!b) continue;
        const int w = tabRect(i).width() - kGap;
        const bool active = i == currentIndex();
        const bool show = !m_pinned.contains(i) && (active ? w >= 56 : (idOf(i) == m_hoverId && w >= 96));
        b->setVisible(show);
    }
}

void PacitumTabBar::setHoverIndex(int i) {
    const quint64 id = i >= 0 ? idOf(i) : 0;
    if (id == m_hoverId) return;
    auto fade = [this](quint64 tid, qreal to, int ms) {
        if (!tid) return;
        if (auto* old = m_hoverAnim.take(tid)) { old->stop(); old->deleteLater(); }
        ms = Pacitum::motionMs(ms);
        if (ms == 0) { m_hover[tid] = to; update(); return; }
        auto* a = new QVariantAnimation(this);
        a->setDuration(ms); a->setEasingCurve(QEasingCurve::OutCubic);
        a->setStartValue(m_hover.value(tid, 0.0)); a->setEndValue(to);
        connect(a, &QVariantAnimation::valueChanged, this, [this, tid](const QVariant& v) {
            m_hover[tid] = v.toReal(); const int idx = indexOfId(tid); if (idx >= 0) update(tabRect(idx).adjusted(-kGap, 0, kGap, 0)); });
        connect(a, &QVariantAnimation::finished, this, [this, tid, a] { if (m_hoverAnim.value(tid) == a) m_hoverAnim.remove(tid); a->deleteLater(); });
        m_hoverAnim.insert(tid, a); a->start();
    };
    fade(m_hoverId, 0.0, 160);
    m_hoverId = id;
    fade(m_hoverId, 1.0, 120);
    updateCloseButtons();
}

void PacitumTabBar::mouseMoveEvent(QMouseEvent* e) {
    if (m_floating) setHoverIndex(tabAt(e->pos()));
    QTabBar::mouseMoveEvent(e);
}
void PacitumTabBar::leaveEvent(QEvent* e) {
    if (m_floating) {
        setHoverIndex(-1);
        if (m_frozenWidth) { m_frozenWidth = 0; relayout(); }
    }
    QTabBar::leaveEvent(e);
}

void PacitumTabBar::setTabLoading(int index, bool on) {
    if (index < 0 || index >= count()) return;
    auto& f = m_flags[ensureId(index)];
    if (f.loading == on) return;
    f.loading = on; if (on) f.crashed = false;
    updateSpinner(); update(tabRect(index));
}
void PacitumTabBar::setTabAudio(int index, bool audible, bool muted) {
    if (index < 0 || index >= count()) return;
    auto& f = m_flags[ensureId(index)];
    if (f.audible == audible && f.muted == muted) return;
    f.audible = audible; f.muted = muted; update(tabRect(index));
}
void PacitumTabBar::setTabCrashed(int index, bool on) {
    if (index < 0 || index >= count()) return;
    m_flags[ensureId(index)].crashed = on; update(tabRect(index));
}
void PacitumTabBar::updateSpinner() {
    if (!m_spin) return;
    bool any = false;
    for (const auto& f : std::as_const(m_flags)) any |= f.loading;
    if (any && Pacitum::reducedMotion()) { m_spin->stop(); m_spinAngle = 0; return; }   // static arc
    if (any && m_spin->state() != QAbstractAnimation::Running) m_spin->start();
    else if (!any) m_spin->stop();
}

void PacitumTabBar::paintFloatingTab(QPainter* p, const QStyleOptionTab& opt) const {
    const auto& pal = ThemeManager::current();
    const int idx = opt.tabIndex;
    const quint64 id = idx >= 0 ? idOf(idx) : 0;
    const TabFlags fl = m_flags.value(id);
    const bool pinned = idx >= 0 && m_pinned.contains(idx);
    const bool selected = opt.state & QStyle::State_Selected;
    const qreal hov = selected ? 0.0 : m_hover.value(id, 0.0);
    const qreal appear = m_progress.value(id, 1.0);
    const QRect r = opt.rect;
    const qreal bodyW = pinned ? kPinW : r.width() - kGap;
    const QRectF body(r.x() + 0.5, r.y() + kTop + 0.5, qMax<qreal>(0, bodyW - 1), kTabH - 1);
    if (body.width() < 8) return;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    if (appear < 1.0) p->setOpacity(qMin(1.0, appear * 1.5));   // open: opacity 0→1 (faster than width)
    const QColor chrome = qc(pal.frame);
    QColor fill = chrome;
    const bool dark = chrome.lightness() < 128;
    if (selected) {
        // shadow-1: 1 px offset soft shadow + hairline outline (cheaper than QGraphicsDropShadowEffect)
        p->setPen(Qt::NoPen);
        p->setBrush(dark ? QColor(0, 0, 0, 0x59) : QColor(0x10, 0x18, 0x28, 0x1A));
        p->drawRoundedRect(body.translated(0, 1), 10, 10);
        p->setBrush(dark ? QColor(0, 0, 0, 0x22) : QColor(0x10, 0x18, 0x28, 0x0A));
        p->drawRoundedRect(body.adjusted(-0.5, 0, 0.5, 1.5), 10.5, 10.5);
        fill = qc(pal.tab);
        p->setBrush(fill);
        p->setPen(QPen(dark ? QColor(255, 255, 255, 0x0A) : QColor(0x10, 0x18, 0x28, 0x0D), 1));
        p->drawRoundedRect(body, 10, 10);
    } else if (hov > 0.001) {
        fill = mixc(chrome, qc(pal.tabHover), hov);
        p->setPen(Qt::NoPen); p->setBrush(fill);
        p->drawRoundedRect(body, 10, 10);
    }
    // keyboard focus: 2 px ring with 2 px gap
    if (selected && hasFocus()) {
        p->setBrush(Qt::NoBrush); p->setPen(QPen(qc(pal.focusRing), 2));
        p->drawRoundedRect(body.adjusted(-2.5, -2.5, 2.5, 2.5), 12, 12);
    }

    // separator between two plain inactive tabs, and after the pinned group
    if (idx >= 0) {
        const int sx = r.right() - kGap / 2 + 1;
        if (pinned && !m_pinned.contains(idx + 1) && idx + 1 < count()) {
            p->fillRect(QRectF(r.x() + kPinW + kGap + 4 - 2, r.y() + kTop + 8, 1, 16), qc(pal.borderStrong));
        } else if (!pinned && !selected && hov < 0.5 && idx + 1 < count() && idx + 1 != currentIndex()
                   && idOf(idx + 1) != m_hoverId && opt.position != QStyleOptionTab::Moving) {
            p->fillRect(QRectF(sx - 1, r.y() + kTop + 8, 1, 16), qc(pal.borderStrong));
        }
    }

    const bool compact = !pinned && bodyW < 80;
    const bool closeVisible = idx >= 0 && tabButton(idx, QTabBar::RightSide) && tabButton(idx, QTabBar::RightSide)->isVisible();
    int iconX = pinned || (compact && !closeVisible) ? int(body.center().x() - 8) : r.x() + 12;
    const QRectF iconR(iconX, r.y() + kTop + 8, 16, 16);
    if (fl.loading) {
        QPen track(qc(pal.surfaceHover), 2); p->setPen(track); p->setBrush(Qt::NoBrush);
        const QRectF a = iconR.adjusted(1.5, 1.5, -1.5, -1.5);
        p->drawEllipse(a);
        QPen arc(qc(pal.accent), 2); arc.setCapStyle(Qt::RoundCap); p->setPen(arc);
        p->drawArc(a, int(-m_spinAngle * 16) + 90 * 16, -90 * 16);
    } else if (fl.crashed) {
        p->drawPixmap(iconR.toRect(), ThemeManager::icon(QStringLiteral("warning"), pal.danger, 16).pixmap(QSize(16, 16), devicePixelRatioF()));
    } else if (!opt.icon.isNull()) {
        // favicons are clipped to a 4 px radius
        QPainterPath clip; clip.addRoundedRect(iconR, 4, 4);
        p->save(); p->setClipPath(clip, Qt::IntersectClip);
        p->drawPixmap(iconR.toRect(), opt.icon.pixmap(QSize(16, 16), devicePixelRatioF()));
        p->restore();
    }
    if (pinned || (compact && !selected) || (compact && closeVisible)) { p->restore(); return; }

    // trailing items: [audio] [close]
    int right = int(body.right()) - 6;
    if (closeVisible) right = closeButtonRect(r).left() - 4;
    if (fl.audible || fl.muted) {
        const QRect ar(right - 16, r.y() + kTop + 8, 16, 16);
        const QPixmap pm = fl.muted ? ThemeManager::icon(QStringLiteral("tab-mute"), pal.textSecondary, 15).pixmap(QSize(15, 15), devicePixelRatioF())
                                    : ThemeManager::icon(QStringLiteral("audio-playing"), pal.accent, 16).pixmap(QSize(16, 16), devicePixelRatioF());
        p->drawPixmap(ar.topLeft() + QPoint((16 - pm.deviceIndependentSize().width()) / 2, (16 - pm.deviceIndependentSize().height()) / 2), pm);
        right = ar.left() - 4;
    }

    // title: left aligned 8 px after the favicon, last 15 % fades out instead of "…"
    QString title = opt.text;
    if (fl.loading && (title.isEmpty() || title == QLatin1String("New Tab") || title.startsWith(QLatin1String("http")))) title = QStringLiteral("Loading…");
    if (fl.crashed) title = QStringLiteral("Aw, snap — page crashed");
    const QRect tr(int(iconR.right()) + 8, r.y() + kTop, qMax(0, right - int(iconR.right()) - 8), kTabH);
    if (tr.width() > 4) {
        QFont f = font(); f.setPixelSize(13); p->setFont(f);
        p->setPen(selected || hov > 0.5 ? qc(pal.text) : qc(pal.textSecondary));
        const int textW = QFontMetrics(f).horizontalAdvance(title);
        p->save(); p->setClipRect(tr);
        p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
        p->restore();
        if (textW > tr.width()) {
            const int fadeW = qMax(12, tr.width() * 15 / 100);
            QLinearGradient g(tr.right() - fadeW, 0, tr.right() + 1, 0);
            QColor c0 = fill; c0.setAlpha(0);
            g.setColorAt(0, c0); g.setColorAt(1, fill);
            p->fillRect(QRect(tr.right() - fadeW, tr.y() + 4, fadeW + 1, tr.height() - 8), g);
        }
    }
    p->restore();
}

void PacitumTabBar::animateIn(int index) {
    if (index < 0 || index >= count()) return;
    const quint64 id = m_nextId++;
    setTabData(index, id);
    const int ms = Pacitum::motionMs(180);
    if (!animationsEnabled || ms == 0) return;
    m_progress[id] = 0.0;
    auto* a = new QVariantAnimation(this);
    a->setDuration(ms); a->setStartValue(0.0); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::OutCubic);
    connect(a, &QVariantAnimation::valueChanged, this, [this, id](const QVariant& v){ m_progress[id] = v.toReal(); relayout(); });
    connect(a, &QVariantAnimation::finished, this, [this, id]{ m_progress.remove(id); relayout(); updateCloseButtons(); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void PacitumTabBar::animateOut(int index, std::function<void()> done) {
    const int ms = Pacitum::motionMs(140);
    if (!animationsEnabled || ms == 0 || index < 0 || index >= count()) { done(); return; }
    quint64 id = ensureId(index);
    if (QWidget* b = tabButton(index, QTabBar::RightSide)) b->hide();
    auto* a = new QVariantAnimation(this);
    a->setDuration(ms); a->setStartValue(m_progress.value(id, 1.0)); a->setEndValue(0.0); a->setEasingCurve(QEasingCurve::InCubic);
    connect(a, &QVariantAnimation::valueChanged, this, [this, id](const QVariant& v){ m_progress[id] = v.toReal(); relayout(); });
    connect(a, &QVariantAnimation::finished, this, [this, id, done]{ m_progress.remove(id); done(); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void PacitumTabBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        int idx = tabAt(e->pos());
        if (idx >= 0) emit tabRightClicked(idx, e->globalPosition().toPoint());
        else emit emptySpaceRightClicked(e->globalPosition().toPoint());
    } else if (e->button() == Qt::MiddleButton) {
        int idx = tabAt(e->pos());
        if (idx >= 0) emit tabCloseRequested(idx);
    } else if (m_floating && e->button() == Qt::LeftButton) {
        const int idx = tabAt(e->pos());
        if (idx >= 0) {
            const TabFlags f = m_flags.value(idOf(idx));
            if ((f.audible || f.muted) && audioRect(idx).adjusted(-3, -3, 3, 3).contains(e->pos())) { emit tabMuteToggled(idx, !f.muted); return; }
        }
    }
    QTabBar::mousePressEvent(e);
}
void PacitumTabBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tabAt(e->pos()) < 0 && window()) {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    }
    QTabBar::mouseDoubleClickEvent(e);
}

QWidget* PacitumTabWidget::stripNewTabButton() const { return m_stripNewTab; }
void PacitumTabWidget::setTabLoading(int i, bool on) { if (m_ext) m_ext->setTabLoading(i, on); }
void PacitumTabWidget::setTabAudio(int i, bool a, bool m) { if (m_ext) m_ext->setTabAudio(i, a, m); }
void PacitumTabWidget::setTabCrashed(int i, bool on) { if (m_ext) m_ext->setTabCrashed(i, on); }

// PacitumTabWidget
PacitumTabWidget::PacitumTabWidget(bool vertical, QWidget* parent) : QTabWidget(parent), m_vertical(vertical) {
    m_bar = new PacitumTabBar(vertical, this);
    setTabBar(m_bar);
    setTabsClosable(true); setMovable(true); setDocumentMode(true);

    m_newTabBtn = new QToolButton(this);
    m_newTabBtn->setFixedSize(28,28); m_newTabBtn->setIconSize(QSize(18,18)); m_newTabBtn->setAutoRaise(true);
    m_newTabBtn->setToolTip(QStringLiteral("New tab (Ctrl+T)"));
    connect(m_newTabBtn, &QToolButton::clicked, this, &PacitumTabWidget::newTabRequested);
    updateCorner();

    connect(m_bar, &PacitumTabBar::tabRightClicked, this, &PacitumTabWidget::showTabContextMenu);
    connect(m_bar, &PacitumTabBar::emptySpaceRightClicked, this, &PacitumTabWidget::showBarContextMenu);
}

QWidget* PacitumTabWidget::createTabStrip(QWidget* parent) {
    if (m_strip) return m_strip;
    m_strip = new QWidget(parent);
    m_strip->setObjectName(QStringLiteral("TabStrip"));
    m_strip->setAttribute(Qt::WA_StyledBackground, true);
    m_strip->setFixedHeight(42);
    auto* lay = new QHBoxLayout(m_strip);
    m_stripLayout = lay;
    lay->setContentsMargins(10,0,0,0); lay->setSpacing(0);
    // 1.5: no standalone app logo here (it duplicated the NTP favicon); identity = window icon + NTP.

    m_ext = new PacitumTabBar(false, m_strip);
    m_ext->setMovable(true);
    m_ext->enableFloatingTabs();
    m_ext->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_ext->setFixedHeight(42);
    lay->addWidget(m_ext, 0, Qt::AlignBottom);

    m_stripNewTab = new AnimatedToolButton(m_strip);
    m_stripNewTab->setRadius(16);
    m_stripNewTab->setFixedSize(32,32); m_stripNewTab->setIconSize(QSize(18,18));
    m_stripNewTab->setToolTip(QStringLiteral("New tab (Ctrl+T)"));
    m_stripNewTab->setAccessibleName(QStringLiteral("New tab"));
    connect(m_stripNewTab, &QToolButton::clicked, this, &PacitumTabWidget::newTabRequested);
    lay->addWidget(m_stripNewTab, 0, Qt::AlignVCenter);
    lay->addSpacing(40);   // drag area is always at least 40 px
    lay->addStretch(1);

    m_tabListBtn = new AnimatedToolButton(m_strip);
    m_tabListBtn->setRadius(0);
    m_tabListBtn->setFixedSize(36,42); m_tabListBtn->setIconSize(QSize(18,18));
    m_tabListBtn->setToolTip(QStringLiteral("Search tabs (Ctrl+Shift+A)"));
    m_tabListBtn->setAccessibleName(QStringLiteral("Search tabs"));
    connect(m_tabListBtn, &QToolButton::clicked, this, &PacitumTabWidget::showTabList);
    lay->addWidget(m_tabListBtn);

    // existing tabs
    for (int i = 0; i < count(); ++i) m_ext->addTab(tabIcon(i), tabText(i));
    m_ext->setCurrentIndex(currentIndex());

    connect(m_ext, &QTabBar::currentChanged, this, [this](int i){ if (!m_syncing && i >= 0) { m_syncing = true; setCurrentIndex(i); m_syncing = false; } });
    connect(this, &QTabWidget::currentChanged, this, [this](int i){ if (!m_syncing && m_ext && i >= 0) { m_syncing = true; m_ext->setCurrentIndex(i); m_syncing = false; } });
    connect(m_ext, &QTabBar::tabMoved, this, [this](int from, int to){ if (!m_syncing) { m_syncing = true; m_bar->moveTab(from, to); m_syncing = false; } });
    connect(m_ext, &QTabBar::tabCloseRequested, this, &QTabWidget::tabCloseRequested);
    connect(m_ext, &PacitumTabBar::tabRightClicked, this, &PacitumTabWidget::showTabContextMenu);
    connect(m_ext, &PacitumTabBar::emptySpaceRightClicked, this, &PacitumTabWidget::showBarContextMenu);
    connect(m_ext, &PacitumTabBar::tabMuteToggled, this, &PacitumTabWidget::tabMuteRequested);

    m_strip->installEventFilter(this);
    updateCorner();
    updateBarVisibility();
    return m_strip;
}

void PacitumTabWidget::updateTabWidths() {
    if (!m_strip || !m_ext || !m_stripLayout) return;
    int used = m_stripLayout->contentsMargins().left() + m_stripLayout->contentsMargins().right();
    for (int i = 0; i < m_stripLayout->count(); ++i) {
        auto* it = m_stripLayout->itemAt(i);
        if (it->widget() == m_ext) continue;
        if (it->widget() && !it->widget()->isVisible()) continue;
        if (it->spacerItem() && it->spacerItem()->expandingDirections()) continue;
        used += it->widget() ? it->widget()->width() : it->sizeHint().width();
    }
    // 'used' already contains the 40 px minimum drag area spacer and the window controls,
    // so tabs are always the first thing to shrink and the controls never clip.
    m_ext->setAvailableWidth(qMax(kMinStripTabs, m_strip->width() - used - 4));
}

bool PacitumTabWidget::eventFilter(QObject* o, QEvent* e) {
    if (o == m_strip && (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest)) updateTabWidths();
    return QTabWidget::eventFilter(o, e);
}

void PacitumTabWidget::animateRemove(int index, std::function<void()> done) {
    if (!m_ext || m_vertical || !m_ext->isVisible()) { done(); return; }
    m_ext->animateOut(index, std::move(done));
}

void PacitumTabWidget::setTabText(int index, const QString& text) {
    QTabWidget::setTabText(index, text);
    if (m_ext && index >= 0 && index < m_ext->count()) m_ext->setTabText(index, text);
}
void PacitumTabWidget::setTabIcon(int index, const QIcon& icon) {
    QTabWidget::setTabIcon(index, icon);
    if (m_ext && index >= 0 && index < m_ext->count()) m_ext->setTabIcon(index, icon);
}
void PacitumTabWidget::setTabToolTip(int index, const QString& tip) {
    QTabWidget::setTabToolTip(index, tip);
    if (m_ext && index >= 0 && index < m_ext->count()) m_ext->setTabToolTip(index, tip);
}

void PacitumTabWidget::tabInserted(int index) {
    QTabWidget::tabInserted(index);
    if (m_ext) {
        m_syncing = true;
        m_ext->insertTab(index, tabIcon(index), tabText(index));
        m_ext->setCurrentIndex(currentIndex());
        m_syncing = false;
        m_ext->animateIn(index);
    }
}
void PacitumTabWidget::tabRemoved(int index) {
    QTabWidget::tabRemoved(index);
    if (m_ext && index < m_ext->count()) {
        m_syncing = true;
        m_ext->removeTab(index);
        if (currentIndex() >= 0) m_ext->setCurrentIndex(currentIndex());
        m_syncing = false;
    }
}

void PacitumTabWidget::showEvent(QShowEvent* e) {
    QTabWidget::showEvent(e);
    updateBarVisibility();
}

void PacitumTabWidget::updateBarVisibility() {
    const bool external = m_ext && !m_vertical;
    m_bar->setVisible(!external);
    if (m_ext) m_ext->setVisible(external);
    if (m_stripNewTab) m_stripNewTab->setVisible(external);
    if (m_tabListBtn) m_tabListBtn->setVisible(external);
    m_newTabBtn->setVisible(!external);
}

void PacitumTabWidget::setVertical(bool v) {
    m_vertical = v; m_bar->setVerticalMode(v);
    setTabPosition(v ? QTabWidget::West : QTabWidget::North);
    updateCorner(); updateBarVisibility();
}

void PacitumTabWidget::setTabPinned(int index, bool pinned) {
    if (index < 0 || index >= count()) return;
    m_bar->setTabPinned(index, pinned);
    if (m_ext) m_ext->setTabPinned(index, pinned);
    if (pinned) {
        QWidget* w = widget(index); QString t = tabText(index); QIcon ic = tabIcon(index);
        removeTab(index);
        int newIndex = 0;
        for (int i=0;i<count();++i) if (!m_bar->isTabPinned(i)) { newIndex=i; break; }
        if (count()==0) newIndex=0;
        insertTab(newIndex, w, ic, t);
        m_bar->setTabPinned(newIndex, true);
        m_bar->setTabButton(newIndex, QTabBar::RightSide, nullptr);
        if (m_ext) { m_ext->setTabPinned(newIndex, true); m_ext->setTabButton(newIndex, QTabBar::RightSide, nullptr); }
        setCurrentIndex(newIndex);
    }
}

void PacitumTabWidget::applyTheme(ThemeManager* tm) {
    const QString qss = tm->tabWidgetStyle(m_vertical);
    setStyleSheet(qss);
    if (m_strip) {
        const auto& p = tm->palette();
        m_ext->setStyleSheet(QStringLiteral(
            "QTabBar{background:transparent;}"
            "QTabBar QToolButton{background:%1;border:none;border-radius:12px;margin:9px 0;}"
            "QTabBar QToolButton:hover{background:%2;}").arg(p.frame, p.surface));
        m_ext->update();
    }
    const QIcon add = tm->secondaryIcon(QStringLiteral("add"), 18);
    m_newTabBtn->setIcon(add);
    if (m_stripNewTab) m_stripNewTab->setIcon(add);
    if (m_tabListBtn) m_tabListBtn->setIcon(tm->icon(QStringLiteral("tab-search"), 18));
}

void PacitumTabWidget::updateCorner() {
    auto corner = m_vertical ? Qt::TopLeftCorner : Qt::TopRightCorner;
    setCornerWidget(m_newTabBtn, corner);
}

void PacitumTabWidget::showTabList() {
    QMenu menu(this);
    for (int i = 0; i < count(); ++i) {
        auto* a = menu.addAction(tabIcon(i), tabText(i));
        a->setCheckable(true); a->setChecked(i == currentIndex());
        connect(a, &QAction::triggered, this, [this, i]{ setCurrentIndex(i); });
    }
    menu.exec(m_tabListBtn->mapToGlobal(QPoint(m_tabListBtn->width() - menu.sizeHint().width(), m_tabListBtn->height())));
}

void PacitumTabWidget::showTabContextMenu(int index, const QPoint& pos) {
    QMenu menu(this);
    auto* closeAct = menu.addAction(QStringLiteral("Close Tab"));
    connect(closeAct, &QAction::triggered, this, [this, index]{ emit tabCloseRequested(index); });
    auto* closeOthers = menu.addAction(QStringLiteral("Close Other Tabs"));
    connect(closeOthers, &QAction::triggered, this, [this, index]{ closeOtherTabs(index); });
    auto* closeRight = menu.addAction(QStringLiteral("Close Tabs to the Right"));
    connect(closeRight, &QAction::triggered, this, [this, index]{ closeTabsToRight(index); });
    menu.addSeparator();
    auto* dup = menu.addAction(QStringLiteral("Duplicate Tab"));
    connect(dup, &QAction::triggered, this, [this, index]{ emit tabDuplicateRequested(index); });
    auto* split = menu.addAction(QStringLiteral("Open in Split View"));
    connect(split, &QAction::triggered, this, [this, index]{ emit tabSplitRequested(index); });
    menu.addSeparator();
    auto* pin = menu.addAction(activeBar()->isTabPinned(index) ? QStringLiteral("Unpin Tab") : QStringLiteral("Pin Tab"));
    connect(pin, &QAction::triggered, this, [this, index]{ emit tabPinRequested(index, !activeBar()->isTabPinned(index)); });
    auto* mute = menu.addAction(QStringLiteral("Mute Tab"));
    connect(mute, &QAction::triggered, this, [this, index]{ emit tabMuteRequested(index, true); });
    menu.exec(pos);
}

void PacitumTabWidget::showBarContextMenu(const QPoint& pos) {
    QMenu menu(this);
    auto* nt = menu.addAction(QStringLiteral("New Tab"));
    connect(nt, &QAction::triggered, this, &PacitumTabWidget::newTabRequested);
    auto* re = menu.addAction(QStringLiteral("Reopen Closed Tab"));
    connect(re, &QAction::triggered, this, &PacitumTabWidget::tabReopenRequested);
    menu.exec(pos);
}

void PacitumTabWidget::closeOtherTabs(int index) {
    for (int i=count()-1;i>=0;--i) if (i!=index) emit tabCloseRequested(i);
}
void PacitumTabWidget::closeTabsToRight(int index) {
    for (int i=count()-1;i>index;--i) emit tabCloseRequested(i);
}
