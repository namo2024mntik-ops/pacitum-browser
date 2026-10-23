#pragma once
// 1.8.9 updater pop-ups (design: "1.8.9 Updater" boards).
//
// Qt Widgets only, no browser code: the same file builds into the browser and into
// PacitumUpdateHelper (Windows), the small program that keeps the "Installing update…" pop-up on
// screen after the browser has quit, runs Setup, and goes away once the new version is running.
#include <QColor>
#include <QPixmap>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QVariantAnimation;
class QTimer;

namespace UpdUi {

struct Colors {
    QColor bg{0x1A, 0x1D, 0x23}, border{0x36, 0x3C, 0x46}, text{0xE8, 0xEA, 0xEE}, sub{0xA0, 0xA6, 0xB1},
           accent{0x2D, 0xD4, 0xBF}, onAccent{0x04, 0x20, 0x1D}, track{0x26, 0x2A, 0x31}, panel{0x12, 0x14, 0x18};
    QString toArg() const;                     // "#bg,#border,…" for the helper's command line
    static Colors fromArg(const QString& s);
};

// The animated picture at the top of each pop-up.
class Hero : public QWidget {
    Q_OBJECT
public:
    enum Mode { Arrow, Ring, Check, Spinner, Done, Warn };
    explicit Hero(QWidget* parent = nullptr);
    void setColors(const Colors& c) { m_c = c; update(); }
    void setMode(Mode m);                      // plays the mode's intro (check draws, rings burst…)
    Mode mode() const { return m_mode; }
    void setProgress(double frac);             // Ring: 0…1, eased towards the new value; <0 = unknown
    void setCenterText(const QString& big, const QString& small) { m_big = big; m_small = small; update(); }
    void setReducedMotion(bool r) { m_reduced = r; }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Colors m_c;
    Mode m_mode = Arrow;
    double m_intro = 1.0, m_shown = 0.0, m_target = 0.0, m_spin = 0.0;
    QString m_big, m_small;
    bool m_reduced = false;
    QVariantAnimation* m_introAnim = nullptr;
    QTimer* m_tick = nullptr;                  // spinner + smooth ring
};

// Big version number that zooms in from large and blurred (2.4x → 1x).
class ZoomText : public QWidget {
    Q_OBJECT
public:
    explicit ZoomText(QWidget* parent = nullptr);
    void setText(const QString& t) { m_text = t; updateGeometry(); update(); }
    void setColor(const QColor& c) { m_color = c; update(); }
    void play(int delayMs, bool reduced);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString m_text; QColor m_color; double m_t = 1.0;
    QVariantAnimation* m_anim = nullptr;
};

// Frameless pop-up card: rounded 22 px, soft shadow, hero + title + text + buttons.
// popIn / popOut zoom the whole card (snapshot scaled 0.82 → 1 with a fade); swap() cross-fades
// to new content and glides to the new height.
class Sheet : public QWidget {
    Q_OBJECT
public:
    explicit Sheet(const Colors& c, QWidget* parent = nullptr, bool onTop = false);
    Hero* hero() const { return m_hero; }
    ZoomText* bigText() const { return m_zoom; }
    QLabel* title() const { return m_title; }
    QLabel* text() const { return m_text; }
    QLabel* detail() const { return m_detail; }
    QLabel* link() const { return m_link; }    // "Show details ▾"
    QWidget* notes() const { return m_notes; } // "What's new" panel
    QPushButton* primary() const { return m_primary; }
    QPushButton* secondary() const { return m_secondary; }
    QPushButton* tertiary() const { return m_tertiary; }
    void setNotes(const QStringList& lines, const QString& heading);
    void setButtons(const QString& primary, const QString& secondary = {}, const QString& tertiary = {});
    void setReducedMotion(bool r) { m_reduced = r; m_hero->setReducedMotion(r); }
    const Colors& colors() const { return m_c; }

    void placeCenter(const QPoint& globalCenter);   // card centre on screen
    QPoint cardCenter() const;
    void popIn();                                   // show with the zoom-in
    void popOut(std::function<void()> done = {});   // zoom-out, then hide
    void swap(const std::function<void()>& change); // cross-fade to the content set in change()
    void lift();                                    // "installing": rises 14 px, accent outline
    void relayout();                                // after text changes: new height, same centre
protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
private:
    QPixmap snapshot();
    void runScale(double from, double to, double opFrom, double opTo, int ms, bool hideAfter, std::function<void()> done);
    Colors m_c;
    QWidget* m_body = nullptr;
    QWidget* m_cover = nullptr;      // old content fading out during swap()
    Hero* m_hero = nullptr;
    ZoomText* m_zoom = nullptr;
    QLabel *m_title = nullptr, *m_text = nullptr, *m_detail = nullptr, *m_link = nullptr;
    QWidget* m_notes = nullptr;
    QPushButton *m_primary = nullptr, *m_secondary = nullptr, *m_tertiary = nullptr;
    QPixmap m_snap;                   // whole card while zooming
    double m_scale = 1.0, m_opacity = 1.0, m_glow = 0.0;
    bool m_zooming = false, m_reduced = false;
    QPointer<QVariantAnimation> m_anim;
};

constexpr int kShadow = 30;          // transparent margin around the card for the shadow
constexpr int kCardWidth = 440;

// Standalone "Installing update…" pop-up (PacitumUpdateHelper.exe, or the browser binary started
// with --update-overlay on Linux). Arguments:
//   --pid N            browser process to wait for (killed after 25 s if it hangs while quitting)
//   --installer PATH   Windows: Setup to run silently once the browser is gone
//   --relaunch PATH    start this program when the browser is gone (Linux AppImage)
//   --app PATH         the browser, for "Open Pacitum" if something goes wrong
//   --version V --cx X --cy Y --colors C --ready FILE --marker FILE --log FILE [--reduced]
// The pop-up stays until the new version writes the marker file (its first window is up).
int runInstallOverlay(const QStringList& args);

}
