#include "ThemeManager.h"

ThemeManager::ThemeManager(bool darkMode) : m_darkMode(darkMode) {}

void ThemeManager::setDarkMode(bool enabled) { m_darkMode = enabled; }

QString ThemeManager::mainWindowStyle() const {
    if (m_darkMode) {
        return R"(
            QMainWindow, QWidget { background-color: #1e1e1e; color: #e0e0e0; }
            QMenuBar { background-color: #1e1e1e; color: #e0e0e0; border-bottom: 1px solid #333; }
            QMenuBar::item:selected { background-color: #333; }
            QMenu { background-color: #252525; border: 1px solid #333; color: #e0e0e0; }
            QMenu::item:selected { background-color: #0066cc; color: white; }
            QPushButton { background-color: #333; border: none; border-radius: 6px; padding: 6px 12px; color: #e0e0e0; }
            QPushButton:hover { background-color: #444; }
            QPushButton:pressed { background-color: #555; }
            QPushButton:disabled { color: #666; background-color: #2a2a2a; }
            QStatusBar { background-color: #1e1e1e; color: #888; border-top: 1px solid #333; }
            QToolTip { background-color: #333; color: #e0e0e0; border: 1px solid #555; padding: 4px; }
            QProgressBar { background: #333; border: none; }
            QProgressBar::chunk { background: #0066cc; }
        )";
    }
    return R"(
        QMainWindow, QWidget { background-color: #f5f5f7; color: #1d1d1f; }
        QMenuBar { background-color: #f5f5f7; color: #1d1d1f; border-bottom: 1px solid #d2d2d7; }
        QMenuBar::item:selected { background-color: #e8e8ed; }
        QMenu { background-color: #ffffff; border: 1px solid #d2d2d7; color: #1d1d1f; }
        QMenu::item:selected { background-color: #0066cc; color: white; }
        QPushButton { background-color: #e8e8ed; border: none; border-radius: 6px; padding: 6px 12px; color: #1d1d1f; }
        QPushButton:hover { background-color: #d2d2d7; }
        QPushButton:pressed { background-color: #c1c1c6; }
        QPushButton:disabled { color: #999; background-color: #f5f5f7; }
        QStatusBar { background-color: #f5f5f7; color: #888; border-top: 1px solid #d2d2d7; }
        QToolTip { background-color: #fff; color: #1d1d1f; border: 1px solid #d2d2d7; padding: 4px; }
        QProgressBar { background: #e8e8ed; border: none; }
        QProgressBar::chunk { background: #0066cc; }
    )";
}

QString ThemeManager::tabWidgetStyle(bool vertical) const {
    return vertical ? verticalTabStyle() : horizontalTabStyle();
}

QString ThemeManager::horizontalTabStyle() const {
    if (m_darkMode) {
        return R"(
            QTabWidget::pane { border: none; background: #1e1e1e; }
            QTabBar::tab { background: #252525; color: #8e8e93; padding: 8px 20px; margin-right: 2px; border-top-left-radius: 10px; border-top-right-radius: 10px; min-width: 120px; max-width: 250px; }
            QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; font-weight: bold; }
            QTabBar::tab:hover:!selected { background: #2c2c2e; }
        )";
    }
    return R"(
        QTabWidget::pane { border: none; background: #ffffff; }
        QTabBar::tab { background: #e3e3e8; color: #86868b; padding: 8px 20px; margin-right: 2px; border-top-left-radius: 10px; border-top-right-radius: 10px; min-width: 120px; max-width: 250px; }
        QTabBar::tab:selected { background: #ffffff; color: #1d1d1f; font-weight: bold; }
        QTabBar::tab:hover:!selected { background: #ebebed; }
    )";
}

QString ThemeManager::verticalTabStyle() const {
    if (m_darkMode) {
        return R"(
            QTabWidget::pane { border: none; background: #1e1e1e; }
            QTabBar { width: 200px; }
            QTabBar::tab { background: #252525; color: #8e8e93; padding: 10px 16px; margin-bottom: 2px; border-radius: 8px; min-height: 36px; text-align: left; }
            QTabBar::tab:selected { background: #2c2c2e; color: #ffffff; font-weight: bold; border-left: 3px solid #0066cc; }
            QTabBar::tab:hover:!selected { background: #333; }
        )";
    }
    return R"(
        QTabWidget::pane { border: none; background: #ffffff; }
        QTabBar { width: 200px; }
        QTabBar::tab { background: transparent; color: #86868b; padding: 10px 16px; margin-bottom: 2px; border-radius: 8px; min-height: 36px; text-align: left; }
        QTabBar::tab:selected { background: #e8e8ed; color: #1d1d1f; font-weight: bold; border-left: 3px solid #0066cc; }
        QTabBar::tab:hover:!selected { background: #f0f0f2; }
    )";
}

QString ThemeManager::addressBarStyle() const {
    if (m_darkMode) {
        return R"(
            AddressBar { background-color: #242933; border: 1px solid #3a4250; border-radius: 14px; }
            QLineEdit { background: transparent; border: none; padding: 6px 12px; font-size: 14px; color: #f3f6fb; selection-background-color: #4077c7; }
            QLabel { color: #8fb3e8; font-size: 10px; font-weight: bold; }
        )";
    }
    return R"(
        AddressBar { background-color: #ffffff; border: 1px solid #d9e0ea; border-radius: 14px; }
        QLineEdit { background: transparent; border: none; padding: 6px 12px; font-size: 14px; color: #1e2938; selection-background-color: #b9d4fa; }
        QLabel { color: #2d74c7; font-size: 10px; font-weight: bold; }
    )";
}
