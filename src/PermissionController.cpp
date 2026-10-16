#include "PermissionController.h"
#include "PermissionDevices.h"
#include "ThemeManager.h"
#include "Motion.h"
#include "WebProfile.h"
#include <QWebChannel>
#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineDownloadRequest>
#include <QDesktopServices>
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QUuid>
#include <QApplication>
#include <QPainter>
#ifdef PACITUM_HAVE_SERIALPORT
#include <QSerialPort>
#include <QSerialPortInfo>
#endif

using Perm::Kind;
using Perm::Decision;

bool PermissionController::s_testAutoAnswer = false;
QString PermissionController::s_testAnswer;

static constexpr qint64 kPendingMs = 2 * 60 * 1000;   // §1: pending 2 min, then counts as dismissed

static bool kindFromQt(QWebEnginePermission::PermissionType t, Kind* k, bool* both) {
    *both = false;
    switch (t) {
    case QWebEnginePermission::PermissionType::MediaAudioCapture: *k = Kind::Microphone; return true;
    case QWebEnginePermission::PermissionType::MediaVideoCapture: *k = Kind::Camera; return true;
    case QWebEnginePermission::PermissionType::MediaAudioVideoCapture: *k = Kind::Camera; *both = true; return true;
    case QWebEnginePermission::PermissionType::DesktopVideoCapture:
    case QWebEnginePermission::PermissionType::DesktopAudioVideoCapture: *k = Kind::ScreenShare; return true;
    case QWebEnginePermission::PermissionType::MouseLock: *k = Kind::MouseLock; return true;
    case QWebEnginePermission::PermissionType::Notifications: *k = Kind::Notifications; return true;
    case QWebEnginePermission::PermissionType::Geolocation: *k = Kind::Location; return true;
    case QWebEnginePermission::PermissionType::ClipboardReadWrite: *k = Kind::Clipboard; return true;
    case QWebEnginePermission::PermissionType::LocalFontsAccess: *k = Kind::Fonts; return true;
    default: return false;
    }
}
static bool qtTypeFor(Kind k, QWebEnginePermission::PermissionType* t) {
    switch (k) {
    case Kind::Notifications: *t = QWebEnginePermission::PermissionType::Notifications; return true;
    case Kind::Location: *t = QWebEnginePermission::PermissionType::Geolocation; return true;
    case Kind::Clipboard: *t = QWebEnginePermission::PermissionType::ClipboardReadWrite; return true;
    case Kind::Fonts: *t = QWebEnginePermission::PermissionType::LocalFontsAccess; return true;
    default: return false;
    }
}

// ------------------------------------------------------------------ bridge
PermissionBridge::PermissionBridge(PermissionController* c) : QObject(c), m_c(c) {}
void PermissionBridge::request(int id, const QString& kind, const QString& json) {
    const QJsonObject env = QJsonDocument::fromJson(json.toUtf8()).object();
    // The nonce is injected only into the main frame's shim: requests from iframes (which can also
    // reach qt.webChannelTransport) are dropped, so prompts are never attributed to the wrong site.
    if (env.value(QStringLiteral("n")).toString() != m_c->nonce()) {
        if (id > 0) emit resolved(id, QStringLiteral("{\"error\":\"SecurityError\",\"message\":\"Not allowed in this frame\"}"));
        return;
    }
    m_c->bridgeRequest(id, kind, env.value(QStringLiteral("o")).toObject());
}

// ------------------------------------------------------------------ tab-share overlay (§3 "while sharing")
namespace {
class ShareOverlay : public QWidget {
public:
    ShareOverlay(QWidget* tab, const QString& host, std::function<void()> stop) : QWidget(tab) {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAttribute(Qt::WA_NoSystemBackground); setAttribute(Qt::WA_TranslucentBackground);
        const auto& pal = ThemeManager::current();
        m_bar = new QWidget(this); m_bar->setObjectName(QStringLiteral("ShareBar"));
        m_bar->setStyleSheet(QStringLiteral("#ShareBar{background:%1;border-radius:20px;border:1px solid %2;} QLabel{color:%3;font-size:13px;}"
                                            "QPushButton{border:none;border-radius:14px;padding:5px 14px;font-size:13px;color:%3;background:%4;}"
                                            "QPushButton#stop{background:%5;color:%6;}").arg(pal.card(), pal.border, pal.text, pal.surface, pal.accent, pal.onAccent));
        auto* l = new QHBoxLayout(m_bar); l->setContentsMargins(16, 6, 6, 6); l->setSpacing(8);
        l->addWidget(new QLabel(QStringLiteral("Sharing this tab with <b>%1</b>").arg(host.toHtmlEscaped()), m_bar));
        auto* stopB = new QPushButton(QStringLiteral("Stop sharing"), m_bar); stopB->setObjectName(QStringLiteral("stop"));
        auto* hide = new QPushButton(QStringLiteral("Hide"), m_bar);
        l->addWidget(stopB); l->addWidget(hide);
        m_bar->setFixedHeight(40);
        QObject::connect(stopB, &QPushButton::clicked, this, [stop]{ stop(); });
        QObject::connect(hide, &QPushButton::clicked, m_bar, &QWidget::hide);
        tab->installEventFilter(this);
        setGeometry(tab->rect()); show(); raise();
        setMask(QRegion(rect()).subtracted(QRegion(rect().adjusted(4, 4, -4, -4))) + QRegion(barRect()));
    }
protected:
    QRect barRect() const { m_bar->adjustSize(); const int w = m_bar->sizeHint().width(); return QRect((width() - w) / 2, 12, w, 40); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); QPen pen(QColor(ThemeManager::current().accent), 4); p.setPen(pen); p.drawRect(rect().adjusted(2, 2, -2, -2));
    }
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == parentWidget() && e->type() == QEvent::Resize) {
            setGeometry(parentWidget()->rect()); m_bar->setGeometry(barRect());
            setMask(QRegion(rect()).subtracted(QRegion(rect().adjusted(4, 4, -4, -4))) + (m_bar->isVisible() ? QRegion(barRect()) : QRegion()));
        }
        return QWidget::eventFilter(o, e);
    }
    void showEvent(QShowEvent*) override { m_bar->setGeometry(barRect()); }
private:
    QWidget* m_bar;
};
}

// ------------------------------------------------------------------ controller
QList<PermissionController*>& PermissionController::all() { static QList<PermissionController*> list; return list; }

