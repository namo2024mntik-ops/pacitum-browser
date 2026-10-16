#include "Motion.h"
#include "ThemeManager.h"
#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScreen>
#include <QVariantAnimation>
#include <QWidget>

namespace Motion {
int ms(int token) { return Pacitum::motionMs(token); }
// Opacity-only fades survive reduced motion, capped at 120 ms (shared kit: reduced = fade 120, no movement).
int fadeMs(int token) { return Pacitum::reducedMotion() ? qMin(token, 120) : token; }
bool targetVisible(QWidget* w) { const QVariant v = w->property("_pmTarget"); return v.isValid() ? v.toBool() : w->isVisible(); }
bool enabled() { return !Pacitum::reducedMotion(); }

void fadeIn(QWidget* w, int token) {
    if (!w || fadeMs(token) <= 0 || w->graphicsEffect()) return;
    auto* eff = new QGraphicsOpacityEffect(w); eff->setOpacity(0.0); w->setGraphicsEffect(eff);
    auto* a = new QPropertyAnimation(eff, "opacity", w);
    a->setDuration(fadeMs(token)); a->setStartValue(0.0); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> g(w);
    QObject::connect(a, &QPropertyAnimation::finished, w, [g]{ if (g) g->setGraphicsEffect(nullptr); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

static int restMax(QWidget* w) { const QVariant v = w->property("_pmMax"); return v.isValid() ? v.toInt() : QWIDGETSIZE_MAX; }

void slideVisible(QWidget* w, bool show, int target, int token) {
    if (!w) return;
    w->setProperty("_pmTarget", show);
    if (!w->property("_pmMin").isValid()) w->setProperty("_pmMin", w->minimumHeight());
    if (!w->property("_pmMax").isValid() && w->maximumHeight() < QWIDGETSIZE_MAX && w->maximumHeight() > 0) w->setProperty("_pmMax", w->maximumHeight());
    if (auto* old = w->findChild<QPropertyAnimation*>(QStringLiteral("_pmSlide"), Qt::FindDirectChildrenOnly)) { old->stop(); old->deleteLater(); }
    if (target <= 0) { const int mx = w->maximumHeight(); w->setMaximumHeight(QWIDGETSIZE_MAX); target = qMax(1, w->sizeHint().height()); w->setMaximumHeight(mx); }
    const int d = ms(token);
    if (d <= 0 || !w->parentWidget() || !w->window()->isVisible()) {
        w->setMaximumHeight(restMax(w)); w->setMinimumHeight(w->property("_pmMin").toInt());
        w->setVisible(show);
        return;
    }
    if (show && w->isVisible() && w->maximumHeight() >= target) return;
    if (!show && !w->isVisible()) return;
    const int from = show ? (w->isVisible() ? qMin(w->maximumHeight(), target) : 0) : qMin(w->height(), target);
    w->setMinimumHeight(0);
    w->setMaximumHeight(from);
    if (show) w->show();
    auto* a = new QPropertyAnimation(w, "maximumHeight", w);
    a->setObjectName(QStringLiteral("_pmSlide"));
    a->setDuration(d); a->setEasingCurve(show ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    a->setStartValue(from); a->setEndValue(show ? target : 0);
    QPointer<QWidget> g(w);
    QObject::connect(a, &QPropertyAnimation::finished, w, [g, show, target]{
        if (!g) return;
        if (!show) g->hide();
        g->setMaximumHeight(restMax(g));   // never leave a clamp behind (resize safety)
        g->setMinimumHeight(g->property("_pmMin").toInt());
        Q_UNUSED(target);
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void pop(QWidget* w) {
    if (!w || w->graphicsEffect()) return;
    auto* eff = new QGraphicsOpacityEffect(w); eff->setOpacity(0.4); w->setGraphicsEffect(eff);
    auto* a = new QPropertyAnimation(eff, "opacity", w);
    a->setDuration(fadeMs(Medium)); a->setStartValue(0.4); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> g(w);
    QObject::connect(a, &QPropertyAnimation::finished, w, [g]{ if (g) g->setGraphicsEffect(nullptr); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

namespace {
class Ghost : public QWidget {
public:
    Ghost(const QPixmap& pm, const QRect& geo, qreal scaleTo)
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), m_pm(pm), m_scaleTo(scaleTo) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(geo);
    }
    qreal t = 0.0;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setOpacity(1.0 - t);
        const qreal s = 1.0 + (m_scaleTo - 1.0) * t;
        const QPointF c = QRectF(rect()).center();
        p.translate(c); p.scale(s, s); p.translate(-c);
        p.drawPixmap(rect(), m_pm);
    }
private:
    QPixmap m_pm; qreal m_scaleTo;
};
}

void ghostOut(QWidget* w, int token, qreal scaleTo) {
    const int d = fadeMs(token);
    if (!enabled()) scaleTo = 1.0;
    if (!w || d <= 0 || QCoreApplication::closingDown() || w->width() < 2 || w->height() < 2) return;
    if (w->width() * w->height() > 1400 * 1000) return;   // never ghost something window-sized
    const QPixmap pm = w->grab();
    if (pm.isNull()) return;
    auto* g = new Ghost(pm, w->geometry(), scaleTo);
    g->show();
    auto* a = new QVariantAnimation(g);
    a->setDuration(d); a->setStartValue(0.0); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::InCubic);
    QObject::connect(a, &QVariantAnimation::valueChanged, g, [g](const QVariant& v){ g->t = v.toReal(); g->update(); });
    QObject::connect(a, &QVariantAnimation::finished, g, &QWidget::close);
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

namespace {
class HoverLift : public QObject {
public:
    HoverLift(QWidget* w, int dy) : QObject(w), m_w(w), m_dy(dy) {}
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::Enter) go(true);
        else if (e->type() == QEvent::Leave || e->type() == QEvent::Hide) go(false);
        else if (e->type() == QEvent::Move && !m_anim) m_base = m_w->pos() - QPoint(0, m_lifted ? m_dy : 0);
        return false;
    }
private:
    void go(bool up) {
        if (up == m_lifted) return;
        if (!m_anim) m_base = m_w->pos() - QPoint(0, m_lifted ? m_dy : 0);
        m_lifted = up;
        const QPoint end = m_base + QPoint(0, up ? m_dy : 0);
        if (m_anim) { m_anim->stop(); m_anim->deleteLater(); }
        if (!enabled()) { m_w->move(end); return; }
        m_anim = new QPropertyAnimation(m_w, "pos", this);
        m_anim->setDuration(ms(Base)); m_anim->setEasingCurve(QEasingCurve::OutCubic);
        m_anim->setStartValue(m_w->pos()); m_anim->setEndValue(end);
        connect(m_anim, &QPropertyAnimation::finished, this, [this]{ if (m_anim) { m_anim->deleteLater(); m_anim = nullptr; } });
        m_anim->start();
    }
    QWidget* m_w; int m_dy; bool m_lifted = false; QPoint m_base; QPointer<QPropertyAnimation> m_anim;
};
}
void installHoverLift(QWidget* w, int dy) {
    if (!w) return;
    w->installEventFilter(new HoverLift(w, dy));
}

namespace {
// Transient pill (zoom percentage, fullscreen hint). A child of the window that re-centres itself
// on every resize, is click-through, and deletes itself after the sequence.
class Bubble : public QLabel {
public:
    Bubble(QWidget* host, int topY) : QLabel(host), m_top(topY) {
        setObjectName(QStringLiteral("PacitumBubble"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAlignment(Qt::AlignCenter);
        host->installEventFilter(this);
    }
    void place() { adjustSize(); move((parentWidget()->width() - width()) / 2, m_top); }
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize) place();
        return false;
    }
    int m_top;
};
}
void bubble(QWidget* host, const QString& text, int holdMs, int topY) {
    if (!host || !host->isVisible()) return;
    QLabel* b = host->findChild<QLabel*>(QStringLiteral("PacitumBubble"), Qt::FindDirectChildrenOnly);
    if (b) { for (auto* a : b->findChildren<QAbstractAnimation*>()) a->stop(); b->deleteLater(); }
    auto* nb = new Bubble(host, topY); b = nb;
    const auto& p = ThemeManager::current();
    b->setStyleSheet(QStringLiteral("QLabel{background:%1;color:%2;border:1px solid %3;border-radius:16px;padding:6px 16px;font-weight:600;}")
                         .arg(p.card(), p.text, p.border));
    nb->setText(text); nb->place(); nb->show(); nb->raise();
    auto* eff = new QGraphicsOpacityEffect(b); eff->setOpacity(0.0); b->setGraphicsEffect(eff);
    auto* seq = new QSequentialAnimationGroup(b);
    auto* in = new QPropertyAnimation(eff, "opacity"); in->setDuration(qMax(1, fadeMs(Base))); in->setStartValue(0.0); in->setEndValue(1.0); in->setEasingCurve(QEasingCurve::OutCubic);
    auto* out = new QPropertyAnimation(eff, "opacity"); out->setDuration(qMax(1, fadeMs(Quick))); out->setStartValue(1.0); out->setEndValue(0.0); out->setEasingCurve(QEasingCurve::InCubic);
    seq->addAnimation(in); seq->addPause(holdMs); seq->addAnimation(out);
    QObject::connect(seq, &QAbstractAnimation::finished, b, &QObject::deleteLater);
    seq->start(QAbstractAnimation::DeleteWhenStopped);
}

// Toast (kit #26): rises 16 px + fades in 240 OutCubic; leaves 8 px down + fade 180 InCubic.
void toast(QWidget* w, bool show) {
    if (!w) return;
    for (auto* a : w->findChildren<QAbstractAnimation*>(QStringLiteral("_pmToast"))) { a->stop(); a->deleteLater(); }
    if (show) w->show(); else if (!w->isVisible()) return;
    const int d = fadeMs(show ? Enter : Exit);
    auto* eff = qobject_cast<QGraphicsOpacityEffect*>(w->graphicsEffect());
    if (d <= 0) { w->setGraphicsEffect(nullptr); w->setVisible(show); return; }
    if (!eff) { eff = new QGraphicsOpacityEffect(w); eff->setOpacity(show ? 0.0 : 1.0); w->setGraphicsEffect(eff); }
    auto* g = new QParallelAnimationGroup(w); g->setObjectName(QStringLiteral("_pmToast"));
    auto* op = new QPropertyAnimation(eff, "opacity"); op->setDuration(d); op->setStartValue(eff->opacity()); op->setEndValue(show ? 1.0 : 0.0);
    op->setEasingCurve(show ? QEasingCurve::OutCubic : QEasingCurve::InCubic); g->addAnimation(op);
    const QPoint rest = w->pos();
    if (enabled()) {
        auto* mv = new QPropertyAnimation(w, "pos"); mv->setDuration(d);
        mv->setStartValue(show ? rest + QPoint(0, 16) : rest); mv->setEndValue(show ? rest : rest + QPoint(0, 8));
        mv->setEasingCurve(show ? QEasingCurve::OutCubic : QEasingCurve::InCubic); g->addAnimation(mv);
    }
    QPointer<QWidget> gw(w);
    QObject::connect(g, &QAbstractAnimation::finished, w, [gw, show, rest]{
        if (!gw) return;
        gw->setGraphicsEffect(nullptr);
        if (!show) { gw->hide(); gw->move(rest); }
    });
    g->start(QAbstractAnimation::DeleteWhenStopped);
}
}
