#pragma once
// 1.8.9 Google sync setup guide: Settings › Sync and Google › "Set up Google sync".
//
// Seven steps with a picture of each Google Cloud page. Numbered, pulsing highlights show where to
// click (hover one for its tip; they light up in turn). "Open this page" goes straight to the right
// console page; copy buttons fill in the names. Step 6 takes the downloaded client JSON (drop it,
// pick it, or copy the client ID: the clipboard is watched) and step 7 signs in.
#include <QDialog>
#include <QPixmap>
#include <QVector>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QLineEdit;
class QVariantAnimation;
class QTimer;

class GuidePicture : public QWidget {
    Q_OBJECT
public:
    struct Spot { QString id; QRectF r; QString tip; };   // r: fractions of the picture
    explicit GuidePicture(QWidget* parent = nullptr);
    void set(const QString& resource, const QVector<Spot>& spots);
    void setActive(int i);
    int active() const { return m_active; }
    int count() const { return m_spots.size(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override { return {560, heightForWidth(560)}; }
signals:
    void activeChanged(int i);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
private:
    QRectF imageRect() const;
    QRectF spotRect(int i) const;
    QPixmap m_pm;
    QVector<Spot> m_spots;
    int m_active = 0, m_hover = -1;
    double m_pulse = 0;
    QTimer* m_tick = nullptr;
    QTimer* m_cycle = nullptr;
};

class SyncGuide : public QDialog {
    Q_OBJECT
public:
    explicit SyncGuide(QWidget* parent = nullptr);
    static void open(QWidget* parent);   // one guide at a time
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
private:
    struct Step { QString title, body, url, picture; QStringList copies; QVector<GuidePicture::Spot> spots; };
    void buildRail(QVBoxLayout* l);
    QWidget* buildStep(int i);
    QWidget* buildConnect();
    QWidget* buildSignIn();
    void go(int i);
    void refreshRail();
    bool readClientFile(const QString& path);
    void validate();
    void onClipboard();
    void refreshSignIn();

    QVector<Step> m_steps;
    QVector<QPushButton*> m_rail;
    QStackedWidget* m_stack = nullptr;
    QPushButton *m_back = nullptr, *m_next = nullptr, *m_open = nullptr;
    QLabel* m_counter = nullptr;
    int m_cur = 0, m_reached = 0;
    // connect step
    QLineEdit *m_id = nullptr, *m_secret = nullptr;
    QLabel* m_connectStatus = nullptr;
    QWidget* m_drop = nullptr;
    // sign-in step
    QLabel* m_signStatus = nullptr;
    QPushButton* m_signBtn = nullptr;
};