PermissionController::PermissionController(QWidget* tab, QWebEngineView* view, QWebEnginePage* page)
    : QObject(tab), m_tab(tab), m_view(view), m_page(page) {
    m_store = Perm::Store::forProfile(page->profile());
    m_nonce = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
    m_tabToken = QUuid::createUuid().toString(QUuid::Id128);
    m_channel = new QWebChannel(this);
    m_bridge = new PermissionBridge(this);
    m_channel->registerObject(QStringLiteral("pacitumPermissions"), m_bridge);
    {   // per-page shim script: main frame only, carries this page's nonce
        static QString shimTemplate;
        if (shimTemplate.isEmpty()) {
            QFile f(QStringLiteral(":/permissions/shim.js")), wc(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
            if (f.open(QIODevice::ReadOnly)) shimTemplate = QString::fromUtf8(f.readAll());
            if (wc.open(QIODevice::ReadOnly)) shimTemplate.replace(QStringLiteral("/*__QWEBCHANNEL_JS__*/"), QString::fromUtf8(wc.readAll()));
        }
        QString src = shimTemplate; src.replace(QStringLiteral("__PACITUM_NONCE__"), m_nonce);
        QWebEngineScript sc;
        sc.setName(QStringLiteral("pacitum-permission-shim"));
        sc.setSourceCode(src);
        sc.setInjectionPoint(QWebEngineScript::DocumentCreation);
        sc.setWorldId(QWebEngineScript::MainWorld);
        sc.setRunsOnSubFrames(false);
        page->scripts().insert(sc);
    }
    connect(page, &QWebEnginePage::permissionRequested, this, &PermissionController::onPermission);
    connect(page, &QWebEnginePage::desktopMediaRequested, this, &PermissionController::onDesktopMedia);
    connect(page, &QWebEnginePage::urlChanged, this, [this](const QUrl& u){
        ensureChannel();
        const QString o = Perm::originOf(u);
        if (o != m_origin) { const QString from = m_origin; m_origin = o; onOriginChanged(from, o); }
    });
    connect(page, &QWebEnginePage::loadStarted, this, [this]{
        ensureChannel();
        // New document: per-page state ends; open prompts close (= dismissed).
        m_shownThisLoad.clear(); m_blocked.clear(); m_blockedPopups.clear(); m_osDenied.clear();
        for (const auto& p : std::as_const(m_queue)) answer(p, QStringLiteral("navigate"));
        m_queue.clear();
        if (m_bubble) m_bubble->dismiss();
        if (m_heldDownloads.size()) m_heldDownloads.clear();
        emit stateChanged();
    });
    m_expiry = new QTimer(this); m_expiry->setInterval(5000);
    connect(m_expiry, &QTimer::timeout, this, &PermissionController::expirePending);
    all().append(this);
    ensureChannel();
}

PermissionController::~PermissionController() {
    all().removeAll(this);
    for (const auto& p : std::as_const(m_queue)) if (p.type == Pending::QtPerm && p.perm.isValid()) p.perm.deny();
    if (m_bubble) m_bubble->close();
    // Tab closed: "Allow this time" grants end.
    if (m_page && m_page->profile())
        for (auto it = m_session.cbegin(); it != m_session.cend(); ++it) {
            QWebEnginePermission::PermissionType t;
            if (qtTypeFor(Kind(it.key()), &t) && m_store->decision(it.value(), Kind(it.key())) != Decision::Allow)
                m_page->profile()->queryPermission(QUrl(it.value()), t).reset();
        }
#ifdef PACITUM_HAVE_SERIALPORT
    qDeleteAll(m_ports);
#endif
}

QString PermissionController::origin() const { return m_page ? Perm::originOf(m_page->url()) : QString(); }

void PermissionController::ensureChannel() {
    // Internal pages (welcome) install their own channel and clear it when leaving; web pages get ours.
    if (!m_page) return;
    const QString scheme = m_page->url().scheme();
    if (!m_page->webChannel() && (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("file") || scheme.isEmpty()))
        m_page->setWebChannel(m_channel);
}

void PermissionController::noteUserInput() { m_lastInput = QDateTime::currentMSecsSinceEpoch(); }
bool PermissionController::recentGesture(int ms) const { return m_lastInput > 0 && QDateTime::currentMSecsSinceEpoch() - m_lastInput <= ms; }

// ------------------------------------------------------------------ Qt permissions
void PermissionController::onPermission(QWebEnginePermission p) {
    Kind k; bool both = false;
    if (!kindFromQt(p.permissionType(), &k, &both)) { p.deny(); return; }
    const QString o = Perm::originOf(p.origin());
    if (k == Kind::MouseLock) {   // §11: no bubble; allowed on a user gesture, Esc hint toast
        if (m_store->effective(o, k) == Decision::Block || !recentGesture(5000)) { p.deny(); return; }
        p.grant(); emit mouseLockHint(); return;
    }
    if (k == Kind::ScreenShare) {
        // The source picker (desktopMediaRequested) is the real consent: nothing is shared until the
        // user picks a screen / window / tab there, and Cancel cancels the capture.
        if (m_store->effective(o, k) == Decision::Block) { p.deny(); addBlocked(k); return; }
        p.grant(); m_desktopPerms << p; return;
    }
    m_store->touch(o);
    auto grantNow = [&](){
        p.grant();
        if (k == Kind::Camera || k == Kind::Microphone) {
            m_grantedMedia << p;
            if (k == Kind::Camera) setInUse(Kind::Camera, true);
            if (k == Kind::Microphone || both) setInUse(Kind::Microphone, true);
        }
        if (k == Kind::Location) setInUse(Kind::Location, true);
        // §9: the site is allowed, but the OS blocks the device → explain (still grant per spec).
        for (Kind dk : {Kind::Camera, Kind::Microphone, Kind::Location}) {
            const bool relevant = dk == k || (both && dk == Kind::Microphone);
            if (relevant && PermDevices::osDenied(dk) && !m_osDenied.contains(int(dk))) { m_osDenied.insert(int(dk)); showOsDenied(dk); }
        }
    };
    // Session grant ("Allow this time")
    const bool sessionOk = m_session.value(int(k)) == o && (!both || m_session.value(int(Kind::Microphone)) == o);
    Decision d = m_store->effective(o, k);
    if (both) {
        const Decision dm = m_store->effective(o, Kind::Microphone);
        if (d == Decision::Block || dm == Decision::Block) d = Decision::Block;
        else if (d == Decision::Allow && dm == Decision::Allow) d = Decision::Allow;
        else d = Decision::Ask;
    }
    if (d == Decision::Allow || sessionOk) { grantNow(); return; }
    if (d == Decision::Block) {
        p.deny();
        addBlocked(k); if (both) addBlocked(Kind::Microphone);
        return;
    }
    Pending pe; pe.type = Pending::QtPerm; pe.perm = p; pe.kind = k; pe.both = both; pe.origin = o;
    pe.at = QDateTime::currentMSecsSinceEpoch();
    // §4 quiet chip: 3+ rejections in 28 days, or requested without a user gesture.
    if (k == Kind::Notifications && m_store->quietNotifications() && (m_store->recentNotificationRejections() >= 3 || !recentGesture(5000))) pe.quiet = true;
    enqueue(pe);
}

void PermissionController::enqueue(const Pending& p) {
    m_queue.append(p);
    m_expiry->start();
    if (s_testAutoAnswer) {   // CI self-test: answer immediately as if clicked
        const QString a = s_testAnswer;
        QTimer::singleShot(0, this, [this, a]{ if (m_queue.isEmpty()) return; const Pending h = m_queue.first(); m_queue.removeFirst(); answer(h, a.isEmpty() ? QStringLiteral("deny") : a); emit stateChanged(); });
        emit stateChanged();
        return;
    }
    if (m_queue.size() == 1 && !p.quiet && !m_shownThisLoad.contains(int(p.kind)) && (!canPrompt || canPrompt())) showHead(false);
    emit stateChanged();
}

PermUi::Bubble* PermissionController::makeBubble(const QString& rich, const QString& plain) {
    QWidget* win = m_tab ? m_tab->window() : nullptr;
    auto* b = new PermUi::Bubble(win);
    const QString host = Perm::hostOf(origin());
    QIcon fav = favicon ? favicon() : QIcon();
    if (fav.isNull()) fav = PermUi::letterIcon(host, 20);
    b->setHeader(fav, rich, plain);
    return b;
}

static QString wantsTo(const QString& host) { return Perm::richHost(host) + QStringLiteral(" wants to"); }

void PermissionController::showHead(bool fromGesture) {
    if (m_queue.isEmpty() || !m_tab) return;
    if (m_bubble) { m_bubble->raise(); return; }
    if (m_popover) m_popover->close();
    const Pending p = m_queue.first();
    m_shownThisLoad.insert(int(p.kind));
    const QString host = Perm::hostOf(p.origin.isEmpty() ? origin() : p.origin);
    const QList<PermUi::Bubble::Action> three = {
        {QStringLiteral("always"), QStringLiteral("Allow on every visit")},
        {QStringLiteral("once"), QStringLiteral("Allow this time")},
        {QStringLiteral("deny"), QStringLiteral("Don't allow")}};
    PermUi::Bubble* b = nullptr;
    if (p.type == Pending::QtPerm || p.type == Pending::Midi) {
        const QString cap = p.both ? QStringLiteral("Use your camera and microphone") : Perm::capability(p.kind);
        b = makeBubble(wantsTo(host), host + QStringLiteral(" wants to ") + cap.toLower());
        b->addCapability(p.both ? QStringLiteral("camera-mic") : Perm::glyph(p.kind), cap,
                         p.type == Pending::Midi && p.opts.value(QStringLiteral("sysex")).toBool() ? QStringLiteral("Includes system-exclusive messages") : QString());
        if (p.quiet) b->addNote(QStringLiteral("Pacitum blocked this request automatically"));
        if ((p.kind == Kind::Camera || p.kind == Kind::Microphone) && (PermDevices::osDenied(p.kind) || (p.both && PermDevices::osDenied(Kind::Microphone))))
            b->addNote(QStringLiteral("%1 currently blocks this device for Pacitum.").arg(PermDevices::osName()), true);
        b->setActions(three, false, QStringLiteral("once"));   // default focus: least privilege (§1 decision 2)
    } else if (p.type == Pending::Storage) {
        b = makeBubble(wantsTo(host), host + QStringLiteral(" wants to store data on this device permanently"));
        b->addCapability(QStringLiteral("storage"), Perm::capability(Kind::Storage), QStringLiteral("Data won't be cleared when space is low"));
        b->setActions({{QStringLiteral("always"), QStringLiteral("Allow")}, {QStringLiteral("deny"), QStringLiteral("Block")}}, false, QStringLiteral("always"));
    } else if (p.type == Pending::Downloads) {
        QStringList names; for (const auto& h : std::as_const(m_heldDownloads)) names << (h.second.isEmpty() ? h.first.fileName() : h.second);
        const int n = m_heldDownloads.size() + 1;
        QString detail = QStringLiteral("%1 files: ").arg(n) + names.mid(0, 2).join(QStringLiteral(", ")) + (names.size() > 2 ? QStringLiteral("…") : QString());
        b = makeBubble(wantsTo(host), host + QStringLiteral(" wants to download multiple files"));
        b->addCapability(QStringLiteral("downloads-multiple"), Perm::capability(Kind::Downloads), detail);
        b->setActions({{QStringLiteral("always"), QStringLiteral("Allow")}, {QStringLiteral("deny"), QStringLiteral("Block")}}, false, QStringLiteral("deny"));
    } else if (p.type == Pending::Device) {
        b = makeBubble(wantsTo(host), host + QStringLiteral(" wants to ") + Perm::capability(p.kind).toLower());
        b->addCapability(Perm::glyph(p.kind), Perm::capability(p.kind));
        auto* list = new PermUi::DeviceList();
        b->addWidget(list);
        const Kind k = p.kind; const QJsonObject opts = p.opts;
        if (k == Kind::Bluetooth) {
            auto* sc = new BluetoothScanner(opts, list);
            connect(sc, &BluetoothScanner::devicesChanged, list, [list, sc]{ list->setDevices(sc->devices()); list->setSearching(true); });
            connect(sc, &BluetoothScanner::finished, list, [list, sc](const QString& err){ list->setDevices(sc->devices()); list->setSearching(false); if (!err.isEmpty() && sc->devices().isEmpty()) list->setToolTip(err); });
            sc->start();
        } else {
            auto refresh = [list, k, opts]{
                QList<QJsonObject> devs;
                for (const auto& d : PermDevices::enumerate(k)) if (PermDevices::matches(k, d, opts)) devs << d;
                list->setDevices(devs); list->setSearching(false);
            };
            refresh();
            auto* t = new QTimer(list); t->setInterval(2000); connect(t, &QTimer::timeout, list, refresh); t->start();   // hot-plug
        }
        b->setActions({{QStringLiteral("cancel"), QStringLiteral("Cancel")}, {QStringLiteral("connect"), QStringLiteral("Connect"), true, false}}, true, QStringLiteral("cancel"));
        connect(list, &PermUi::DeviceList::selectionChanged, b, [b](bool any){ b->setActionEnabled(QStringLiteral("connect"), any); });
        connect(list, &PermUi::DeviceList::activated, b, [b]{ emit b->action(QStringLiteral("connect")); });
        b->setProperty("_list", QVariant::fromValue<QObject*>(list));
    }
    if (!b) return;
    m_bubble = b;
    connect(b, &PermUi::Bubble::action, this, [this, b](const QString& id){
        if (m_queue.isEmpty()) { b->dismiss(); return; }
        Pending h = m_queue.first();
        if (h.type == Pending::Device && id == QStringLiteral("connect")) {
            auto* list = qobject_cast<PermUi::DeviceList*>(b->property("_list").value<QObject*>());
            const QJsonObject dev = list ? list->selected() : QJsonObject();
            if (dev.isEmpty()) return;
            h.opts.insert(QStringLiteral("_selected"), dev);
        }
        m_queue.removeFirst();
        answer(h, id);
        b->dismiss();
        finishBubble();
    });
    connect(b, &PermUi::Bubble::closed, this, [this](bool){ emit stateChanged(); });
    connect(b, &QObject::destroyed, this, [this]{ emit stateChanged(); });
    const QPoint at = anchor ? anchor() : (m_tab ? m_tab->mapToGlobal(QPoint(120, 6)) : QPoint());
    b->popup(at, fromGesture);
    emit stateChanged();
}

void PermissionController::finishBubble() {
    emit stateChanged();
    // Next queued request (§1 throttling: one bubble at a time, FIFO)
    if (!m_queue.isEmpty() && !m_queue.first().quiet && !m_shownThisLoad.contains(int(m_queue.first().kind)) && (!canPrompt || canPrompt()))
        QTimer::singleShot(Motion::enabled() ? Motion::Fast + 20 : 0, this, [this]{ if (!m_bubble) showHead(false); });
}

void PermissionController::answer(const Pending& p, const QString& choice) {
    const bool navigate = choice == QLatin1String("navigate"), timeout = choice == QLatin1String("timeout");
    const QString o = p.origin;
    auto storeBoth = [&](Decision d){ m_store->setDecision(o, p.kind, d); if (p.both) m_store->setDecision(o, Kind::Microphone, d); };
    switch (p.type) {
    case Pending::QtPerm: {
        if (!p.perm.isValid()) return;
        if (choice == QLatin1String("always") || choice == QLatin1String("once")) {
            if (choice == QLatin1String("always")) storeBoth(Decision::Allow);
            else { m_session.insert(int(p.kind), o); if (p.both) m_session.insert(int(Kind::Microphone), o); }
            onPermission(p.perm);   // re-runs the grant path (in-use state, OS check)
        } else if (choice == QLatin1String("deny")) {
            storeBoth(Decision::Block);
            p.perm.deny();
            addBlocked(p.kind);
            if (p.kind == Kind::Notifications) m_store->noteNotificationRejected();
        } else {   // timeout / navigation: dismissed, not stored
            p.perm.deny();
            if (p.kind == Kind::Notifications && timeout) m_store->noteNotificationRejected();
        }
        break;
    }
    case Pending::Midi:
        if (choice == QLatin1String("always") || choice == QLatin1String("once")) {
            if (choice == QLatin1String("always")) m_store->setDecision(o, Kind::Midi, Decision::Allow); else m_session.insert(int(Kind::Midi), o);
            reply(p.reqId, true);
        } else {
            if (choice == QLatin1String("deny")) { m_store->setDecision(o, Kind::Midi, Decision::Block); addBlocked(Kind::Midi); }
            replyError(p.reqId, QStringLiteral("NotAllowedError"), QStringLiteral("Permission denied"));
        }
        break;
    case Pending::Storage:
        if (choice == QLatin1String("always")) { m_store->setDecision(o, Kind::Storage, Decision::Allow); reply(p.reqId, true); }
        else { if (choice == QLatin1String("deny")) m_store->setDecision(o, Kind::Storage, Decision::Block); reply(p.reqId, false); }
        break;
    case Pending::Downloads:
        if (choice == QLatin1String("always")) { m_store->setDecision(o, Kind::Downloads, Decision::Allow); releaseDownloads(true); }
        else { if (choice == QLatin1String("deny")) { m_store->setDecision(o, Kind::Downloads, Decision::Block); addBlocked(Kind::Downloads); } releaseDownloads(false); }
        break;
    case Pending::Device: {
        const QJsonObject dev = p.opts.value(QStringLiteral("_selected")).toObject();
        if (choice == QLatin1String("connect") && !dev.isEmpty() && o == origin()) {
            m_store->grantDevice(o, p.kind, dev);   // scoped: this origin, this profile, this kind
            QJsonObject out = dev; out.remove(QStringLiteral("meta")); out.remove(QStringLiteral("port"));
            reply(p.reqId, out);
        } else replyError(p.reqId, QStringLiteral("NotFoundError"), QStringLiteral("No device selected."));
        break;
    }
    case Pending::Desktop: break;
    }
    Q_UNUSED(navigate);
    emit stateChanged();
}

void PermissionController::expirePending() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (now - m_queue[i].at < kPendingMs) continue;
        if (i == 0 && m_bubble && m_bubble->isVisible()) continue;   // never time out a visible bubble... (§1: never auto-allow/deny visibly)
        const Pending p = m_queue.takeAt(i);
        answer(p, QStringLiteral("timeout"));
        changed = true;
    }
    if (m_queue.isEmpty()) m_expiry->stop();
    if (changed) emit stateChanged();
}

