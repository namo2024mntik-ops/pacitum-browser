#include "SyncGuide.h"
#include "SyncManager.h"
#include "AppSettings.h"
#include "ThemeManager.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <cmath>

namespace {
const QRegularExpression kClientId(QStringLiteral("\\b(\\d{6,}-[a-z0-9]+\\.apps\\.googleusercontent\\.com)\\b"));
const QRegularExpression kSecret(QStringLiteral("\\b(GOCSPX-[A-Za-z0-9_-]{10,})\\b"));
QColor withAlpha(QColor c, double a) { c.setAlphaF(a); return c; }
}

// ============================================================================================ picture
GuidePicture::GuidePicture(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_tick = new QTimer(this); m_tick->setInterval(33);
    connect(m_tick, &QTimer::timeout, this, [this]{ m_pulse = std::fmod(m_pulse + 0.033 / 1.6, 1.0); update(); });
    m_cycle = new QTimer(this); m_cycle->setInterval(2600);   // highlights light up in turn
    connect(m_cycle, &QTimer::timeout, this, [this]{ if (m_hover < 0 && !m_spots.isEmpty()) setActive((m_active + 1) % m_spots.size()); });
}

void GuidePicture::set(const QString& resource, const QVector<Spot>& spots) {
    m_pm = QPixmap(resource);
    m_spots = spots; m_active = 0; m_hover = -1;
    if (Pacitum::reducedMotion()) { m_tick->stop(); m_cycle->stop(); } else { m_tick->start(); m_cycle->start(); }
    updateGeometry(); update();
}

void GuidePicture::setActive(int i) {
    if (i < 0 || i >= m_spots.size() || i == m_active) return;
    m_active = i; update();
    emit activeChanged(i);
}

int GuidePicture::heightForWidth(int w) const {
    if (m_pm.isNull()) return 0;
    return qRound(w * double(m_pm.height()) / m_pm.width());
}

QRectF GuidePicture::imageRect() const {
    if (m_pm.isNull()) return {};
    const double ar = double(m_pm.height()) / m_pm.width();
    double w = width() - 2, h = w * ar;
    if (h > height() - 2) { h = height() - 2; w = h / ar; }
    return QRectF((width() - w) / 2, 1, w, h);
}

QRectF GuidePicture::spotRect(int i) const {
    const QRectF img = imageRect(); const QRectF& f = m_spots[i].r;
    return QRectF(img.x() + f.x() * img.width(), img.y() + f.y() * img.height(), f.width() * img.width(), f.height() * img.height()).adjusted(-5, -4, 5, 4);
}

