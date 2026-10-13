#pragma once
#include <QString>
#include <QColor>
#include <QIcon>

// Pacitum design tokens (design-spec.md §2)
struct PacitumPalette {
    QString frame, tab, tabHover, toolbar, page, surface, surfaceHover, border,
            text, textSecondary, disabled, accent, onAccent, accentSoft, secure, danger;
};

class ThemeManager {
public:
    explicit ThemeManager(bool darkMode = false);

    void setDarkMode(bool enabled);
    bool isDarkMode() const { return m_darkMode; }
    void setPrivate(bool p) { m_private = p; }
    void setAccent(const QString& accent) { m_accentOverride = accent; }
    const PacitumPalette& palette() const { return m_pal; }
    static const PacitumPalette& current();   // palette of the most recently applied theme

    QString mainWindowStyle() const;
    QString tabWidgetStyle(bool vertical) const;
    QString addressBarStyle() const;

    // Load a design SVG icon from :/icons/ui/<name>.svg tinted with color.
    static QIcon icon(const QString& name, const QString& color, int size = 18,
                      const QString& disabledColor = QString());
private:
    static QIcon renderIcon(const QString& name, const QString& color, int size,
                      const QString& disabledColor);
public:
    QIcon icon(const QString& name, int size = 18) const {
        return icon(name, m_pal.text, size, m_pal.disabled);
    }
    QIcon secondaryIcon(const QString& name, int size = 18) const {
        return icon(name, m_pal.textSecondary, size, m_pal.disabled);
    }

private:
    void rebuild();
    QString horizontalTabStyle() const;
    QString verticalTabStyle() const;

    bool m_darkMode;
    bool m_private = false;
    QString m_accentOverride;
    PacitumPalette m_pal;
};
