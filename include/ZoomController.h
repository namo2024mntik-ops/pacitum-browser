#pragma once
#include <QObject>

class ZoomController : public QObject {
    Q_OBJECT
public:
    static constexpr double MIN_ZOOM = 0.25;
    static constexpr double MAX_ZOOM = 5.0;
    static constexpr double DEFAULT_ZOOM = 1.0;
    static constexpr double ZOOM_STEP = 0.1;

    explicit ZoomController(double initial = DEFAULT_ZOOM, QObject* parent = nullptr);

    void zoomIn();
    void zoomOut();
    void zoomReset();
    void setZoom(double factor);
    double zoom() const { return m_zoom; }
    int zoomPercent() const { return int(m_zoom * 100); }

signals:
    void zoomChanged(double factor);

private:
    double m_zoom;
};