void GuidePicture::paintEvent(QPaintEvent*) {
    if (m_pm.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto& pal = ThemeManager::current();
    const QRectF img = imageRect();
    QPainterPath clip; clip.addRoundedRect(img, 12, 12);
    p.save(); p.setClipPath(clip); p.drawPixmap(img, m_pm, m_pm.rect()); p.restore();
    p.setPen(QPen(QColor(pal.border), 1)); p.setBrush(Qt::NoBrush); p.drawRoundedRect(img.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

    const QColor acc(pal.accent);
    int n = 0;
    for (int i = 0; i < m_spots.size(); ++i) {
        const QRectF r = spotRect(i);
        const bool hot = i == (m_hover >= 0 ? m_hover : m_active);
        const bool info = m_spots[i].id == QLatin1String("p");
        const double rad = qMin(r.height() / 2, 12.0);
        if (hot) {   // pulse ring growing out of the target
            const double t = m_pulse, g = 2 + 10 * t;
            p.setPen(QPen(withAlpha(acc, 0.55 * (1 - t)), 2)); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(-g, -g, g, g), rad + g, rad + g);
            p.setBrush(withAlpha(acc, 0.12));
        } else p.setBrush(Qt::NoBrush);
        QPen pen(withAlpha(acc, hot ? 1.0 : 0.55), hot ? 2.6 : 1.6);
        if (info) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawRoundedRect(r, rad, rad);
        // number badge on the top-left corner
        const QString label = info ? QStringLiteral("i") : QString::number(++n);
        const QPointF c(r.left() + 2, r.top() + 2);
        p.setPen(QPen(QColor(pal.toolbar), 2)); p.setBrush(hot ? acc : withAlpha(acc, 0.8));
        p.drawEllipse(c, 10, 10);
        QFont f = font(); f.setPixelSize(11); f.setBold(true); p.setFont(f);
        p.setPen(QColor(pal.onAccent));
        p.drawText(QRectF(c.x() - 10, c.y() - 10, 20, 20), Qt::AlignCenter, label);
    }
}

void GuidePicture::mouseMoveEvent(QMouseEvent* e) {
    int hit = -1;
    for (int i = 0; i < m_spots.size(); ++i) if (spotRect(i).adjusted(-6, -6, 6, 6).contains(e->position())) { hit = i; break; }
    if (hit != m_hover) {
        m_hover = hit;
        if (hit >= 0) { setActive(hit); QToolTip::showText(e->globalPosition().toPoint(), m_spots[hit].tip, this); }
        else QToolTip::hideText();
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void GuidePicture::leaveEvent(QEvent*) { m_hover = -1; update(); }

// ============================================================================================ guide
void SyncGuide::open(QWidget* parent) {
    static QPointer<SyncGuide> g;
    if (!g) { g = new SyncGuide(parent); g->setAttribute(Qt::WA_DeleteOnClose); }
    g->show(); g->raise(); g->activateWindow();
}

SyncGuide::SyncGuide(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("SyncGuide"));
    setWindowTitle(tr("Set up Google sync"));
    setAcceptDrops(true);
    resize(960, 700);
    const auto& p = ThemeManager::current();
    setStyleSheet(QStringLiteral(
        "#SyncGuide{background:%1;} QLabel{color:%2;background:transparent;} QLabel#GSub{color:%3;}"
        "QLabel#GTitle{font-size:20px;font-weight:600;} QLabel#GCount{color:%3;font-size:12px;font-weight:600;}"
        "#GRail{background:%4;border-right:1px solid %5;}"
        "QPushButton#GRailBtn{text-align:left;border:none;border-radius:10px;padding:8px 10px;color:%2;background:transparent;}"
        "QPushButton#GRailBtn:hover{background:%6;} QPushButton#GRailBtn:checked{background:%7;font-weight:600;}"
        "QPushButton#GRailBtn:disabled{color:%3;}"
        "QPushButton{min-height:34px;border-radius:17px;padding:0 16px;border:1px solid %5;background:transparent;color:%2;}"
        "QPushButton:hover{background:%6;}"
        "QPushButton#GPrimary{background:%8;color:%9;border:none;font-weight:600;} QPushButton#GPrimary:hover{background:%10;}"
        "QPushButton#GPrimary:disabled{background:%5;color:%3;}"
        "QPushButton#GChip{min-height:28px;border-radius:14px;padding:0 12px;font-family:monospace;}"
        "QLineEdit{min-height:34px;border-radius:8px;border:1px solid %5;padding:0 10px;background:%4;color:%2;}"
        "QLineEdit:focus{border:1px solid %8;}"
        "#GDrop{border:2px dashed %5;border-radius:14px;background:%4;} #GDrop[hot=\"true\"]{border-color:%8;background:%7;}"
        "QScrollArea{background:transparent;border:none;} #GPage{background:transparent;}")
        .arg(p.page, p.text, p.textSecondary, p.toolbar, p.border, p.surfaceHover, p.accentSoft, p.accent, p.onAccent, p.accentHover));

    auto spots = [](std::initializer_list<GuidePicture::Spot> l){ return QVector<GuidePicture::Spot>(l); };
    const QString proj = tr("Check that your new project is selected here");
    m_steps = {
        {tr("Create a project"),
         tr("Google keeps Pacitum's sign-in settings in a small project of your own. It's free and takes a few seconds."),
         QStringLiteral("https://console.cloud.google.com/projectcreate"), QStringLiteral(":/sync-guide/step1-project.png"),
         {QStringLiteral("Pacitum Browser")},
         spots({{QStringLiteral("1"), {0.0682, 0.2426, 0.4091, 0.0778}, tr("Type a name, for example Pacitum Browser")},
                {QStringLiteral("2"), {0.0682, 0.5426, 0.1019, 0.0667}, tr("Click Create and wait a few seconds")}})},
        {tr("Turn on the Drive API"),
         tr("Sync keeps its data in a hidden Pacitum area of your Drive, so the Drive API has to be on for your project."),
         QStringLiteral("https://console.cloud.google.com/apis/library/drive.googleapis.com"), QStringLiteral(":/sync-guide/step2-drive.png"), {},
         spots({{QStringLiteral("p"), {0.1859, 0.0139, 0.1732, 0.0593}, proj},
                {QStringLiteral("1"), {0.1886, 0.3741, 0.1030, 0.0667}, tr("Click Enable (it says Manage when it's already on)")}})},
        {tr("Set up the sign-in screen"),
         tr("This is the page Google shows when you sign in. Click Get started, then fill in the four short parts."),
         QStringLiteral("https://console.cloud.google.com/auth/overview"), QStringLiteral(":/sync-guide/step3-branding.png"),
         {QStringLiteral("Pacitum Browser")},
         spots({{QStringLiteral("p"), {0.1859, 0.0139, 0.1732, 0.0593}, proj},
                {QStringLiteral("1"), {0.2886, 0.2926, 0.3750, 0.0778}, tr("App name: Pacitum Browser. Support e-mail: pick your own")},
                {QStringLiteral("2"), {0.3775, 0.5370, 0.0856, 0.0278}, tr("Audience: choose External")},
                {QStringLiteral("3"), {0.2886, 0.7148, 0.3752, 0.0352}, tr("Contact e-mail: yours. Then tick the box to agree")},
                {QStringLiteral("4"), {0.4222, 0.7722, 0.1019, 0.0667}, tr("Click Create")}})},
        {tr("Add who may sign in"),
         tr("While the app is in Testing, only people on this list can sign in (up to 100). Add your own Gmail address, and anyone you share this Pacitum with."),
         QStringLiteral("https://console.cloud.google.com/auth/audience"), QStringLiteral(":/sync-guide/step4-testers.png"), {},
         spots({{QStringLiteral("p"), {0.1859, 0.0139, 0.1732, 0.0593}, proj},
                {QStringLiteral("1"), {0.2477, 0.4593, 0.1449, 0.0667}, tr("Under Test users, click Add users")},
                {QStringLiteral("2"), {0.6614, 0.3241, 0.2864, 0.0778}, tr("Type every Gmail address that will use Pacitum sync")},
                {QStringLiteral("3"), {0.8668, 0.4352, 0.0810, 0.0667}, tr("Click Save")}})},
        {tr("Create the desktop client"),
         tr("This gives Pacitum its own key for signing in. Choose Desktop app, create it, and download the JSON file. Keep the download: Google shows the secret in full only now."),
         QStringLiteral("https://console.cloud.google.com/auth/clients/create"), QStringLiteral(":/sync-guide/step5-client.png"),
         {QStringLiteral("Pacitum")},
         spots({{QStringLiteral("p"), {0.1859, 0.0139, 0.1732, 0.0593}, proj},
                {QStringLiteral("1"), {0.2477, 0.2037, 0.3750, 0.0778}, tr("Application type: Desktop app. Name: Pacitum")},
                {QStringLiteral("2"), {0.2477, 0.4259, 0.1019, 0.0667}, tr("Click Create")},
                {QStringLiteral("3"), {0.4136, 0.5926, 0.1960, 0.0667}, tr("Click Download JSON. Pacitum reads this file in the next step")}})},
        {tr("Connect Pacitum"), tr("Give Pacitum the file you just downloaded. Nothing is sent anywhere: it stays in this profile."), {}, {}, {}, {}},
        {tr("Sign in"),
         tr("Pacitum opens Google in your browser. Because the app is yours and still in Testing, Google warns that it isn't verified. That's expected."),
         {}, QStringLiteral(":/sync-guide/step7-consent.png"), {},
         spots({{QStringLiteral("1"), {0.0864, 0.5949, 0.1067, 0.0667}, tr("It's your own app: click Continue")},
                {QStringLiteral("2"), {0.5455, 0.4685, 0.3682, 0.0630}, tr("Tick this box. Without it sync can't save anything")},
                {QStringLiteral("3"), {0.8069, 0.6204, 0.1067, 0.0667}, tr("Click Continue, then come back to Pacitum")}})},
    };

    auto* root = new QHBoxLayout(this); root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    auto* rail = new QWidget(this); rail->setObjectName(QStringLiteral("GRail")); rail->setAttribute(Qt::WA_StyledBackground); rail->setFixedWidth(236);
    auto* rl = new QVBoxLayout(rail); rl->setContentsMargins(16, 22, 16, 18); rl->setSpacing(4);
    buildRail(rl);
    root->addWidget(rail);

    auto* right = new QWidget(this);
    auto* r = new QVBoxLayout(right); r->setContentsMargins(28, 22, 28, 18); r->setSpacing(10);
    m_counter = new QLabel(right); m_counter->setObjectName(QStringLiteral("GCount"));
    r->addWidget(m_counter);
    m_stack = new QStackedWidget(right);
    for (int i = 0; i < m_steps.size(); ++i) m_stack->addWidget(i == 5 ? buildConnect() : i == 6 ? buildSignIn() : buildStep(i));
    r->addWidget(m_stack, 1);
    auto* bar = new QHBoxLayout(); bar->setSpacing(8);
    m_open = new QPushButton(tr("Open this page ↗"), right); m_open->setObjectName(QStringLiteral("GPrimary"));
    m_back = new QPushButton(tr("Back"), right);
    m_next = new QPushButton(tr("Done, next step"), right);
    for (auto* b : {m_open, m_back, m_next}) b->setCursor(Qt::PointingHandCursor);
    bar->addWidget(m_open); bar->addStretch(1); bar->addWidget(m_back); bar->addWidget(m_next);
    r->addLayout(bar);
    root->addWidget(right, 1);

    connect(m_open, &QPushButton::clicked, this, [this]{ QDesktopServices::openUrl(QUrl(m_steps[m_cur].url)); });
    connect(m_back, &QPushButton::clicked, this, [this]{ go(m_cur - 1); });
    connect(m_next, &QPushButton::clicked, this, [this]{ if (m_cur == m_steps.size() - 1) accept(); else go(m_cur + 1); });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &SyncGuide::onClipboard);
    connect(SyncManager::instance(), &SyncManager::changed, this, &SyncGuide::refreshSignIn);

    auto* sm = SyncManager::instance();
    m_reached = qBound(0, AppSettings::get(QStringLiteral("sync_guide_step")).toInt(0), int(m_steps.size()) - 1);
    if (sm->hasClient()) m_reached = int(m_steps.size()) - 1;
    go(sm->hasClient() ? int(m_steps.size()) - 1 : m_reached);
}

void SyncGuide::buildRail(QVBoxLayout* l) {
    auto* h = new QLabel(tr("Set up Google sync")); QFont f = h->font(); f.setPixelSize(16); f.setWeight(QFont::DemiBold); h->setFont(f);
    auto* sub = new QLabel(tr("About 5 minutes, once. You need a Google account; everything here is free."));
    sub->setObjectName(QStringLiteral("GSub")); sub->setWordWrap(true);
    l->addWidget(h); l->addWidget(sub); l->addSpacing(14);
    for (int i = 0; i < m_steps.size(); ++i) {
        auto* b = new QPushButton(m_steps[i].title); b->setObjectName(QStringLiteral("GRailBtn"));
        b->setCheckable(true); b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, i]{ go(i); });
        m_rail << b; l->addWidget(b);
    }
    l->addStretch(1);
    auto* why = new QLabel(tr("Why is this needed? Google lets each copy of an open-source app sign in only with its owner's own key. "
                              "Builds that come with a key built in skip straight to Sign in."));
    why->setObjectName(QStringLiteral("GSub")); why->setWordWrap(true);
    QFont wf = why->font(); wf.setPixelSize(11); why->setFont(wf);
    l->addWidget(why);
}