void PermissionController::setInUse(Kind k, bool on) {
    bool* f = k == Kind::Camera ? &m_camera : k == Kind::Microphone ? &m_mic : k == Kind::Location ? &m_location : k == Kind::ScreenShare ? &m_screen : nullptr;
    if (!f || *f == on) return;
    const bool wasCapturing = capturing();
    *f = on;
    if (!wasCapturing && capturing()) m_captureSince = QDateTime::currentDateTime();
    emit stateChanged();
}

void PermissionController::addBlocked(Kind k) { m_blocked.insert(int(k)); emit stateChanged(); }

void PermissionController::onOriginChanged(const QString& from, const QString& to) {
    Q_UNUSED(to);
    // Cross-origin navigation: "Allow this time" ends; capture stops being ours to show.
    if (!from.isEmpty() && m_page && m_page->profile())
        for (auto it = m_session.cbegin(); it != m_session.cend(); ++it) {
            QWebEnginePermission::PermissionType t;
            if (it.value() == from && qtTypeFor(Kind(it.key()), &t) && m_store->decision(from, Kind(it.key())) != Decision::Allow)
                m_page->profile()->queryPermission(QUrl(from), t).reset();
        }
    m_session.clear();
    m_grantedMedia.clear();
    m_camera = m_mic = m_location = m_screen = false;
    for (const QString& t : std::as_const(m_activeTabShares))
        for (auto* c : all()) if (c->m_tabToken == t) c->setInUse(Kind::ScreenShare, false);
    m_activeTabShares.clear();
    if (m_popover) m_popover->close();
    emit stateChanged();
}

