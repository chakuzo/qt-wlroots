/*
 * compositor_wrapper.cpp - C++ wrapper implementation
 */
#include "compositor_wrapper.h"
#include "compositor_core.h"

#include <QDebug>
#include <QRect>

CompositorWrapper::CompositorWrapper(QObject* parent)
    : QObject(parent)
{
}

CompositorWrapper::~CompositorWrapper() {
    stop();
}

bool CompositorWrapper::initialize() {
    qDebug() << "Initializing compositor...";
    
    /* Create server */
    m_server = comp_server_create();
    if (!m_server) {
        emit error("Failed to create compositor server");
        return false;
    }
    
    /* Initialize backend */
    if (!comp_server_init_backend(m_server)) {
        emit error("Failed to initialize backend");
        comp_server_destroy(m_server);
        m_server = nullptr;
        return false;
    }
    
    /* Set callbacks */
    comp_server_set_frame_callback(m_server, &CompositorWrapper::frameCallback, this);
    comp_server_set_view_callback(m_server, &CompositorWrapper::viewCallback, this);
    
    qDebug() << "Compositor initialized";
    return true;
}

bool CompositorWrapper::start() {
    if (!m_server) {
        emit error("Server not initialized");
        return false;
    }
    
    qDebug() << "Starting compositor...";
    
    if (!comp_server_start(m_server)) {
        emit error("Failed to start compositor");
        return false;
    }
    
    m_socketName = QString::fromUtf8(comp_server_get_socket(m_server));
    emit socketNameChanged();
    
    qDebug() << "Compositor started on socket:" << m_socketName;
    
    /* Setup Qt event integration */
    int fd = comp_server_get_event_fd(m_server);
    if (fd >= 0) {
        m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, 
                this, &CompositorWrapper::onWaylandEvents);
        m_notifier->setEnabled(true);
    }
    
    /* Frame timer for consistent updates (60fps) */
    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &CompositorWrapper::onFrameTimer);
    m_frameTimer->start(16);  /* ~60fps */
    
    m_running = true;
    emit runningChanged();
    
    return true;
}

void CompositorWrapper::stop() {
    if (!m_running) return;
    
    qDebug() << "Stopping compositor...";
    
    m_running = false;
    
    if (m_frameTimer) {
        m_frameTimer->stop();
        delete m_frameTimer;
        m_frameTimer = nullptr;
    }
    
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
    
    if (m_server) {
        comp_server_destroy(m_server);
        m_server = nullptr;
    }
    
    m_views.clear();
    emit runningChanged();
    emit viewsChanged();
}

QString CompositorWrapper::socketName() const {
    return m_socketName;
}

bool CompositorWrapper::isRunning() const {
    return m_running;
}

int CompositorWrapper::viewCount() const {
    return m_views.size();
}

QString CompositorWrapper::viewTitle(int index) const {
    if (index < 0 || index >= m_views.size()) return QString();
    
    const char* title = comp_view_get_title(m_views[index]);
    return title ? QString::fromUtf8(title) : QString("(untitled)");
}

QRect CompositorWrapper::viewGeometry(int index) const {
    if (index < 0 || index >= m_views.size()) return QRect();
    
    int32_t x, y;
    uint32_t w, h;
    comp_view_get_geometry(m_views[index], &x, &y, &w, &h);
    return QRect(x, y, w, h);
}

void CompositorWrapper::focusView(int index) {
    if (index < 0 || index >= m_views.size()) return;
    comp_view_focus(m_views[index]);
}

void CompositorWrapper::closeView(int index) {
    if (index < 0 || index >= m_views.size()) return;
    comp_view_close(m_views[index]);
}

QImage CompositorWrapper::getViewFrame(int index) {
    if (index < 0 || index >= m_views.size()) return QImage();
    
    struct comp_view* view = m_views[index];
    if (!comp_view_is_mapped(view)) return QImage();
    
    uint32_t width, height;
    comp_view_get_surface_size(view, &width, &height);
    
    if (width == 0 || height == 0) return QImage();
    
    /* Create buffer for rendering */
    QImage frame(width, height, QImage::Format_ARGB32);
    frame.fill(Qt::black);
    
    /* Try to render view to buffer 
     * Note: This currently returns false because wlroots doesn't easily
     * support CPU readback. The actual rendering happens in the nested
     * window managed by wlroots. For true embedded rendering, we'd need
     * a headless backend with texture readback. */
    if (comp_view_render_to_buffer(view, frame.bits(), width, height, frame.bytesPerLine())) {
        return frame;
    }
    
    /* Return placeholder with view dimensions */
    return frame;
}

void CompositorWrapper::sendKey(quint32 key, bool pressed) {
    if (m_server) {
        comp_server_send_key(m_server, key, pressed);
    }
}

void CompositorWrapper::sendModifiers(quint32 depressed, quint32 latched,
                                       quint32 locked, quint32 group) {
    if (m_server) {
        comp_server_send_modifiers(m_server, depressed, latched, locked, group);
    }
}

void CompositorWrapper::sendPointerMotion(double x, double y) {
    if (m_server) {
        comp_server_send_pointer_motion(m_server, x, y);
    }
}

void CompositorWrapper::sendPointerButton(quint32 button, bool pressed) {
    if (m_server) {
        comp_server_send_pointer_button(m_server, button, pressed);
    }
}

void CompositorWrapper::sendPointerAxis(bool horizontal, double value) {
    if (m_server) {
        comp_server_send_pointer_axis(m_server, horizontal, value);
    }
}

void CompositorWrapper::onWaylandEvents() {
    if (m_server) {
        comp_server_dispatch_events(m_server);
    }
}

void CompositorWrapper::onFrameTimer() {
    if (m_server) {
        comp_server_dispatch_events(m_server);
        comp_server_flush_clients(m_server);
    }
}

void CompositorWrapper::frameCallback(void* userData, uint32_t width, 
                                       uint32_t height, void* buffer) {
    Q_UNUSED(width);
    Q_UNUSED(height);
    Q_UNUSED(buffer);
    
    auto* self = static_cast<CompositorWrapper*>(userData);
    emit self->frameReady();
}

void CompositorWrapper::viewCallback(void* userData, struct comp_view* view, bool added) {
    auto* self = static_cast<CompositorWrapper*>(userData);
    
    if (added) {
        if (!self->m_views.contains(view)) {
            int index = self->m_views.size();
            self->m_views.append(view);
            emit self->viewsChanged();
            emit self->viewAdded(index);
            qDebug() << "View added, count:" << self->m_views.size();
        }
    } else {
        int index = self->m_views.indexOf(view);
        if (index >= 0) {
            self->m_views.removeAt(index);
            emit self->viewsChanged();
            emit self->viewRemoved(index);
            qDebug() << "View removed, count:" << self->m_views.size();
        }
    }
}