void SyncGuide::refreshRail() {
    const auto& p = ThemeManager::current();
    for (int i = 0; i < m_rail.size(); ++i) {
        auto* b = m_rail[i];
        const bool done = i < m_reached || (i == 5 && SyncManager::instance()->hasClient()) || (i == 6 && SyncManager::instance()->signedIn());
        b->setText(QStringLiteral("%1  %2").arg(done && i != m_cur ? QStringLiteral("✓") : QString::number(i + 1), m_steps[i].title));
        b->setChecked(i == m_cur);
        b->setEnabled(i <= m_reached || i == m_cur);
        b->setStyleSheet(done && i != m_cur ? QStringLiteral("color:%1;").arg(p.accent) : QString());
    }
}

QWidget* SyncGuide::buildStep(int i) {
    const Step& s = m_steps[i];
    auto* scroll = new QScrollArea(); scroll->setWidgetResizable(true);
    auto* page = new QWidget(); page->setObjectName(QStringLiteral("GPage"));
    auto* l = new QVBoxLayout(page); l->setContentsMargins(0, 0, 4, 0); l->setSpacing(10);
    auto* t = new QLabel(s.title); t->setObjectName(QStringLiteral("GTitle")); l->addWidget(t);
    auto* b = new QLabel(s.body); b->setObjectName(QStringLiteral("GSub")); b->setWordWrap(true); l->addWidget(b);
    if (!s.copies.isEmpty()) {
        auto* row = new QHBoxLayout(); row->setSpacing(8);
        auto* lab = new QLabel(tr("Copy:")); lab->setObjectName(QStringLiteral("GSub")); row->addWidget(lab);
        for (const QString& c : s.copies) {
            auto* chip = new QPushButton(c + QStringLiteral("  ⧉")); chip->setObjectName(QStringLiteral("GChip")); chip->setCursor(Qt::PointingHandCursor);
            chip->setToolTip(tr("Copy \"%1\"").arg(c));
            connect(chip, &QPushButton::clicked, chip, [chip, c]{
                QApplication::clipboard()->setText(c);
                chip->setText(QObject::tr("Copied ✓"));
                QTimer::singleShot(1400, chip, [chip, c]{ chip->setText(c + QStringLiteral("  ⧉")); });
            });
            row->addWidget(chip);
        }
        row->addStretch(1); l->addLayout(row);
    }
    auto* pic = new GuidePicture(); pic->set(s.picture, s.spots);
    l->addWidget(pic);
    auto* cap = new QLabel(tr("Drawing of the Google page; yours may look a little different. Hover a number to see what to do."));
    cap->setObjectName(QStringLiteral("GSub")); QFont cf = cap->font(); cf.setPixelSize(11); cap->setFont(cf);
    l->addWidget(cap);
    // numbered list under the picture, the active line follows the highlight
    QVector<QLabel*> lines;
    int n = 0;
    for (const auto& sp : s.spots) {
        const QString num = sp.id == QLatin1String("p") ? QStringLiteral("i") : QString::number(++n);
        auto* line = new QLabel(QStringLiteral("<b>%1</b>&nbsp;&nbsp;%2").arg(num, sp.tip.toHtmlEscaped()));
        line->setWordWrap(true); lines << line; l->addWidget(line);
    }
    auto mark = [lines](int a){
        for (int k = 0; k < lines.size(); ++k) {
            QFont f = lines[k]->font(); f.setWeight(k == a ? QFont::DemiBold : QFont::Normal); lines[k]->setFont(f);
            lines[k]->setObjectName(k == a ? QString() : QStringLiteral("GSub"));
            lines[k]->style()->unpolish(lines[k]); lines[k]->style()->polish(lines[k]);
        }
    };
    connect(pic, &GuidePicture::activeChanged, page, mark);
    mark(0);
    l->addStretch(1);
    scroll->setWidget(page);
    return scroll;
}