// ------------------------------------------------------------------ desktop capture (§3)
void PermissionController::onDesktopMedia(const QWebEngineDesktopMediaRequest& req) {
    const QString o = origin();
    if (m_store->effective(o, Kind::ScreenShare) == Decision::Block || !m_tab) { req.cancel(); addBlocked(Kind::ScreenShare); return; }
    if (s_testAutoAnswer) { req.cancel(); return; }
    if (m_picker) m_picker->close();
    QList<PermUi::ScreenSharePicker::TabSource> tabs = shareableTabs ? shareableTabs() : QList<PermUi::ScreenSharePicker::TabSource>{};
    QIcon fav = favicon ? favicon() : QIcon(); if (fav.isNull()) fav = PermUi::letterIcon(Perm::hostOf(o), 28);
    auto* picker = new PermUi::ScreenSharePicker(m_tab, Perm::hostOf(o), fav, req.screensModel(), req.windowsModel(), tabs);
    m_picker = picker;
    m_store->touch(o);
    connect(picker, &PermUi::ScreenSharePicker::chosen, this, [this, req, o](int choice, int row, const QString& token){
        using C = PermUi::ScreenSharePicker::Choice;
        if (o != origin()) { req.cancel(); return; }
        if (choice == int(C::Screen) && req.screensModel()) { req.selectScreen(req.screensModel()->index(row)); setInUse(Kind::ScreenShare, true); }
        else if (choice == int(C::Window) && req.windowsModel()) { req.selectWindow(req.windowsModel()->index(row)); setInUse(Kind::ScreenShare, true); }
        else if (choice == int(C::Tab) && !token.isEmpty()) {
            // Qt can't capture a tab: cancel Qt's request; the shim then asks for this token and
            // receives the tab's frames over the bridge (canvas.captureStream()).
            m_pendingTabShare = token; m_pendingTabShareAt = QDateTime::currentMSecsSinceEpoch();
            req.cancel();
        } else req.cancel();
    });
    picker->open();
}

QPixmap PermissionController::grabFrame() const {
    if (!m_view) return {};
    QPixmap pm = m_view->grab();
    if (pm.width() > 1280) pm = pm.scaledToWidth(1280, Qt::SmoothTransformation);
    return pm;
}

// ------------------------------------------------------------------ bridge requests
void PermissionController::reply(int id, const QJsonValue& value) {
    if (id <= 0) return;
    emit m_bridge->resolved(id, QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("value"), value}}).toJson(QJsonDocument::Compact)));
}
void PermissionController::replyError(int id, const QString& name, const QString& message) {
    if (id <= 0) return;
    emit m_bridge->resolved(id, QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("error"), name}, {QStringLiteral("message"), message}}).toJson(QJsonDocument::Compact)));
}

