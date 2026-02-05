/*
 * compositor_wrapper.h - Qt/C++ wrapper for the wlroots compositor
 *
 * Provides a QObject-based interface to integrate the Wayland compositor
 * with Qt's event loop and signal/slot system.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024
 */
#ifndef COMPOSITOR_WRAPPER_H
#define COMPOSITOR_WRAPPER_H

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QList>
#include <QRect>
#include <QImage>
#include <memory>

/* Forward declare C types */
extern "C" {
    struct comp_server;
    struct comp_view;
}

class CompositorWrapper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString socketName READ socketName NOTIFY socketNameChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int viewCount READ viewCount NOTIFY viewsChanged)
    Q_PROPERTY(bool hardwareRendering READ isHardwareRendering NOTIFY hardwareRenderingChanged)

public:
    explicit CompositorWrapper(QObject* parent = nullptr);
    ~CompositorWrapper() override;

    /* Initialize compositor - optionally with hardware acceleration */
    bool initialize(bool useHardware = false);
    bool start();
    void stop();
    
    /* Check if hardware acceleration is available */
    static bool hardwareAvailable();

    /* Properties */
    QString socketName() const;
    bool isRunning() const;
    int viewCount() const;
    bool isHardwareRendering() const;

    /* View access */
    Q_INVOKABLE QString viewTitle(int index) const;
    Q_INVOKABLE QRect viewGeometry(int index) const;
    Q_INVOKABLE void focusView(int index);
    Q_INVOKABLE void closeView(int index);
    Q_INVOKABLE void resizeView(int index, int width, int height);
    
    /* Get rendered frame for a specific view */
    QImage getViewFrame(int index);

    /* Input forwarding from Qt */
    Q_INVOKABLE void sendKey(quint32 key, bool pressed);
    Q_INVOKABLE void sendModifiers(quint32 depressed, quint32 latched, 
                                   quint32 locked, quint32 group);
    Q_INVOKABLE void sendPointerMotion(double x, double y);
    Q_INVOKABLE void sendPointerButton(quint32 button, bool pressed);
    Q_INVOKABLE void sendPointerAxis(bool horizontal, double value);

signals:
    void socketNameChanged();
    void runningChanged();
    void viewsChanged();
    void viewAdded(int index);
    void viewRemoved(int index);
    void frameReady();
    void error(const QString& message);
    void hardwareRenderingChanged();

private slots:
    void onWaylandEvents();
    void onFrameTimer();

private:
    /* Static callbacks for C interface */
    static void frameCallback(void* userData, uint32_t width, uint32_t height, void* buffer);
    static void viewCallback(void* userData, struct comp_view* view, bool added);
    static void commitCallback(void* userData);

    /* Internal state */
    struct comp_server* m_server = nullptr;
    QSocketNotifier* m_notifier = nullptr;
    QTimer* m_frameTimer = nullptr;
    QList<struct comp_view*> m_views;
    bool m_running = false;
    QString m_socketName;
};

#endif /* COMPOSITOR_WRAPPER_H */
