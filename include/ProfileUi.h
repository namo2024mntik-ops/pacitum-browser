#pragma once
#include <QWidget>
#include <QDialog>
#include <QFrame>
#include "ProfileManager.h"

class QLineEdit;
class QLabel;
class QCheckBox;
class QHBoxLayout;
class QVBoxLayout;
class QAbstractButton;

// home-profile-spec §2.1: "Who's browsing?" picker window (startup and "Manage profiles").
class ProfilePicker : public QWidget {
    Q_OBJECT
public:
    enum Mode { Startup, Manage };
    explicit ProfilePicker(Mode mode, QWidget* parent = nullptr);
    // Startup: blocks until a profile is chosen. Returns the id, "guest", or empty if closed.
    static QString pickAtStartup();
signals:
    void chosen(const QString& id);   // id or "guest"
protected:
    void closeEvent(QCloseEvent* e) override;
private:
    void rebuild();
    QWidget* card(const ProfileInfo& p, bool add);
    Mode m_mode;
    QHBoxLayout* m_cards = nullptr;
    QCheckBox* m_startup = nullptr;
    bool m_editAll = false;
};

// §2.3 add / edit profile (live preview on the left).
class ProfileEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfileEditDialog(const ProfileInfo& p, bool create, QWidget* parent = nullptr);
    ProfileInfo result() const { return m_p; }
    static bool editProfile(const QString& id, QWidget* parent);         // true if saved/deleted
    static QString addProfile(QWidget* parent);                         // new id or empty
private:
    void refreshPreview();
    void accept() override;
    ProfileInfo m_p; bool m_create;
    QLineEdit* m_name = nullptr; QLabel* m_error = nullptr;
    QLabel* m_avatar = nullptr; QLabel* m_previewName = nullptr; QWidget* m_mini = nullptr;
    QList<QAbstractButton*> m_colourBtns, m_glyphBtns;
};

// §2.2 toolbar profile switcher popup.
class ProfileSwitcher : public QWidget {
    Q_OBJECT
public:
    explicit ProfileSwitcher(QWidget* parent = nullptr);
    void showUnder(QWidget* anchor);
signals:
    void settingsRequested();
    void manageRequested();
private:
    void build();
    QFrame* m_card = nullptr;
};

// Toolbar avatar with the 1.5 px profile-colour ring (§2.2 trigger).
void paintProfileAvatarLabel(QLabel* label, int avatarSize, qreal dpr);