QWidget* SyncGuide::buildConnect() {
    const Step& s = m_steps[5];
    auto* page = new QWidget(); page->setObjectName(QStringLiteral("GPage"));
    auto* l = new QVBoxLayout(page); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(12);
    auto* t = new QLabel(s.title); t->setObjectName(QStringLiteral("GTitle")); l->addWidget(t);
    auto* b = new QLabel(s.body); b->setObjectName(QStringLiteral("GSub")); b->setWordWrap(true); l->addWidget(b);

    m_drop = new QWidget(); m_drop->setObjectName(QStringLiteral("GDrop")); m_drop->setAttribute(Qt::WA_StyledBackground);
    m_drop->setMinimumHeight(170);
    auto* dl = new QVBoxLayout(m_drop); dl->setAlignment(Qt::AlignCenter); dl->setSpacing(8);
    auto* icon = new QLabel(QStringLiteral("⤓")); QFont fi = icon->font(); fi.setPixelSize(30); icon->setFont(fi); icon->setAlignment(Qt::AlignCenter);
    auto* dt = new QLabel(tr("Drop the downloaded <b>client_secret_….json</b> here")); dt->setAlignment(Qt::AlignCenter);
    auto* pick = new QPushButton(tr("Choose file…")); pick->setCursor(Qt::PointingHandCursor);
    dl->addWidget(icon); dl->addWidget(dt); dl->addWidget(pick, 0, Qt::AlignHCenter);
    l->addWidget(m_drop);
    connect(pick, &QPushButton::clicked, this, [this]{
        const QString f = QFileDialog::getOpenFileName(this, tr("Client JSON from Google"),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), tr("JSON files (*.json)"));
        if (!f.isEmpty()) readClientFile(f);
    });

    auto* or_ = new QLabel(tr("…or copy the Client ID and Client secret from Google. Pacitum picks them up from the clipboard by itself."));
    or_->setObjectName(QStringLiteral("GSub")); or_->setWordWrap(true); l->addWidget(or_);
    m_id = new QLineEdit(SyncManager::instance()->clientId()); m_id->setPlaceholderText(tr("Client ID  (…apps.googleusercontent.com)"));
    m_secret = new QLineEdit(AppSettings::get(QStringLiteral("sync_client_secret")).toString()); m_secret->setPlaceholderText(tr("Client secret  (GOCSPX-…)"));
    m_secret->setEchoMode(QLineEdit::Password);
    l->addWidget(m_id); l->addWidget(m_secret);
    connect(m_id, &QLineEdit::textChanged, this, &SyncGuide::validate);
    connect(m_secret, &QLineEdit::textChanged, this, &SyncGuide::validate);
    m_connectStatus = new QLabel(); m_connectStatus->setWordWrap(true); l->addWidget(m_connectStatus);
    l->addStretch(1);
    validate();
    return page;
}

