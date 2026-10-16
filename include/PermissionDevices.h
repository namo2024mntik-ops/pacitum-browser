#pragma once
// Native device enumeration for the WebUSB / WebHID / Web Serial / Web Bluetooth choosers and the
// OS-level privacy checks (permissions-spec §9). QtWebEngine 6.8 has no chooser API for these
// device APIs, so Pacitum lists devices itself and only ever hands a page the descriptor of the
// device the user picked.
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include "SitePermissions.h"

namespace PermDevices {
// {id, name, meta, vendorId, productId, serial, manufacturer, port} — id is stable per device.
QList<QJsonObject> enumerate(Perm::Kind k);
// WebUSB/WebHID/Web Serial filter matching (vendorId/productId/usbVendorId/usbProductId);
// fields Pacitum can't inspect (class codes, usage pages) don't exclude a device.
bool matches(Perm::Kind k, const QJsonObject& dev, const QJsonObject& options);
bool bluetoothAvailable();
// OS privacy switch for camera / microphone / location is off (Windows ConsentStore).
bool osDenied(Perm::Kind k);
QString osSettingsUrl(Perm::Kind k);
QString osName();
}

class BluetoothScanner : public QObject {
    Q_OBJECT
public:
    explicit BluetoothScanner(const QJsonObject& options, QObject* parent = nullptr);
    void start();
    QList<QJsonObject> devices() const { return m_devices; }
signals:
    void devicesChanged();
    void finished(const QString& error);
private:
    QJsonObject m_opts;
    QList<QJsonObject> m_devices;
    QObject* m_agent = nullptr;
};
