#pragma once
// Settings → Site settings (permissions-spec §8): list page, per-permission pages, site details.
#include <QWidget>
#include "SitePermissions.h"
class QStackedWidget;
class QVBoxLayout;

class SitePermissionsPage : public QWidget {
    Q_OBJECT
public:
    explicit SitePermissionsPage(Perm::Store* store, QWidget* parent = nullptr);
    void showList();
    void showKind(Perm::Kind k);
    void showSite(const QString& origin);
signals:
    void settingChanged(const QString& key);
private:
    QWidget* page(const QString& title, QVBoxLayout** l, bool back);
    QWidget* card(QVBoxLayout* l, const QString& heading);
    QWidget* row(QWidget* card, const QString& glyph, const QString& title, const QString& sub, QWidget* control, bool clickable);
    void swapTo(QWidget* w);
    Perm::Store* m_store;
    QStackedWidget* m_stack;
    QWidget* m_current = nullptr;
    QWidget* m_list = nullptr;
};
