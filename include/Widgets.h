#pragma once
#include <QToolButton>
#include <QAbstractButton>
#include <QVariantAnimation>
#include <QPointer>
#include <QIcon>

// Tool button with an animated (120 ms ease-out) hover/press background, per design-spec §5.
class AnimatedToolButton : public QToolButton {
    Q_OBJECT
public:
    explicit AnimatedToolButton(QWidget* parent = nullptr);
    void setRadius(int r) { m_radius = r; update(); }
    void setHoverColor(const QString& c) { m_hoverColor = c; }   // empty → palette surface
protected:
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void paintBackground(QPainter& p);
    qreal hoverLevel() const { return m_level; }
private:
    void animateTo(qreal v);
    QVariantAnimation m_anim;
    qreal m_level = 0.0;
    int m_radius = 8;
    QString m_hoverColor;
};

// Min / max / close caption button drawn with QPainter (46×40, Windows-style).
class WindowButton : public AnimatedToolButton {
    Q_OBJECT
public:
    enum Kind { Minimize, Maximize, Close };
    WindowButton(Kind k, QWidget* parent = nullptr);
    void setMaximized(bool m) { m_max = m; update(); }
protected:
    void paintEvent(QPaintEvent* e) override;
private:
    Kind m_kind;
    bool m_max = false;
};

// 36×20 switch with sliding knob (design-spec §4 Settings controls).
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {36, 20}; }
protected:
    void paintEvent(QPaintEvent* e) override;
    void checkStateSet() override;
    void nextCheckState() override;
private:
    QVariantAnimation m_anim;
    qreal m_pos = 0.0;
};

// 22 px accent swatch circle with selected outline.
class SwatchButton : public QAbstractButton {
    Q_OBJECT
public:
    SwatchButton(const QString& color, QWidget* parent = nullptr);
    QString color() const { return m_color; }
    QSize sizeHint() const override { return {30, 30}; }
protected:
    void paintEvent(QPaintEvent* e) override;
private:
    QString m_color;
};

namespace Pacitum {
// 16 px accent spinner arc (2 px stroke) for loading tabs.
QIcon spinnerIcon(int angle, const QString& color);
// Fade-in for every QMenu popup (installed once on qApp).
void installMenuFader();
}
