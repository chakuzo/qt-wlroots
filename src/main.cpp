/*
 * main.cpp - Application entry point with QML UI
 */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>

#include "compositor_wrapper.h"
#include "embedded_view.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {
    /* Check if we're running in a graphical environment */
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char* x11Display = std::getenv("DISPLAY");
    
    if (!waylandDisplay && !x11Display) {
        std::cerr << "Error: No WAYLAND_DISPLAY or DISPLAY environment variable set.\n";
        std::cerr << "This compositor must run in nested mode inside an existing compositor or X11.\n";
        return 1;
    }
    
    std::cout << "Starting wlroots-qt-compositor in nested mode\n";
    if (waylandDisplay) {
        std::cout << "  Parent compositor: Wayland (" << waylandDisplay << ")\n";
    } else {
        std::cout << "  Parent compositor: X11 (" << x11Display << ")\n";
    }
    
    /* Create Qt application */
    QGuiApplication app(argc, argv);
    app.setApplicationName("wlroots-qt-compositor");
    app.setApplicationVersion("1.0");
    
    /* Register QML types */
    qmlRegisterType<EmbeddedView>("WaylandCompositor", 1, 0, "EmbeddedView");
    
    /* Create compositor */
    CompositorWrapper compositor;
    
    /* Set compositor for EmbeddedView items */
    EmbeddedView::setCompositor(&compositor);
    
    /* Create QML engine */
    QQmlApplicationEngine engine;
    
    /* Expose compositor to QML */
    engine.rootContext()->setContextProperty("compositor", &compositor);
    
    /* Load QML */
    engine.load(QUrl("qrc:/qml/main.qml"));
    
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "Failed to load QML\n";
        return 1;
    }
    
    /* Initialize and start compositor after event loop begins */
    QTimer::singleShot(100, [&]() {
        if (!compositor.initialize()) {
            std::cerr << "Failed to initialize compositor\n";
            QGuiApplication::quit();
            return;
        }
        
        if (!compositor.start()) {
            std::cerr << "Failed to start compositor\n";
            QGuiApplication::quit();
            return;
        }
        
        std::cout << "\n";
        std::cout << "===========================================\n";
        std::cout << "  Compositor is running!\n";
        std::cout << "  Socket: " << compositor.socketName().toStdString() << "\n";
        std::cout << "===========================================\n";
        std::cout << "\n";
        std::cout << "To test, open a new terminal and run:\n";
        std::cout << "  WAYLAND_DISPLAY=" << compositor.socketName().toStdString() << " foot\n";
        std::cout << "  WAYLAND_DISPLAY=" << compositor.socketName().toStdString() << " alacritty\n";
        std::cout << "\n";
        std::cout << "First app goes to View 1, second to View 2.\n";
        std::cout << "Click a view to focus it, then type!\n";
        std::cout << "\n";
    });
    
    /* Run event loop */
    int result = app.exec();
    
    /* Cleanup */
    compositor.stop();
    
    return result;
}