void PermissionController::bridgeRequest(int id, const QString& kindStr, const QJsonObject& opts) {
    const QString o = origin();   // derived by the browser, never taken from the page
    if (o.isEmpty()) { replyError(id, QStringLiteral("SecurityError"), QStringLiteral("Not available on this page")); return; }
    Kind k = Kind::Usb;
    const bool deviceKind = Perm::fromKey(kindStr, &k) && Perm::isDevice(k);
    if (deviceKind) {
        if (m_store->effective(o, k) == Decision::Block) { addBlocked(k); replyError(id, QStringLiteral("NotFoundError"), QStringLiteral("No device selected.")); return; }
        if (!recentGesture(5000) && !s_testAutoAnswer) { replyError(id, QStringLiteral("SecurityError"), QStringLiteral("Must be handling a user gesture to show a permission request.")); return; }
        Pending p; p.type = Pending::Device; p.kind = k; p.reqId = id; p.opts = opts; p.origin = o; p.at = QDateTime::currentMSecsSinceEpoch();
        m_store->touch(o);
        m_shownThisLoad.remove(int(k));   // choosers always open on request (they're gesture-gated)
        enqueue(p);
        return;
    }
    if (kindStr == QLatin1String("devices") || kindStr == QLatin1String("forget")) {
        Kind dk; if (!Perm::fromKey(opts.value(QStringLiteral("kind")).toString(), &dk) || !Perm::isDevice(dk)) { replyError(id, QStringLiteral("TypeError"), QStringLiteral("bad kind")); return; }
        if (kindStr == QLatin1String("forget")) { m_store->revokeDevice(o, dk, opts.value(QStringLiteral("id")).toString()); reply(id, true); return; }
        QJsonArray out;
        if (m_store->effective(o, dk) != Decision::Block)
            for (const auto& v : m_store->devices(o, dk)) { QJsonObject d = v.toObject(); d.remove(QStringLiteral("meta")); d.remove(QStringLiteral("port")); out.append(d); }
        reply(id, out); return;
    }
    if (kindStr == QLatin1String("midi")) {
        const Decision d = m_store->effective(o, Kind::Midi);
        if (d == Decision::Allow || m_session.value(int(Kind::Midi)) == o) { reply(id, true); return; }
        if (d == Decision::Block) { addBlocked(Kind::Midi); replyError(id, QStringLiteral("NotAllowedError"), QStringLiteral("Permission denied")); return; }
        Pending p; p.type = Pending::Midi; p.kind = Kind::Midi; p.reqId = id; p.opts = opts; p.origin = o; p.at = QDateTime::currentMSecsSinceEpoch();
        m_store->touch(o); enqueue(p); return;
    }
    if (kindStr == QLatin1String("storage")) {
        const Decision d = m_store->effective(o, Kind::Storage);
        if (d != Decision::Ask) { reply(id, d == Decision::Allow); return; }
        Pending p; p.type = Pending::Storage; p.kind = Kind::Storage; p.reqId = id; p.origin = o; p.at = QDateTime::currentMSecsSinceEpoch();
        m_store->touch(o); enqueue(p); return;
    }
    if (kindStr == QLatin1String("storage-query")) { reply(id, m_store->effective(o, Kind::Storage) == Decision::Allow); return; }
    if (kindStr == QLatin1String("bluetooth-available")) { reply(id, PermDevices::bluetoothAvailable()); return; }
    if (kindStr == QLatin1String("capture-state")) {
        setInUse(Kind::Camera, opts.value(QStringLiteral("video")).toInt() > 0);
        setInUse(Kind::Microphone, opts.value(QStringLiteral("audio")).toInt() > 0);
        if (opts.value(QStringLiteral("display")).toInt() == 0 && m_activeTabShares.isEmpty()) setInUse(Kind::ScreenShare, false);
        else if (opts.value(QStringLiteral("display")).toInt() > 0) setInUse(Kind::ScreenShare, true);
        return;
    }
    if (kindStr == QLatin1String("tab-share-take")) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QString tok;
        if (!m_pendingTabShare.isEmpty() && now - m_pendingTabShareAt < 15000) tok = m_pendingTabShare;
        m_pendingTabShare.clear();
        if (!tok.isEmpty()) {
            m_activeTabShares.insert(tok);
            setInUse(Kind::ScreenShare, true);
            for (auto* c : all()) if (c->m_tabToken == tok && c->m_tab) {
                c->setInUse(Kind::ScreenShare, true);
                QPointer<PermissionController> self(this), src(c);
                auto* ov = new ShareOverlay(c->m_tab, Perm::hostOf(o), [self, tok]{ if (self) { emit self->m_bridge->stopRequested(QStringLiteral("tabshare"), tok); self->bridgeRequest(0, QStringLiteral("tab-share-stop"), QJsonObject{{QStringLiteral("token"), tok}}); } });
                ov->setObjectName(QStringLiteral("share-overlay-") + tok);
            }
        }
        reply(id, tok); return;
    }
    if (kindStr == QLatin1String("tab-frame")) {
        const QString tok = opts.value(QStringLiteral("token")).toString();
        if (!m_activeTabShares.contains(tok)) { replyError(id, QStringLiteral("NotAllowedError"), QStringLiteral("Not shared")); return; }
        for (auto* c : all()) if (c->m_tabToken == tok) {
            const QPixmap pm = c->grabFrame();
            QByteArray jpg; QBuffer buf(&jpg); buf.open(QIODevice::WriteOnly); pm.save(&buf, "JPEG", 70);
            reply(id, QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpg.toBase64()));
            return;
        }
        replyError(id, QStringLiteral("NotFoundError"), QStringLiteral("Tab closed")); return;
    }
    if (kindStr == QLatin1String("tab-share-stop")) {
        const QString tok = opts.value(QStringLiteral("token")).toString();
        if (m_activeTabShares.remove(tok)) {
            for (auto* c : all()) if (c->m_tabToken == tok) {
                c->setInUse(Kind::ScreenShare, false);
                if (c->m_tab) if (auto* ov = c->m_tab->findChild<QWidget*>(QStringLiteral("share-overlay-") + tok)) ov->deleteLater();
            }
            if (m_activeTabShares.isEmpty()) setInUse(Kind::ScreenShare, false);
        }
        reply(id, true); return;
    }
#ifdef PACITUM_HAVE_SERIALPORT
    if (kindStr == QLatin1String("serial-open")) { serialOpen(id, opts); return; }
    if (kindStr == QLatin1String("serial-write") || kindStr == QLatin1String("serial-close") || kindStr == QLatin1String("serial-signals")) {
        QSerialPort* sp = m_ports.value(opts.value(QStringLiteral("token")).toString());
        if (!sp) { replyError(id, QStringLiteral("InvalidStateError"), QStringLiteral("The port is closed.")); return; }
        if (kindStr == QLatin1String("serial-write")) {
            const QByteArray data = QByteArray::fromBase64(opts.value(QStringLiteral("data")).toString().toLatin1());
            if (sp->write(data) < 0) replyError(id, QStringLiteral("NetworkError"), sp->errorString()); else reply(id, true);
        } else if (kindStr == QLatin1String("serial-signals")) {
            const QJsonObject s = opts.value(QStringLiteral("signals")).toObject();
            if (s.contains(QStringLiteral("dataTerminalReady"))) sp->setDataTerminalReady(s.value(QStringLiteral("dataTerminalReady")).toBool());
            if (s.contains(QStringLiteral("requestToSend"))) sp->setRequestToSend(s.value(QStringLiteral("requestToSend")).toBool());
            if (s.contains(QStringLiteral("break"))) sp->setBreakEnabled(s.value(QStringLiteral("break")).toBool());
            reply(id, true);
        } else {
            m_ports.remove(opts.value(QStringLiteral("token")).toString()); sp->close(); sp->deleteLater(); reply(id, true);
        }
        return;
    }
#endif
    replyError(id, QStringLiteral("NotSupportedError"), QStringLiteral("Not supported by Pacitum"));
}

