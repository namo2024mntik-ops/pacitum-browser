#include "TabOverview.h"
#include "ThemeManager.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>

TabOverview::TabOverview(const QList<Card>& cards, int current, QWidget* parent)
    : QWidget(parent), m_cards(cards) {
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Tab overview"));
    m_sel = qBound(0, current, qMax(0, int(m_cards.size()) - 1));
    m_pos = m_sel;
    m_posAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_posAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){ m_pos = v.toReal(); update(); });
    m_showAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_showAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v){ m_show = v.toReal(); update(); });
    connect(&m_showAnim, &QVariantAnimation::finished, this, [this]{
        if (m_show <= 0.0) { emit chosen(m_result); close(); }
    });
}

void TabOverview::open() {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    prepareCards();
    show(); raise(); setFocus();
    const int ms = Pacitum::motionMs(240);
    if (ms <= 0) { m_show = 1.0; update(); return; }
    m_showAnim.setDuration(ms); m_showAnim.setStartValue(0.0); m_showAnim.setEndValue(1.0); m_showAnim.start();
}

QSize TabOverview::cardSize() const {
    const int h = qMax(120, int(height() * 0.46));
    return QSize(int(h * 1.6), h);
}

// Pre-scale every snapshot once per size: the 3D frames then sample small pixmaps.
void TabOverview::prepareCards() {
    const QSize cs = cardSize() * devicePixelRatioF();
    m_scaled.clear();
    for (const Card& c : std::as_const(m_cards)) {
        QPixmap pm;
        if (!c.shot.isNull()) {
            pm = c.shot.scaled(cs, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            pm = pm.copy((pm.width() - cs.width()) / 2, 0, cs.width(), cs.height());
            pm.setDevicePixelRatio(devicePixelRatioF());
        }
        m_scaled << pm;
    }
}

void TabOverview::resizeEvent(QResizeEvent*) { prepareCards(); }

void TabOverview::select(int i, bool animate) {
    if (m_cards.isEmpty()) return;
    m_sel = qBound(0, i, int(m_cards.size()) - 1);
    const int ms = animate ? Pacitum::motionMs(260) : 0;
    m_posAnim.stop();
    if (ms <= 0) { m_pos = m_sel; update(); return; }
    m_posAnim.setDuration(ms); m_posAnim.setStartValue(m_pos); m_posAnim.setEndValue(qreal(m_sel)); m_posAnim.start();
}

void TabOverview::finish(int index) {
    if (m_closing) return;   // a second Esc / click while the close animation runs
    m_closing = true;
    m_result = index;
    if (index >= 0 && index != m_sel) select(index);
    const int ms = Pacitum::motionMs(180);
    if (ms <= 0) { emit chosen(m_result); close(); return; }
    m_showAnim.stop(); m_showAnim.setDuration(ms); m_showAnim.setStartValue(m_show); m_showAnim.setEndValue(0.0); m_showAnim.start();
}

void TabOverview::paintEvent(QPaintEvent*) {
    const auto& pal = ThemeManager::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // backdrop: the window colour, faded in
    QColor bg(pal.frame); bg.setAlphaF(0.94 * m_show);
    p.fillRect(rect(), bg);
    if (m_cards.isEmpty()) return;

    const QSize cs = cardSize();
    const QPointF centre(width() / 2.0, height() * 0.46);
    const qreal spacing = cs.width() * 0.42;
    const qreal lift = (1.0 - m_show) * 40.0;   // cards rise into place when opening
    m_hit = QList<QPolygonF>(m_cards.size());

    // paint far cards first so nearer ones overlap them
    QList<int> order;
    for (int i = 0; i < m_cards.size(); ++i) if (qAbs(i - m_pos) < 5.5) order << i;
    std::sort(order.begin(), order.end(), [this](int a, int b){ return qAbs(a - m_pos) > qAbs(b - m_pos); });

    for (int i : order) {
        const qreal off = i - m_pos;
        const qreal a = qAbs(off);
        const qreal angle = qBound(-58.0, -off * 38.0, 58.0);
        const qreal scale = qMax(0.55, 1.0 - 0.14 * a) * (0.9 + 0.1 * m_show);
        const qreal x = centre.x() + (off > 0 ? 1 : -1) * (a <= 1 ? a * spacing * 1.25 : spacing * 1.25 + (a - 1) * spacing * 0.55);
        QTransform tr;
        tr.translate(x, centre.y() + lift);
        tr.rotate(angle, Qt::YAxis);
        tr.scale(scale, scale);
        tr.translate(-cs.width() / 2.0, -cs.height() / 2.0);
        const QRectF card(QPointF(0, 0), QSizeF(cs));
        const qreal opacity = qBound(0.0, 1.0 - 0.17 * a, 1.0) * m_show;
        p.save();
        p.setTransform(tr);
        p.setOpacity(opacity);
        // shadow, then the snapshot clipped to rounded corners
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 70));
        p.drawRoundedRect(card.translated(0, 10).adjusted(6, 6, -6, 4), 16, 16);
        QPainterPath clip; clip.addRoundedRect(card, 14, 14);
        p.setClipPath(clip);
        p.fillRect(card, QColor(pal.page));
        const QPixmap& pm = m_scaled.value(i);
        p.setRenderHint(QPainter::SmoothPixmapTransform, qAbs(off) < 0.05);   // sharp when still, fast while moving
        if (!pm.isNull()) p.drawPixmap(card.toRect(), pm);
        else {   // never shown yet: favicon + title placeholder
            const QPixmap ic = m_cards[i].icon.pixmap(QSize(48, 48), devicePixelRatioF());
            if (!ic.isNull()) p.drawPixmap(QPointF(card.center().x() - 24, card.center().y() - 40), ic);
            QFont f = font(); f.setPixelSize(16); f.setWeight(QFont::DemiBold); p.setFont(f);
            p.setPen(QColor(pal.textSecondary));
            p.drawText(card.adjusted(24, 0, -24, -10).toRect(), Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, m_cards[i].title);
        }
        p.setClipping(false);
        // selection ring on the chosen card
        p.setBrush(Qt::NoBrush);
        p.setPen(i == m_sel ? QPen(QColor(pal.accent), 3) : QPen(QColor(pal.border), 1));
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        p.restore();
        m_hit[i] = tr.map(QPolygonF(card));
    }

    // title of the selected tab + hint
    p.setOpacity(m_show);
    QFont f = font(); f.setPixelSize(18); f.setWeight(QFont::DemiBold); p.setFont(f);
    p.setPen(QColor(pal.text));
    const int ty = int(centre.y() + cs.height() / 2.0 + 44);
    const QString title = QFontMetrics(f).elidedText(m_cards.value(m_sel).title, Qt::ElideRight, width() - 120);
    p.drawText(QRect(0, ty, width(), 28), Qt::AlignHCenter | Qt::AlignVCenter, title);
    QFont h = font(); h.setPixelSize(12); p.setFont(h);
    p.setPen(QColor(pal.textSecondary));
    p.drawText(QRect(0, ty + 30, width(), 20), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("%1 of %2  ·  ← → or scroll to browse  ·  Enter to open  ·  Esc to close").arg(m_sel + 1).arg(m_cards.size()));
}

