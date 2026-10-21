#include <QHash>
#include "ThemeManager.h"
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QGuiApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static PacitumPalette darkPalette() {
    PacitumPalette p;
    // 1.9 refresh: the active tab shares the toolbar colour so tab + toolbar read as one surface.
    p.frame = "#111318"; p.tab = "#1A1D23"; p.tabHover = "#1C1F25"; p.toolbar = "#1A1D23"; p.page = "#121418";
    p.surface = "#24282F"; p.surfaceHover = "#2D323A"; p.border = "#2A2F37"; p.text = "#E8EAEE";
    p.textSecondary = "#A0A6B1"; p.disabled = "#4F555F"; p.accent = "#2DD4BF"; p.onAccent = "#04201D";
    p.accentSoft = "rgba(45,212,191,0.14)"; p.secure = "#34D399"; p.danger = "#F87171";
    p.omnibox = "#23272E"; p.omniboxHover = "#292D35"; p.pressed = "#353B44"; p.borderStrong = "#363C46";
    p.textTertiary = "#7C838F"; p.accentHover = "#5EEAD4"; p.accentPressed = "#14B8A6";
    p.focusRing = "rgba(45,212,191,0.55)"; p.warning = "#FBBF24";
    return p;
}
static PacitumPalette lightPalette() {
    PacitumPalette p;
    p.frame = "#E9ECF1"; p.tab = "#FFFFFF"; p.tabHover = "#DDE1E7"; p.toolbar = "#FFFFFF"; p.page = "#F7F8FA";
    p.surface = "#E9ECF1"; p.surfaceHover = "#DFE3E9"; p.border = "#DDE1E7"; p.text = "#16191E";
    p.textSecondary = "#5B626D"; p.disabled = "#B3B9C2"; p.accent = "#0D9488"; p.onAccent = "#FFFFFF";
    p.accentSoft = "rgba(13,148,136,0.10)"; p.secure = "#059669"; p.danger = "#DC2626";
    p.omnibox = "#F0F2F5"; p.omniboxHover = "#E9ECF1"; p.pressed = "#D3D8E0"; p.borderStrong = "#CDD2DA";
    p.textTertiary = "#8A919C"; p.accentHover = "#0F766E"; p.accentPressed = "#115E59";
    p.focusRing = "rgba(13,148,136,0.45)"; p.warning = "#D97706";
    return p;
}

namespace Pacitum {
static int s_reduceOverride = -1;
void setReducedMotion(bool on) { s_reduceOverride = on ? 1 : 0; }
void clearReducedMotionOverride() { s_reduceOverride = -1; }
bool reducedMotion() {
    if (s_reduceOverride >= 0) return s_reduceOverride == 1;
    static int os = -1;
    if (os < 0) {
        os = qEnvironmentVariableIntValue("PACITUM_REDUCED_MOTION") ? 1 : 0;
#ifdef Q_OS_WIN
        BOOL anim = TRUE;
        if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0) && !anim) os = 1;
#endif
    }
    return os == 1;
}
int motionMs(int ms) { return reducedMotion() ? 0 : ms; }
}

static PacitumPalette s_current = darkPalette();
const PacitumPalette& ThemeManager::current() { return s_current; }

ThemeManager::ThemeManager(bool darkMode) : m_darkMode(darkMode) { rebuild(); }

void ThemeManager::setDarkMode(bool enabled) { m_darkMode = enabled; rebuild(); }

void ThemeManager::rebuild() {
    m_pal = m_darkMode ? darkPalette() : lightPalette();
    if (!m_accentOverride.isEmpty()) {
        QColor c(m_accentOverride);
        if (c.isValid()) {
            m_pal.accent = c.name();
            m_pal.accentSoft = QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(m_darkMode ? 0.14 : 0.10);
            m_pal.focusRing = QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(m_darkMode ? 0.55 : 0.45);
            m_pal.accentHover = (m_darkMode ? c.lighter(125) : c.darker(120)).name();
            m_pal.accentPressed = (m_darkMode ? c.darker(115) : c.darker(140)).name();
            m_pal.onAccent = c.lightness() > 140 ? QStringLiteral("#04201D") : QStringLiteral("#FFFFFF");
        }
    }
    if (m_private) m_pal.frame = m_darkMode ? QStringLiteral("#1C1A2B") : QStringLiteral("#E9E7F5");
    s_current = m_pal;
}

QIcon ThemeManager::icon(const QString& name, const QString& color, int size, const QString& disabledColor) {
    // Tinted SVG icons are requested on every repaint/theme pass; render each variant once.
    static QHash<QString, QIcon> cache;
    const qreal cdpr = qMax<qreal>(1.0, qGuiApp ? qGuiApp->devicePixelRatio() : 1.0);
    const QString key = name + QLatin1Char('|') + color + QLatin1Char('|') + QString::number(size) + QLatin1Char('|') + disabledColor
                      + QLatin1Char('|') + QString::number(cdpr);
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    if (cache.size() > 2000) cache.clear();
    QIcon made = renderIcon(name, color, size, disabledColor);
    cache.insert(key, made);
    return made;
}

