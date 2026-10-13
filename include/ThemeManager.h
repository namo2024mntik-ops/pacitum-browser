#pragma once
#include <QString>

class ThemeManager {
public:
    explicit ThemeManager(bool darkMode = false);

    void setDarkMode(bool enabled);
    bool isDarkMode() const { return m_darkMode; }

    QString mainWindowStyle() const;
    QString tabWidgetStyle(bool vertical) const;
    QString addressBarStyle() const;

private:
    QString horizontalTabStyle() const;
    QString verticalTabStyle() const;

    bool m_darkMode;
};
