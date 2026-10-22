#pragma once
// 1.8.5 built-in tools: screenshot, picture-in-picture window, notes side panel, QR code.
#include <QDialog>
#include <QPixmap>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class QLabel;

// Page QR code (encodes the current address) with Copy / Save.
class QrDialog : public QDialog {
    Q_OBJECT
public:
    QrDialog(const QString& url, QWidget* parent = nullptr);
    static QImage render(const QString& text, int modulePx = 8, int quiet = 4);
private:
    QImage m_img;
};

// Screenshot of the visible page. Drag to select an area (or take everything), then Copy / Save.
// A child overlay over the page area; Esc cancels, Enter copies.
class ScreenshotOverlay : public QWidget {
    Q_OBJECT
public:
    ScreenshotOverlay(const QPixmap& shot, QWidget* parent);
signals:
    void finished(const QString& message);   // empty when cancelled
protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
private:
    QPixmap selected() const;
    void copy();
    void save();
    void layoutBar();
    QPixmap m_shot;
    QRect m_sel;           // in widget coords; empty = whole page
    QPoint m_origin;
    bool m_dragging = false;
    qreal m_flash = 1.0;   // capture flash, fades to 0
    QWidget* m_bar = nullptr;
    QLabel* m_hint = nullptr;
};

// Notes side panel: plain-text notes saved per profile (notes.txt), autosaved.
class NotesPanel : public QWidget {
    Q_OBJECT
public:
    explicit NotesPanel(QWidget* parent = nullptr);
    void focusEditor();
    void flush();                 // save now
signals:
    void closeRequested();
private:
    QPlainTextEdit* m_edit = nullptr;
    QLabel* m_status = nullptr;
    QTimer m_save;
};

// Frameless always-on-top window that holds a tab's web view while its video pops out.
class PipWindow : public QWidget {
    Q_OBJECT
public:
    PipWindow(QWidget* view, const QString& title, QWidget* parentWindow);
signals:
    void returnRequested();
protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    QWidget* m_titleBar = nullptr;
    QPoint m_dragOffset;
    bool m_closingByReturn = false;
    friend class BrowserTab;
};
