#include "ThemeManager.h"
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QGuiApplication>

static PacitumPalette darkPalette() {
    return {"#0E1014","#1C1F26","#171A20","#1C1F26","#14161B","#232730","#2B303A","#2C313C",
            "#E6E8EE","#9AA1AF","#5A616E","#2DD4BF","#0B1F1D","rgba(45,212,191,0.16)","#34D399","#F87171"};
}
static PacitumPalette lightPalette() {
    return {"#E7EAF0","#FFFFFF","#F3F5F8","#FFFFFF","#F6F7F9","#EEF1F5","#E3E7ED","#DCE0E7",
            "#1A1D23","#5F6672","#A9AFB9","#0D9488","#FFFFFF","rgba(13,148,136,0.12)","#059669","#DC2626"};
}

ThemeManager::ThemeManager(bool darkMode) : m_darkMode(darkMode) { rebuild(); }

void ThemeManager::setDarkMode(bool enabled) { m_darkMode = enabled; rebuild(); }

void ThemeManager::rebuild() {
    m_pal = m_darkMode ? darkPalette() : lightPalette();
    if (!m_accentOverride.isEmpty()) {
        QColor c(m_accentOverride);
        if (c.isValid()) {
            m_pal.accent = c.name();
            m_pal.accentSoft = QStringLiteral("rgba(%1,%2,%3,0.16)").arg(c.red()).arg(c.green()).arg(c.blue());
            m_pal.onAccent = c.lightness() > 140 ? QStringLiteral("#0B1F1D") : QStringLiteral("#FFFFFF");
        }
    }
    if (m_private) m_pal.frame = m_darkMode ? QStringLiteral("#1E1B2E") : QStringLiteral("#DCD8EE");
}