QIcon ThemeManager::renderIcon(const QString& name, const QString& color, int size, const QString& disabledColor) {
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
#TabStrip QToolButton { background: transparent; border: none; }
#NavToolbar { background: @toolbar; border: none; }
#NavToolbar QToolButton { background: transparent; border: none; padding: 0; }
#NavToolbar QToolButton::menu-indicator { image: none; width: 0; }
#ChromeDivider { background: @border; border: none; min-height: 1px; max-height: 1px; }
#Avatar { border-radius: 13px; color: #FFFFFF; font-weight: 600; font-size: 11px;
          background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #2DD4BF, stop:1 #3B82F6); }
BookmarksBar { background: @toolbar; border: none; }
BookmarksBar QScrollArea, BookmarksBar QScrollArea > QWidget > QWidget { background: @toolbar; border: none; }
BookmarksBar QPushButton { background: transparent; border: none; border-radius: 8px; padding: 0 10px;
          height: 26px; min-height: 26px; max-width: 160px; font-size: 12px; color: @text; text-align: left; }
BookmarksBar QPushButton:hover { background: @surface; }
BookmarksBar QPushButton:pressed { background: @pressed; }
QMenu { background: @toolbar; border: 1px solid @borderStrong; border-radius: 16px; padding: 6px; }
QMenu::item { height: 34px; padding: 0 12px 0 6px; border-radius: 10px; background: transparent; color: @text; }
QMenu::item:selected { background: @surface; }
QMenu::item:pressed { background: @pressed; }
QMenu::item:disabled { color: @disabled; }
QMenu::icon { padding-left: 10px; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QMenu QToolButton { background: transparent; border: none; border-radius: 15px; }
QMenu QToolButton:hover { background: @surfaceHover; }
QMenu QToolButton:pressed { background: @pressed; }
QMenu QLabel { background: transparent; }
#MenuProfile { background: transparent; border-radius: 8px; }
#MenuProfile:hover { background: @surface; }
#MenuProfileName { font-weight: 600; font-size: 13px; }
#MenuProfileSub { color: @textSecondary; font-size: 12px; }
#MenuAvatar { border-radius: 18px; color: #FFFFFF; font-weight: 600; font-size: 14px;
          background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #2DD4BF, stop:1 #3B82F6); }
#ZoomPill { background: @surface; border-radius: 15px; }
#ZoomPill QLabel { font-weight: 600; font-size: 12px; }
#InfoBarWrap { background: @page; }
#InfoBar { background: @toolbar; border: 1px solid @border; border-radius: 10px; }
#InfoBar QLabel { background: transparent; border: none; font-size: 13px; color: @text; }
#InfoBar QPushButton { min-height: 32px; max-height: 32px; border-radius: 16px; padding: 0 14px; }
#InfoBar QToolButton { background: transparent; border: none; border-radius: 15px; }
#InfoBar QToolButton:hover { background: @surface; }
#InfoBar QToolButton:pressed { background: @pressed; }
QPushButton { background: transparent; border: 1px solid @borderStrong; border-radius: 17px; padding: 0 16px; min-height: 32px; color: @text; }
QPushButton:hover { background: @surface; }
QPushButton:pressed { background: @pressed; }
QPushButton:disabled { color: @disabled; border-color: @border; }
QPushButton:default, QPushButton#PrimaryButton { background: @accent; color: @onAccent; border: none; font-weight: 600; }
QPushButton:default:hover, QPushButton#PrimaryButton:hover { background: @accentHover; }
QPushButton:default:pressed, QPushButton#PrimaryButton:pressed { background: @accentPressed; }
QLineEdit, QComboBox, QSpinBox { background: @surface; border: 1px solid transparent; border-radius: 10px; min-height: 30px; padding: 0 10px; color: @text; selection-background-color: @accent; selection-color: @onAccent; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover { border: 1px solid @border; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 2px solid @accent; background: @toolbar; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: @toolbar; border: 1px solid @border; selection-background-color: @surface; selection-color: @text; outline: 0; }
QCheckBox { spacing: 10px; background: transparent; }
QCheckBox::indicator { width: 36px; height: 20px; border-radius: 10px; background: @disabled; }
QCheckBox::indicator:checked { background: @accent; }
QGroupBox { background: @toolbar; border: 1px solid @border; border-radius: 14px; margin-top: 28px; padding: 14px 20px 10px 20px; font-weight: 400; }
QGroupBox::title { subcontrol-origin: margin; left: 2px; top: 4px; padding: 0; font-size: 15px; font-weight: 600; color: @text; }
QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QRadioButton { background: transparent; }
QTabWidget#SettingsTabs::pane { border: none; background: @page; }
QTabWidget#SettingsTabs QTabBar::tab { background: transparent; color: @text; height: 36px; padding: 0 14px; margin: 8px 2px 0 2px; border-radius: 8px; }
QTabWidget#SettingsTabs QTabBar::tab:selected { background: @accentSoft; color: @accent; font-weight: 600; }
QTabWidget#SettingsTabs QTabBar::tab:hover:!selected { background: @surface; }
#SettingsPage, #SettingsContent, #SettingsScroll, #SettingsScroll > QWidget > QWidget { background: @page; }
#SettingsNav { background: @page; border: none; border-right: 1px solid @border; outline: 0; padding: 0 12px; }
#SettingsNav::item { height: 36px; border-radius: 10px; padding: 0 12px; color: @text; margin: 1px 0; }
#SettingsNav::item:hover { background: @surface; }
#SettingsNav::item:selected { background: @accentSoft; color: @accent; font-weight: 600; }
#SettingsH1 { font-size: 20px; font-weight: 600; background: transparent; }
#SettingsTitle { font-size: 22px; font-weight: 600; background: transparent; }
QLineEdit#SettingsSearch { background: @toolbar; border: 1px solid @border; border-radius: 10px; min-height: 34px; padding: 0 10px; }
QLineEdit#SettingsSearch:focus { border: 2px solid @accent; padding: 0 9px; }
#SettingsH2 { font-size: 15px; font-weight: 600; background: transparent; margin-top: 6px; }
#Card { background: @toolbar; border: 1px solid @border; border-radius: 14px; }
#Card QLabel { background: transparent; }
#Divider { background: @border; border: none; min-height: 1px; max-height: 1px; }
#RowSub { color: @textSecondary; font-size: 12px; }
#Segmented { background: @surface; border-radius: 10px; }
#Segmented QPushButton { background: transparent; border: none; border-radius: 8px; min-height: 30px; padding: 0 14px; }
#Segmented QPushButton:hover { background: transparent; }
#Segmented QPushButton:checked { background: transparent; font-weight: 600; }
#SegThumb { background: @segThumb; border-radius: 8px; }
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
        {"@surfaceHover", p.surfaceHover}, {"@borderStrong", p.borderStrong}, {"@textTertiary", p.textTertiary},
        {"@accentHover", p.accentHover}, {"@accentPressed", p.accentPressed}, {"@pressed", p.pressed}, {"@textSecondary", p.textSecondary}, {"@accentSoft", p.accentSoft},
        {"@onAccent", p.onAccent}, {"@tabHover", p.tabHover}, {"@frame", p.frame}, {"@toolbar", p.toolbar},
        {"@page", p.page}, {"@surface", p.surface}, {"@border", p.border}, {"@text", p.text},
        {"@disabled", p.disabled}, {"@accent", p.accent}, {"@tab", p.tab},
        {"@segThumb", m_darkMode ? p.pressed : p.toolbar}};
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
    QString s = QStringLiteral(R"(
#Omnibox { background: @omnibox; border: 1px solid transparent; border-radius: 12px; }
#Omnibox:hover { background: @omniboxHover; border: 1px solid transparent; }
#Omnibox[focused="true"] { background: @toolbar; border: 2px solid @accent; }
#Omnibox QLineEdit { background: transparent; border: none; padding: 0; min-height: 0; font-size: 14px; color: @text;
                     selection-background-color: @accent; selection-color: @onAccent; }
#Omnibox QLineEdit[urlview="true"] { color: transparent; }
#Omnibox QLineEdit:focus { border: none; background: transparent; }
#Omnibox QLabel { background: transparent; border: none; font-size: 14px; }
#Omnibox QToolButton { background: transparent; border: none; border-radius: 15px; }
#Omnibox QToolButton:hover { background: @surface; }
#Omnibox QToolButton:pressed { background: @pressed; }
#OmniChip { border-radius: 9px; padding: 0 6px; font-size: 12px; }
#OmniChip[kind="danger"] { color: @danger; padding: 0 10px 0 8px; }
#OmniChip[kind="internal"] { color: @accent; padding: 0 10px 0 8px; }
#OmniChip:hover { background: @surface; }
)");
    const QList<QPair<QString, QString>> repl = {
        {"@omniboxHover", p.omniboxHover}, {"@omnibox", p.omnibox}, {"@toolbar", p.toolbar}, {"@borderStrong", p.borderStrong}, {"@border", p.border},
        {"@accent", p.accent}, {"@onAccent", p.onAccent}, {"@text", p.text}, {"@surface", p.surface},
        {"@pressed", p.pressed}, {"@danger", p.danger}};
    for (auto& r : repl) s.replace(r.first, r.second);
    return s;
}