#ifdef PACITUM_HAVE_SERIALPORT
void PermissionController::serialOpen(int id, const QJsonObject& o) {
    const QString org = origin();
    const QString devId = o.value(QStringLiteral("id")).toString();
    if (!m_store->hasDevice(org, Kind::Serial, devId) || m_store->effective(org, Kind::Serial) == Decision::Block) {
        replyError(id, QStringLiteral("SecurityError"), QStringLiteral("This port wasn't shared with the site.")); return;
    }
    QString portName;
    for (const auto& d : PermDevices::enumerate(Kind::Serial)) if (d.value(QStringLiteral("id")).toString() == devId) portName = d.value(QStringLiteral("port")).toString();
    if (portName.isEmpty()) { replyError(id, QStringLiteral("NotFoundError"), QStringLiteral("The device has been lost.")); return; }
    auto* sp = new QSerialPort(portName, this);
    sp->setBaudRate(o.value(QStringLiteral("baudRate")).toInt(9600));
    sp->setDataBits(QSerialPort::DataBits(o.value(QStringLiteral("dataBits")).toInt(8)));
    sp->setStopBits(o.value(QStringLiteral("stopBits")).toInt(1) == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop);
    const QString par = o.value(QStringLiteral("parity")).toString();
    sp->setParity(par == QLatin1String("even") ? QSerialPort::EvenParity : par == QLatin1String("odd") ? QSerialPort::OddParity : QSerialPort::NoParity);
    sp->setFlowControl(o.value(QStringLiteral("flowControl")).toString() == QLatin1String("hardware") ? QSerialPort::HardwareControl : QSerialPort::NoFlowControl);
    if (!sp->open(QIODevice::ReadWrite)) { replyError(id, QStringLiteral("NetworkError"), sp->errorString()); delete sp; return; }
    const QString tok = QUuid::createUuid().toString(QUuid::Id128);
    m_ports.insert(tok, sp);
    connect(sp, &QSerialPort::readyRead, this, [this, sp, tok]{ emit m_bridge->serialData(tok, QString::fromLatin1(sp->readAll().toBase64())); });
    connect(sp, &QSerialPort::errorOccurred, this, [this, sp, tok](QSerialPort::SerialPortError e){
        if (e == QSerialPort::NoError || e == QSerialPort::TimeoutError) return;
        emit m_bridge->serialData(tok, QString());
        if (m_ports.remove(tok)) sp->deleteLater();
    });
    reply(id, tok);
}
#else
void PermissionController::serialOpen(int id, const QJsonObject&) { replyError(id, QStringLiteral("NotSupportedError"), QStringLiteral("Serial support is not built in")); }
#endif

// ------------------------------------------------------------------ pop-ups & downloads (§11)
bool PermissionController::allowPopup(QWebEnginePage::WebWindowType type) {
    if (type == QWebEnginePage::WebBrowserBackgroundTab) return true;       // middle / ctrl-click
    const QString o = origin();
    if (o.isEmpty()) return true;                                            // internal pages
    if (m_store->effective(o, Kind::Popups) == Decision::Allow) return true;
    if (recentGesture(5000)) { m_lastInput = 0; return true; }               // one pop-up per user activation
    return false;
}

void PermissionController::popupBlocked(const QUrl& url) {
    if (url.isValid() && !url.isEmpty() && !m_blockedPopups.contains(url.toString())) m_blockedPopups << url.toString();
    addBlocked(Kind::Popups);
}

bool PermissionController::filterDownload(QWebEngineDownloadRequest* dl) {
    if (m_bypassDownloads > 0) { --m_bypassDownloads; return true; }
    const QString o = origin();
    if (o.isEmpty()) return true;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (!m_dlTimes.isEmpty() && now - m_dlTimes.first() > 5000) m_dlTimes.removeFirst();
    m_dlTimes << now;
    if (m_dlTimes.size() <= 1 && m_heldDownloads.isEmpty()) return true;
    if (recentGesture(1000)) return true;
    const Decision d = m_store->effective(o, Kind::Downloads);
    if (d == Decision::Allow) return true;
    if (d == Decision::Block) { addBlocked(Kind::Downloads); return false; }
    // Qt can't defer a download (unaccepted requests are deleted after the signal), so the extra
    // downloads are cancelled now and re-issued with QWebEnginePage::download() on Allow.
    m_heldDownloads << qMakePair(dl->url(), dl->suggestedFileName());
    bool queued = false; for (const auto& p : std::as_const(m_queue)) queued |= p.type == Pending::Downloads;
    if (!queued) {
        Pending p; p.type = Pending::Downloads; p.kind = Kind::Downloads; p.origin = o; p.at = now;
        enqueue(p);
    } else emit stateChanged();
    return false;
}

void PermissionController::releaseDownloads(bool allow) {
    const auto held = m_heldDownloads; m_heldDownloads.clear();
    if (!allow || !m_page) return;
    for (const auto& h : held) { ++m_bypassDownloads; m_page->download(h.first, h.second); }
}

// ------------------------------------------------------------------ chrome state
QList<OmniChip> PermissionController::chips() const {
    const auto& pal = ThemeManager::current();
    QList<OmniChip> out;
    if (!m_queue.isEmpty()) {
        const Pending& h = m_queue.first();
        OmniChip c;
        if (h.quiet) { c.id = QStringLiteral("quiet"); c.glyph = QStringLiteral("notification-blocked"); c.color = pal.textSecondary; c.text = QStringLiteral("Notifications blocked"); }
        else { c.id = QStringLiteral("request"); c.glyph = h.both ? QStringLiteral("camera-mic") : h.type == Pending::Downloads ? QStringLiteral("downloads-multiple") : Perm::glyph(h.kind); c.color = pal.accent; c.soft = true; }
        c.badge = m_queue.size() - 1;
        out << c;
    }
    if (m_camera || m_mic || m_location || m_screen) {
        OmniChip c; c.id = QStringLiteral("inuse"); c.color = m_osDenied.isEmpty() ? pal.accent : pal.warning;
        if (m_camera) c.glyphs << QStringLiteral("camera");
        if (m_mic) c.glyphs << QStringLiteral("microphone");
        if (m_screen) c.glyphs << QStringLiteral("screen-share");
        if (m_location) c.glyphs << QStringLiteral("location");
        c.glyph = c.glyphs.value(0); c.dot = m_camera || m_mic;
        out << c;
    }
    if (!m_blockedPopups.isEmpty()) out << OmniChip{QStringLiteral("popups"), QStringLiteral("popup-blocked"), pal.textSecondary};
    for (int k : m_blocked) {
        if (Kind(k) == Kind::Popups && !m_blockedPopups.isEmpty()) continue;
        out << OmniChip{QStringLiteral("blocked:") + Perm::key(Kind(k)), Perm::blockedGlyph(Kind(k)), pal.textSecondary};
        break;   // one blocked chip; the popover lists the rest via site controls
    }
    return out;
}

int PermissionController::tabUsage() const {
    if (m_camera || m_mic) return 3;
    if (m_screen) return 2;
    if (m_location) return 1;
    return 0;
}

QList<PermUi::SiteRow> PermissionController::siteRows() const {
    QList<PermUi::SiteRow> rows;
    const QString o = origin();
    QSet<int> kinds;
    for (int k = 0; k < int(Kind::Count); ++k) if (m_store->hasSiteValue(o, Kind(k))) kinds.insert(k);
    for (auto it = m_session.cbegin(); it != m_session.cend(); ++it) if (it.value() == o) kinds.insert(it.key());
    for (int k : m_blocked) kinds.insert(k);
    for (Kind dk : {Kind::Usb, Kind::Bluetooth, Kind::Serial, Kind::Hid}) if (!m_store->devices(o, dk).isEmpty()) kinds.insert(int(dk));
    if (m_camera) kinds.insert(int(Kind::Camera));
    if (m_mic) kinds.insert(int(Kind::Microphone));
    if (m_screen) kinds.insert(int(Kind::ScreenShare));
    for (Kind k : Perm::settingsKinds()) {
        if (!kinds.contains(int(k))) continue;
        PermUi::SiteRow r{k, QString(), 0, false, false};
        const Decision d = m_store->decision(o, k);
        if (k == Kind::ScreenShare) { r.control = 1; r.state = m_screen ? QStringLiteral("Sharing now") : QStringLiteral("Ask every time"); }
        else if (m_osDenied.contains(int(k))) { r.state = QStringLiteral("Blocked by %1").arg(PermDevices::osName()); r.warning = true; r.on = true; }
        else if (d == Decision::Allow) { r.state = QStringLiteral("Allowed on every visit"); r.on = true; }
        else if (m_session.value(int(k)) == o) { r.state = QStringLiteral("Allowed this time · until you close the tab"); r.on = true; }
        else if (d == Decision::Block || m_blocked.contains(int(k))) {
            r.state = k == Kind::Popups && !m_blockedPopups.isEmpty() ? QStringLiteral("Blocked · %1 blocked on this page").arg(m_blockedPopups.size()) : QStringLiteral("Blocked");
        } else r.state = QStringLiteral("Ask every time");
        if (Perm::isDevice(k)) {
            const int n = m_store->devices(o, k).size();
            if (n && d != Decision::Block) { r.state = QStringLiteral("%1 device%2 allowed").arg(n).arg(n == 1 ? "" : "s"); r.on = true; }
        }
        rows << r;
    }
    return rows;
}