QIcon ThemeManager::icon(const QString& name, const QString& color, int size, const QString& disabledColor) {
    QFile f(QStringLiteral(":/icons/ui/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray src = f.readAll();
    auto render = [&](const QString& col) {
        QByteArray svg = src; svg.replace("currentColor", col.toUtf8());
        QSvgRenderer r(svg);
        const qreal dpr = qMax<qreal>(1.0, qGuiApp ? qGuiApp->devicePixelRatio() : 1.0);
        const int px = qRound(size * qMax<qreal>(dpr, 2.0));
        QPixmap pm(px, px); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing); r.render(&p); p.end();
        return pm;
    };
    QIcon ic;
    ic.addPixmap(render(color), QIcon::Normal);
    ic.addPixmap(render(color), QIcon::Active);
    if (!disabledColor.isEmpty()) ic.addPixmap(render(disabledColor), QIcon::Disabled);
    return ic;
}

QString ThemeManager::mainWindowStyle() const {
    const auto& p = m_pal;
    QString s = QStringLiteral(R"(
QWidget { color: @text; font-family: "Inter", "Segoe UI", "Cantarell", sans-serif; font-size: 13px; }
QMainWindow, QDialog { background: @page; }
#TabStrip { background: @frame; }
#TabStrip QToolButton { background: transparent; border: none; border-radius: 7px; }
#TabStrip QToolButton:hover { background: @surface; }
#TabStrip QToolButton:pressed { background: @surfaceHover; }
#NavToolbar { background: @toolbar; }
#NavToolbar QToolButton { background: transparent; border: none; border-radius: 8px; padding: 0; }
#NavToolbar QToolButton:hover { background: @surface; }
#NavToolbar QToolButton:pressed { background: @surfaceHover; }
#NavToolbar QToolButton:focus { border: 2px solid @accent; }
#NavToolbar QToolButton::menu-indicator { image: none; width: 0; }
#Avatar { border-radius: 12px; color: #FFFFFF; font-weight: 600; font-size: 11px;
          background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #F59E0B, stop:1 #EC4899); }
BookmarksBar { background: @toolbar; border-bottom: 1px solid @border; }
BookmarksBar QScrollArea, BookmarksBar QScrollArea > QWidget > QWidget { background: @toolbar; border: none; }
BookmarksBar QPushButton { background: transparent; border: none; border-radius: 6px; padding: 0 8px;
          height: 26px; min-height: 26px; max-width: 160px; font-size: 12px; color: @text; text-align: left; }
BookmarksBar QPushButton:hover { background: @surface; }
BookmarksBar QPushButton:pressed { background: @surfaceHover; }
QMenu { background: @toolbar; border: 1px solid @border; border-radius: 12px; padding: 6px; }
QMenu::item { height: 32px; padding: 0 10px; border-radius: 6px; background: transparent; color: @text; }
QMenu::item:selected { background: @surface; }
QMenu::item:disabled { color: @disabled; }
QMenu::icon { padding-left: 10px; }
QMenu::separator { height: 1px; background: @border; margin: 6px 4px; }
QMenu QToolButton { background: transparent; border: none; border-radius: 6px; }
QMenu QToolButton:hover { background: @surface; }
QMenu QLabel { background: transparent; }
QPushButton { background: @toolbar; border: 1px solid @border; border-radius: 8px; padding: 0 16px; min-height: 32px; color: @text; }
QPushButton:hover { background: @surface; }
QPushButton:pressed { background: @surfaceHover; }
QPushButton:disabled { color: @disabled; }
QPushButton:default { background: @accent; color: @onAccent; border: none; font-weight: 600; }
QLineEdit, QComboBox, QSpinBox { background: @surface; border: 1px solid transparent; border-radius: 8px; min-height: 30px; padding: 0 10px; color: @text; selection-background-color: @accent; selection-color: @onAccent; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover { border: 1px solid @border; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 2px solid @accent; background: @toolbar; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: @toolbar; border: 1px solid @border; selection-background-color: @surface; selection-color: @text; outline: 0; }
QCheckBox { spacing: 10px; background: transparent; }
QCheckBox::indicator { width: 36px; height: 20px; border-radius: 10px; background: @disabled; }
QCheckBox::indicator:checked { background: @accent; }
QGroupBox { background: @toolbar; border: 1px solid @border; border-radius: 12px; margin-top: 28px; padding: 14px 20px 10px 20px; font-weight: 400; }
QGroupBox::title { subcontrol-origin: margin; left: 2px; top: 4px; padding: 0; font-size: 15px; font-weight: 600; color: @text; }
QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QRadioButton { background: transparent; }
QTabWidget#SettingsTabs::pane { border: none; background: @page; }
QTabWidget#SettingsTabs QTabBar::tab { background: transparent; color: @text; height: 36px; padding: 0 14px; margin: 8px 2px 0 2px; border-radius: 8px; }
QTabWidget#SettingsTabs QTabBar::tab:selected { background: @accentSoft; color: @accent; font-weight: 600; }
QTabWidget#SettingsTabs QTabBar::tab:hover:!selected { background: @surface; }
QStatusBar { background: @toolbar; color: @textSecondary; border-top: 1px solid @border; font-size: 12px; }
QStatusBar QLabel { background: transparent; color: @textSecondary; }
QToolTip { background: @toolbar; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 8px; }
QProgressBar { background: transparent; border: none; }
QProgressBar::chunk { background: @accent; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QSplitter::handle { background: @border; }
QListWidget, QTableWidget, QTreeWidget { background: @toolbar; border: 1px solid @border; border-radius: 12px; alternate-background-color: @page; }
QHeaderView::section { background: @toolbar; color: @textSecondary; border: none; border-bottom: 1px solid @border; padding: 6px; }
)");
    const QList<QPair<QString, QString>> repl = {
        {"@surfaceHover", p.surfaceHover}, {"@textSecondary", p.textSecondary}, {"@accentSoft", p.accentSoft},
        {"@onAccent", p.onAccent}, {"@tabHover", p.tabHover}, {"@frame", p.frame}, {"@toolbar", p.toolbar},
        {"@page", p.page}, {"@surface", p.surface}, {"@border", p.border}, {"@text", p.text},
        {"@disabled", p.disabled}, {"@accent", p.accent}, {"@tab", p.tab}};
    for (auto& r : repl) s.replace(r.first, r.second);
    return s;
}

QString ThemeManager::tabWidgetStyle(bool vertical) const {
    return vertical ? verticalTabStyle() : horizontalTabStyle();
}

QString ThemeManager::horizontalTabStyle() const {
    const auto& p = m_pal;
    return QStringLiteral(R"(
QTabWidget::pane { border: none; background: %1; }
QTabWidget::tab-bar { left: 0; }
QTabBar { background: %2; qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: %3; height: 32px; min-width: 72px; max-width: 220px; width: 220px;
               padding: 0 8px 0 12px; margin-right: 2px; margin-top: 8px; text-align: left;
               border-top-left-radius: 8px; border-top-right-radius: 8px; }
QTabBar::tab:selected { background: %4; color: %5; }
QTabBar::tab:hover:!selected { background: %6; }
QTabBar::close-button { image: url(%7); subcontrol-position: right; width: 14px; height: 14px; padding: 3px; border-radius: 5px; }
QTabBar::close-button:hover { background: %8; }
QTabBar QToolButton { background: %2; border: none; }
)").arg(p.page, p.frame, p.textSecondary, p.tab, p.text, p.tabHover,
         m_darkMode ? QStringLiteral(":/icons/ui/close-dark.svg") : QStringLiteral(":/icons/ui/close-light.svg"),
         p.surfaceHover);
}

QString ThemeManager::verticalTabStyle() const {
    const auto& p = m_pal;
    return QStringLiteral(R"(
QTabWidget::pane { border: none; background: %1; }
QTabBar { background: %2; }
QTabBar::tab { background: transparent; color: %3; padding: 0 10px; margin: 2px 6px; border-radius: 8px; min-height: 36px; width: 200px; text-align: left; }
QTabBar::tab:selected { background: %4; color: %5; }
QTabBar::tab:hover:!selected { background: %6; }
QTabBar::close-button { image: url(%7); width: 14px; height: 14px; padding: 3px; border-radius: 5px; }
QTabBar::close-button:hover { background: %8; }
)").arg(p.page, p.frame, p.textSecondary, p.tab, p.text, p.tabHover,
         m_darkMode ? QStringLiteral(":/icons/ui/close-dark.svg") : QStringLiteral(":/icons/ui/close-light.svg"),
         p.surfaceHover);
}

QString ThemeManager::addressBarStyle() const {
    const auto& p = m_pal;
    return QStringLiteral(R"(
#Omnibox { background: %1; border: 1px solid transparent; border-radius: 17px; }
#Omnibox:hover { border: 1px solid %2; }
#Omnibox[focused="true"] { background: %3; border: 2px solid %4; }
#Omnibox QLineEdit { background: transparent; border: none; padding: 0; min-height: 0; font-size: 14px; color: %5;
                     selection-background-color: %4; selection-color: %6; }
#Omnibox QLineEdit:focus { border: none; background: transparent; }
#Omnibox QLabel { background: transparent; border: none; }
#Omnibox QToolButton { background: transparent; border: none; border-radius: 14px; }
#Omnibox QToolButton:hover { background: %7; }
)").arg(p.surface, p.border, p.toolbar, p.accent, p.text, p.onAccent, p.surfaceHover);
}