void TabOverview::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Escape: finish(-1); return;
    case Qt::Key_Left: case Qt::Key_Up: select(m_sel - 1); return;
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Tab: select(m_sel + 1); return;
    case Qt::Key_Backtab: select(m_sel - 1); return;
    case Qt::Key_Home: select(0); return;
    case Qt::Key_End: select(int(m_cards.size()) - 1); return;
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space: finish(m_sel); return;
    default: QWidget::keyPressEvent(e);
    }
}

void TabOverview::wheelEvent(QWheelEvent* e) {
    m_wheelAcc += (e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x());
    while (m_wheelAcc >= 120) { m_wheelAcc -= 120; select(m_sel - 1); }
    while (m_wheelAcc <= -120) { m_wheelAcc += 120; select(m_sel + 1); }
    e->accept();
}

void TabOverview::mousePressEvent(QMouseEvent* e) {
    // topmost (nearest) card under the cursor wins
    int best = -1; qreal bestDist = 1e9;
    for (int i = 0; i < m_hit.size(); ++i)
        if (!m_hit[i].isEmpty() && m_hit[i].containsPoint(e->position(), Qt::OddEvenFill) && qAbs(i - m_pos) < bestDist) { best = i; bestDist = qAbs(i - m_pos); }
    if (best < 0) { finish(-1); return; }   // click on the backdrop closes
    if (best == m_sel) finish(best); else select(best);
}