QWidget* SyncGuide::buildSignIn() {
    QWidget* w = buildStep(6);
    auto* page = static_cast<QScrollArea*>(w)->widget();
    auto* l = static_cast<QVBoxLayout*>(page->layout());
    auto* row = new QHBoxLayout(); row->setSpacing(12);
    m_signBtn = new QPushButton(tr("Sign in with Google")); m_signBtn->setObjectName(QStringLiteral("GPrimary")); m_signBtn->setCursor(Qt::PointingHandCursor);
    m_signStatus = new QLabel(); m_signStatus->setWordWrap(true);
    row->addWidget(m_signBtn); row->addWidget(m_signStatus, 1);
    l->insertLayout(2, row);
    connect(m_signBtn, &QPushButton::clicked, this, []{ SyncManager::instance()->signIn(); });
    return w;
}

void SyncGuide::go(int i) {
    if (i < 0 || i >= m_steps.size()) return;
    m_cur = i;
    if (i > m_reached) { m_reached = i; AppSettings::set(QStringLiteral("sync_guide_step"), qMin(i, 5)); }
    m_stack->setCurrentIndex(i);
    m_counter->setText(tr("STEP %1 OF %2").arg(i + 1).arg(m_steps.size()));
    m_open->setVisible(!m_steps[i].url.isEmpty());
    m_back->setEnabled(i > 0);
    const bool last = i == m_steps.size() - 1;
    m_next->setText(last ? tr("Close") : i == 5 ? tr("Next") : tr("Done, next step"));
    if (i == 5) m_next->setEnabled(SyncManager::instance()->hasClient());
    else m_next->setEnabled(true);
    m_next->setObjectName(last || i == 5 ? QStringLiteral("GPrimary") : QString());
    m_next->style()->unpolish(m_next); m_next->style()->polish(m_next);
    refreshRail();
    refreshSignIn();
    if (i == 5) onClipboard();
}