// ------------------------------------------------------------------ chrome actions
void PermissionController::chipClicked(const QString& id, bool fromGesture) {
    if (id == QLatin1String("request") || id == QLatin1String("quiet")) {
        if (m_bubble) { m_bubble->dismiss(); return; }
        m_shownThisLoad.remove(m_queue.isEmpty() ? -1 : int(m_queue.first().kind));
        showHead(fromGesture);
    } else if (id == QLatin1String("inuse")) showInUsePopover();
    else if (id == QLatin1String("popups")) showPopupsBubble();
    else if (id.startsWith(QLatin1String("blocked:"))) { Kind k; if (Perm::fromKey(id.mid(8), &k)) showBlockedPopover(k); }
}

void PermissionController::focusPending() {
    if (m_bubble) { m_bubble->activateWindow(); m_bubble->setFocus(); if (m_bubble->focusProxy()) m_bubble->focusProxy()->setFocus(); return; }
    if (!m_queue.isEmpty()) { m_shownThisLoad.remove(int(m_queue.first().kind)); showHead(true); }
}

void PermissionController::collapse() { if (m_bubble) m_bubble->dismiss(); if (m_popover) m_popover->close(); }
void PermissionController::tabActivated() {
    if (!m_queue.isEmpty() && !m_bubble && !m_queue.first().quiet && !m_shownThisLoad.contains(int(m_queue.first().kind))) showHead(false);
}

void PermissionController::stopCapture() {
    // "Stop using camera & mic on this tab": revoke for this page and stop the tracks.
    for (const auto& p : std::as_const(m_grantedMedia)) if (p.isValid()) p.deny();
    m_grantedMedia.clear();
    m_session.remove(int(Kind::Camera)); m_session.remove(int(Kind::Microphone));
    emit m_bridge->stopRequested(QStringLiteral("capture"), QString());
    setInUse(Kind::Camera, false); setInUse(Kind::Microphone, false);
}

void PermissionController::showInUsePopover() {
    if (m_popover) { m_popover->close(); return; }
    const QString host = Perm::hostOf(origin());
    QStringList what; if (m_camera) what << QStringLiteral("camera"); if (m_mic) what << QStringLiteral("microphone");
    if (m_screen) what << QStringLiteral("screen"); if (m_location) what << QStringLiteral("location");
    const QString title = QStringLiteral("Using your ") + (what.size() > 1 ? what.mid(0, what.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + what.last() : what.value(0));
    auto* b = makeBubble(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), title);
    b->setWidthHint(320);
    b->addNote(host.toHtmlEscaped() + (m_captureSince.isValid() && capturing() ? QStringLiteral(" · since ") + m_captureSince.toString(QStringLiteral("HH:mm")) : QString()));
    const auto& pal = ThemeManager::current();
    for (const QString& g : what) {
        Kind k = g == QLatin1String("camera") ? Kind::Camera : g == QLatin1String("microphone") ? Kind::Microphone : g == QLatin1String("screen") ? Kind::ScreenShare : Kind::Location;
        auto* row = new QWidget(); auto* l = new QHBoxLayout(row); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(10);
        auto* ic = new QLabel(row); ic->setPixmap(PermUi::glyphPixmap(Perm::glyph(k), pal.textSecondary, 18, row->devicePixelRatioF()));
        auto* nm = new QLabel(Perm::title(k), row); nm->setStyleSheet(QStringLiteral("color:%1;font-size:13px;").arg(pal.text));
        const bool os = m_osDenied.contains(int(k));
        auto* tag = new QLabel(os ? QStringLiteral("Blocked by %1").arg(PermDevices::osName()) : QStringLiteral("Allowed"), row);
        QColor sc(os ? pal.warning : pal.secure);
        tag->setStyleSheet(QStringLiteral("color:%1;background:rgba(%2,%3,%4,0.15);border-radius:12px;padding:3px 10px;font-size:12px;").arg(sc.name()).arg(sc.red()).arg(sc.green()).arg(sc.blue()));
        l->addWidget(ic); l->addWidget(nm, 1); l->addWidget(tag);
        b->addWidget(row);
    }
    QList<PermUi::Bubble::Action> acts;
    if (capturing()) acts << PermUi::Bubble::Action{QStringLiteral("stop"), QStringLiteral("Stop using camera & mic on this tab")};
    if (m_screen && !capturing()) acts << PermUi::Bubble::Action{QStringLiteral("stopshare"), QStringLiteral("Stop sharing")};
    acts << PermUi::Bubble::Action{QStringLiteral("controls"), QStringLiteral("Site controls…")};
    b->setActions(acts);
    connect(b, &PermUi::Bubble::action, this, [this, b](const QString& id){
        b->dismiss();
        if (id == QLatin1String("stop")) stopCapture();
        else if (id == QLatin1String("stopshare")) {
            emit m_bridge->stopRequested(QStringLiteral("display"), QString());
            for (const QString& t : QSet<QString>(m_activeTabShares)) { emit m_bridge->stopRequested(QStringLiteral("tabshare"), t); bridgeRequest(0, QStringLiteral("tab-share-stop"), QJsonObject{{QStringLiteral("token"), t}}); }
            setInUse(Kind::ScreenShare, false);
        }
        else if (id == QLatin1String("controls")) showSiteControls(anchor ? anchor() : QPoint());
    });
    m_popover = b;
    b->popup(anchor ? anchor() : QPoint(), true);
}

void PermissionController::showBlockedPopover(Kind k) {
    if (m_popover) { m_popover->close(); return; }
    const QString o = origin(), host = Perm::hostOf(o);
    const auto& pal = ThemeManager::current();
    auto* b = makeBubble(QStringLiteral("<b>%1 blocked</b>").arg(Perm::title(k).toHtmlEscaped()), Perm::title(k) + QStringLiteral(" blocked"));
    b->setWidthHint(330);
    QString body;
    switch (k) {
    case Kind::Location: body = QStringLiteral("You blocked %1 from knowing your location."); break;
    case Kind::Camera: body = QStringLiteral("You blocked %1 from using your camera."); break;
    case Kind::Microphone: body = QStringLiteral("You blocked %1 from using your microphone."); break;
    case Kind::Notifications: body = QStringLiteral("You blocked %1 from showing notifications."); break;
    case Kind::Downloads: body = QStringLiteral("%1 tried to download multiple files automatically."); break;
    default: body = QStringLiteral("%1 isn't allowed to ") + Perm::capability(k).left(1).toLower() + Perm::capability(k).mid(1) + QLatin1Char('.'); break;
    }
    b->addCapability(Perm::blockedGlyph(k), body.arg(host), QString(), false);
    auto* link = new QLabel(QStringLiteral("<a href=\"#\" style=\"color:%1;text-decoration:none\">Manage %2 settings</a>").arg(pal.accent, Perm::title(k).toLower()));
    connect(link, &QLabel::linkActivated, this, [this, b, o]{ b->dismiss(); emit siteSettingsRequested(o); });
    b->addWidget(link);
    b->setActions({{QStringLiteral("allow"), QStringLiteral("Allow on every visit")}, {QStringLiteral("done"), QStringLiteral("Done"), true}}, true, QStringLiteral("done"));
    connect(b, &PermUi::Bubble::action, this, [this, b, k, o](const QString& id){
        if (id == QLatin1String("allow")) {
            m_store->setDecision(o, k, Decision::Allow);
            if (k == Kind::Microphone || k == Kind::Camera) m_store->setDecision(o, k, Decision::Allow);
            b->setActionText(QStringLiteral("done"), QStringLiteral("Reload to apply"));   // §6
            b->setActionEnabled(QStringLiteral("allow"), false);
            b->setProperty("_reload", true);
            m_blocked.remove(int(k)); emit stateChanged();
            return;
        }
        if (b->property("_reload").toBool() && m_page) m_page->triggerAction(QWebEnginePage::Reload);
        b->dismiss();
    });
    m_popover = b;
    b->popup(anchor ? anchor() : QPoint(), true);
}

