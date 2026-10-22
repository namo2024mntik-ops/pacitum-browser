#include "SplitView.h"
#include "ThemeManager.h"
#include <QPainter>
#include <QPropertyAnimation>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QMoveEvent>

namespace {
QColor mixColor(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}
}

SplitHandle::SplitHandle(Qt::Orientation o, QSplitter* parent) : QSplitterHandle(o, parent) {
    setAttribute(Qt::WA_Hover);
    setToolTip(QStringLiteral("Drag to resize · double-click for 50/50"));
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){ m_hover = v.toReal(); update(); });
}

void SplitHandle::animateTo(qreal v) {
    m_anim.stop();
    const int ms = Pacitum::motionMs(140);
    if (ms <= 0) { m_hover = v; update(); return; }
    m_anim.setDuration(ms); m_anim.setStartValue(m_hover); m_anim.setEndValue(v); m_anim.start();
}
void SplitHandle::enterEvent(QEnterEvent* e) { animateTo(1.0); QSplitterHandle::enterEvent(e); }
void SplitHandle::leaveEvent(QEvent* e) { if (!m_dragging) animateTo(0.0); QSplitterHandle::leaveEvent(e); }
void SplitHandle::mousePressEvent(QMouseEvent* e) { m_dragging = true; update(); QSplitterHandle::mousePressEvent(e); }
void SplitHandle::mouseReleaseEvent(QMouseEvent* e) {
    m_dragging = false; if (!underMouse()) animateTo(0.0); update();
    QSplitterHandle::mouseReleaseEvent(e);
}
void SplitHandle::mouseDoubleClickEvent(QMouseEvent* e) {
    if (auto* s = qobject_cast<PacitumSplitter*>(splitter())) s->equalize();
    e->accept();
}

void SplitHandle::paintEvent(QPaintEvent*) {
    const auto& pal = ThemeManager::current();
    QPainter p(this);
    p.fillRect(rect(), QColor(pal.frame));
    const bool horiz = orientation() == Qt::Horizontal;
    // hairline in the middle, then a grip pill that grows and turns accent on hover / drag
    const QColor line = QColor(pal.border);
    if (horiz) p.fillRect(QRectF(width() / 2.0 - 0.5, 0, 1, height()), line);
    else p.fillRect(QRectF(0, height() / 2.0 - 0.5, width(), 1), line);
    const qreal t = m_dragging ? 1.0 : m_hover;
    const QColor grip = mixColor(QColor(pal.textTertiary), QColor(pal.accent), t);
    const qreal len = 28 + 20 * t, thick = 4;
    const QRectF pill = horiz ? QRectF((width() - thick) / 2.0, (height() - len) / 2.0, thick, len)
                              : QRectF((width() - len) / 2.0, (height() - thick) / 2.0, len, thick);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(grip);
    p.drawRoundedRect(pill, thick / 2, thick / 2);
}

PacitumSplitter::PacitumSplitter(QWidget* parent) : QSplitter(Qt::Horizontal, parent) {
    setHandleWidth(8);
    setChildrenCollapsible(false);
    connect(this, &QSplitter::splitterMoved, this, [this]{ refreshMarker(false); });
}

QSplitterHandle* PacitumSplitter::createHandle() { return new SplitHandle(orientation(), this); }

int PacitumSplitter::visiblePanes() const {
    int n = 0;
    for (int i = 0; i < count(); ++i) if (widget(i) && widget(i)->isVisibleTo(this)) ++n;
    return n;
}

void PacitumSplitter::equalize() {
    QList<int> sizes;
    const int n = visiblePanes();
    if (n < 2) return;
    const int each = (orientation() == Qt::Horizontal ? width() : height()) / n;
    for (int i = 0; i < count(); ++i) sizes << (widget(i)->isVisibleTo(this) ? each : 0);
    setSizes(sizes);
    refreshMarker(true);
}

void PacitumSplitter::setActivePane(QWidget* pane) {
    const bool changed = pane != m_active;
    m_active = pane;
    refreshMarker(changed);
}

void PacitumSplitter::refreshMarker(bool animate) {
    QWidget* host = parentWidget();
    if (!host) return;
    if (!m_marker) {
        // The marker is a sibling of the splitter (a child would be adopted as a third pane).
        m_marker = new QWidget(host);
        m_marker->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_marker->setAutoFillBackground(true);
        m_markerAnim = new QPropertyAnimation(m_marker, "geometry", m_marker);
        m_markerAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    if (visiblePanes() < 2 || !m_active || indexOf(m_active) < 0 || !isVisible()) { m_markerAnim->stop(); m_marker->hide(); return; }
    QPalette pl = m_marker->palette(); pl.setColor(QPalette::Window, QColor(ThemeManager::current().accent)); m_marker->setPalette(pl);
    const QRect pane = m_active->geometry().translated(pos());
    const QRect target(pane.x() + 12, pane.y(), qMax(0, pane.width() - 24), 2);
    const int ms = animate && m_marker->isVisible() ? Pacitum::motionMs(220) : 0;
    m_markerAnim->stop();
    m_marker->show(); m_marker->raise();
    if (ms <= 0) { m_marker->setGeometry(target); return; }
    m_markerAnim->setDuration(ms); m_markerAnim->setStartValue(m_marker->geometry()); m_markerAnim->setEndValue(target);
    m_markerAnim->start();
}

void PacitumSplitter::resizeEvent(QResizeEvent* e) { QSplitter::resizeEvent(e); refreshMarker(false); }
void PacitumSplitter::moveEvent(QMoveEvent* e) { QSplitter::moveEvent(e); refreshMarker(false); }