bool SyncGuide::readClientFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) {
        m_connectStatus->setText(tr("Couldn't read %1.").arg(QFileInfo(path).fileName())); return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.contains(QStringLiteral("web"))) {
        m_connectStatus->setText(tr("This is a <b>Web application</b> client. Go back one step and create a <b>Desktop app</b> client instead."));
        return false;
    }
    const QJsonObject c = o.value(QStringLiteral("installed")).toObject();
    const QString id = c.value(QStringLiteral("client_id")).toString(), secret = c.value(QStringLiteral("client_secret")).toString();
    if (id.isEmpty()) { m_connectStatus->setText(tr("That file isn't a Google OAuth client file.")); return false; }
    m_id->setText(id); m_secret->setText(secret);
    return true;
}

void SyncGuide::validate() {
    if (!m_id) return;
    const QString id = m_id->text().trimmed(), secret = m_secret->text().trimmed();
    const auto& p = ThemeManager::current();
    const bool idOk = QRegularExpression(QStringLiteral("^\\d{6,}-[a-z0-9]+\\.apps\\.googleusercontent\\.com$")).match(id).hasMatch();
    if (id.isEmpty()) { m_connectStatus->setText({}); }
    else if (!idOk) m_connectStatus->setText(QStringLiteral("<span style=\"color:%1\">%2</span>").arg(p.danger, tr("That doesn't look like a client ID. It ends in .apps.googleusercontent.com")));
    else if (secret.isEmpty()) m_connectStatus->setText(tr("Client ID found. Now the Client secret (it starts with GOCSPX-)."));
    if (idOk && !secret.isEmpty()) {
        SyncManager::instance()->setClient(id, secret);
        m_connectStatus->setText(QStringLiteral("<span style=\"color:%1\">✓ %2</span>").arg(p.accent, tr("Pacitum is connected to your Google project. Continue to sign in.")));
        m_drop->setProperty("hot", true);
    } else m_drop->setProperty("hot", false);
    m_drop->style()->unpolish(m_drop); m_drop->style()->polish(m_drop);
    if (m_cur == 5) m_next->setEnabled(idOk && !secret.isEmpty());
    refreshRail();
}