void PermissionController::showPopupsBubble() {
    if (m_popover) { m_popover->close(); return; }
    const QString o = origin();
    const int n = m_blockedPopups.size();
    const QString q = n == 1 ? QStringLiteral("1 pop-up was blocked on this page") : QStringLiteral("%1 pop-ups were blocked on this page").arg(n);
    auto* b = makeBubble(QStringLiteral("<b>%1</b>").arg(q), q);
    const auto& pal = ThemeManager::current();
    for (const QString& u : m_blockedPopups.mid(0, 5)) {
        auto* l = new QLabel(QStringLiteral("<a href=\"%1\" style=\"color:%2;text-decoration:none\">%3</a>").arg(u.toHtmlEscaped(), pal.accent,
                             QFontMetrics(qApp->font()).elidedText(u, Qt::ElideMiddle, 290).toHtmlEscaped()));
        l->setToolTip(u);
        connect(l, &QLabel::linkActivated, this, [this, b](const QString& url){
            // the user asked for it: open through the normal new-tab path (this click is a gesture)
            emit openUrlRequested(url);
            b->dismiss();
        });
        b->addWidget(l);
    }
    b->setActions({{QStringLiteral("always"), QStringLiteral("Always allow")}, {QStringLiteral("done"), QStringLiteral("Done"), true}}, true, QStringLiteral("done"));
    connect(b, &PermUi::Bubble::action, this, [this, b, o](const QString& id){
        if (id == QLatin1String("always")) m_store->setDecision(o, Kind::Popups, Decision::Allow);
        b->dismiss();
    });
    m_popover = b;
    b->popup(anchor ? anchor() : QPoint(), true);
}

void PermissionController::showOsDenied(Kind k) {
    if (s_testAutoAnswer || !m_tab) return;
    const QString host = Perm::hostOf(origin());
    auto* b = makeBubble(wantsTo(host), host + QStringLiteral(" wants to ") + Perm::capability(k).toLower());
    b->addCapability(Perm::glyph(k), Perm::capability(k), QStringLiteral("Allowed for this site, but blocked by the system"), true);
    const QString dev = k == Kind::Camera ? QStringLiteral("camera") : k == Kind::Microphone ? QStringLiteral("microphone") : QStringLiteral("location");
    b->addNote(QStringLiteral("Pacitum doesn't have access to your %1 in %2 settings. Turn on <b>Let desktop apps access your %1</b>, then reload this page.").arg(dev, PermDevices::osName()), true);
    const QString url = PermDevices::osSettingsUrl(k);
    QList<PermUi::Bubble::Action> acts{{QStringLiteral("dismiss"), QStringLiteral("Dismiss")}};
    if (!url.isEmpty()) acts << PermUi::Bubble::Action{QStringLiteral("open"), QStringLiteral("Open settings"), true, true, QStringLiteral("external-link")};
    b->setActions(acts, true, QStringLiteral("open"));
    connect(b, &PermUi::Bubble::action, this, [b, url](const QString& id){ if (id == QLatin1String("open")) QDesktopServices::openUrl(QUrl(url)); b->dismiss(); });
    if (m_bubble) return;   // a prompt is up; the in-use chip shows the warning tint instead
    m_popover = b;
    b->popup(anchor ? anchor() : QPoint(), false);
}

void PermissionController::showSiteControls(const QPoint& at) {
    if (!m_tab) return;
    const QString o = origin();
    if (o.isEmpty()) return;
    QIcon fav = favicon ? favicon() : QIcon(); if (fav.isNull()) fav = PermUi::letterIcon(Perm::hostOf(o), 28);
    auto* sc = new PermUi::SiteControls(m_tab->window(), o, fav, o.startsWith(QLatin1String("https://")), siteRows(), -1);
    connect(sc, &PermUi::SiteControls::toggled, this, [this, o](Kind k, bool on){
        m_store->setDecision(o, k, on ? Decision::Allow : Decision::Block);
        if (!on) {
            m_session.remove(int(k));
            if (k == Kind::Camera || k == Kind::Microphone) stopCapture();
            if (Perm::isDevice(k)) m_store->revokeDevices(o, k);
        }
        emit stateChanged();
    });
    connect(sc, &PermUi::SiteControls::resetRequested, this, [this, o]{
        m_store->resetOrigin(o); m_session.clear(); m_blocked.clear(); stopCapture(); emit stateChanged();
    });
    connect(sc, &PermUi::SiteControls::siteSettingsRequested, this, [this, o]{ emit siteSettingsRequested(o); });
    sc->popup(at);
}

// ------------------------------------------------------------------ profile sync
void PermissionController::syncQtPermissions(QWebEngineProfile* profile) {
    if (!profile) return;
    auto* st = Perm::Store::forProfile(profile);
    for (const QString& o : st->origins()) {
        for (Kind k : {Kind::Notifications, Kind::Location, Kind::Clipboard, Kind::Fonts}) {
            QWebEnginePermission::PermissionType t; qtTypeFor(k, &t);
            const Decision d = st->decision(o, k);
            QWebEnginePermission p = profile->queryPermission(QUrl(o), t);
            if (!p.isValid()) continue;
            if (d == Decision::Allow && p.state() != QWebEnginePermission::State::Granted) p.grant();
            else if (d == Decision::Block && p.state() != QWebEnginePermission::State::Denied) p.deny();
        }
    }
}

void PermissionController::installProfile(QWebEngineProfile* profile) {
    if (!profile || profile->property("_pacitumPerms").toBool()) return;
    profile->setProperty("_pacitumPerms", true);
    // Pacitum's store is the source of truth; Qt keeps only an in-memory mirror for this session,
    // so "Allow this time" never leaks to disk and Settings can always change a decision.
    profile->setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::StoreInMemory);
    auto* st = Perm::Store::forProfile(profile);
    // Pre-1.7.5 "Pop-ups and redirects" switch becomes the pop-up default once.
    if (!st->hasExplicitDefault(Kind::Popups) && Pacitum::webPrefs().popups) st->setDefault(Kind::Popups, Decision::Allow);
    syncQtPermissions(profile);
    QObject::connect(st, &Perm::Store::changed, profile, [profile](const QString& o){
        if (!o.isEmpty()) {   // decisions changed in Settings / site controls: reset Qt's copy for Ask
            auto* s = Perm::Store::forProfile(profile);
            for (Kind k : {Kind::Notifications, Kind::Location, Kind::Clipboard, Kind::Fonts}) {
                QWebEnginePermission::PermissionType t; qtTypeFor(k, &t);
                if (s->decision(o, k) == Decision::Ask) { auto p = profile->queryPermission(QUrl(o), t); if (p.isValid() && p.state() != QWebEnginePermission::State::Ask) p.reset(); }
            }
        }
        syncQtPermissions(profile);
    });
}
