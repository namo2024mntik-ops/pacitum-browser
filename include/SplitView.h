#pragma once
// 1.8.5 split view: a splitter with a grip handle (hover glow, double-click = 50/50) and an accent
// bar that slides over whichever side has focus. Nothing here animates a widget that hosts a web
// view: only the handle and a 2 px marker move.
#include <QSplitter>
#include <QSplitterHandle>
#include <QPointer>
#include <QVariantAnimation>

class QPropertyAnimation;

class SplitHandle : public QSplitterHandle {
    Q_OBJECT
public:
    SplitHandle(Qt::Orientation o, QSplitter* parent);
protected:
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
private:
    void animateTo(qreal v);
    QVariantAnimation m_anim;
    qreal m_hover = 0.0;
    bool m_dragging = false;
};

class PacitumSplitter : public QSplitter {
    Q_OBJECT
public:
    explicit PacitumSplitter(QWidget* parent = nullptr);
    void equalize();                       // both visible panes get the same width
    void setActivePane(QWidget* pane);     // accent bar over the focused side (only while split)
    void refreshMarker(bool animate = false);
protected:
    QSplitterHandle* createHandle() override;
    void resizeEvent(QResizeEvent* e) override;
    void moveEvent(QMoveEvent* e) override;
private:
    int visiblePanes() const;
    QPointer<QWidget> m_active;
    QPointer<QWidget> m_marker;
    QPropertyAnimation* m_markerAnim = nullptr;
};
