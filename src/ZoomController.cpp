#include "ZoomController.h"
#include <algorithm>
#include <cmath>

ZoomController::ZoomController(double initial, QObject* parent)
    : QObject(parent), m_zoom(std::clamp(initial, MIN_ZOOM, MAX_ZOOM)) {}

void ZoomController::zoomIn()  { setZoom(std::min(m_zoom + ZOOM_STEP, MAX_ZOOM)); }
void ZoomController::zoomOut() { setZoom(std::max(m_zoom - ZOOM_STEP, MIN_ZOOM)); }
void ZoomController::zoomReset(){ setZoom(DEFAULT_ZOOM); }

void ZoomController::setZoom(double factor) {
    factor = std::clamp(factor, MIN_ZOOM, MAX_ZOOM);
    if (std::abs(factor - m_zoom) > 0.01) {
        m_zoom = factor;
        emit zoomChanged(factor);
    }
}