void SyncGuide::onClipboard() {
    if (m_cur != 5 || !m_id) return;
    const QString text = QApplication::clipboard()->text();
    if (text.size() > 4096) return;
    if (const auto m = kClientId.match(text); m.hasMatch() && m_id->text().trimmed() != m.captured(1)) m_id->setText(m.captured(1));
    if (const auto m = kSecret.match(text); m.hasMatch() && m_secret->text().trimmed() != m.captured(1)) m_secret->setText(m.captured(1));
}

void SyncGuide::refreshSignIn() {
    if (!m_signBtn) return;
    auto* sm = SyncManager::instance();
    const auto& p = ThemeManager::current();
    if (sm->signedIn()) {
        m_signBtn->hide();
        const QString who = sm->account().email.isEmpty() ? tr("your Google account") : sm->account().email;
        m_signStatus->setText(QStringLiteral("<span style=\"color:%1;font-size:15px;\"><b>✓ %2</b></span>").arg(p.accent, tr("Signed in as %1. Sync is on.").arg(who.toHtmlEscaped())));
    } else {
        m_signBtn->show(); m_signBtn->setEnabled(sm->hasClient() && !sm->busy());
        m_signStatus->setText(sm->hasClient() ? sm->status().toHtmlEscaped() : tr("Finish step 6 first."));
    }
    refreshRail();
}

void SyncGuide::dragEnterEvent(QDragEnterEvent* e) {
    if (m_cur == 5 && e->mimeData()->hasUrls()) { e->acceptProposedAction(); m_drop->setProperty("hot", true); m_drop->style()->unpolish(m_drop); m_drop->style()->polish(m_drop); }
}

void SyncGuide::dropEvent(QDropEvent* e) {
    for (const QUrl& u : e->mimeData()->urls()) if (u.isLocalFile() && readClientFile(u.toLocalFile())) { e->acceptProposedAction(); return; }
    validate();
}
