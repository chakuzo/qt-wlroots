/*
 * main.cpp - Application entry point with QML UI
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024
 */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>
#include <QCommandLineParser>

#include "compositor_wrapper.h"
#include "embedded_view.h"

#include <cstdlib>
#include <iostream>

void printUsage() {
    std::cout << "Usage: wlroots-qt-compositor [options]\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  --hardware, -hw    Use hardware-accelerated rendering (GLES2)\n";
    std::cout << "  --software, -sw    Use software rendering (Pixman) [default]\n";
    std::cout << "  --help, -h         Show this help\n";
    std::cout << "\n";
    std::cout << "Environment:\n";
    std::cout << "  WLROOTS_QT_HARDWARE=1   Enable hardware rendering\n";
}

int main(int argc, char* argv[]) {
    /* Check if we're running in a graphical environment */
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    const char* x11Display = std::getenv("DISPLAY");
    
    if (!waylandDisplay && !x11Display) {
        std::cerr << "Error: No WAYLAND_DISPLAY or DISPLAY environment variable set.\n";
        std::cerr << "This compositor must run in nested mode inside an existing compositor or X11.\n";
        return 1;
    }
    
    /* Parse command line arguments manually before QApplication */
    bool useHardware = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--hardware" || arg == "-hw") {
            useHardware = true;
        } else if (arg == "--software" || arg == "-sw") {
            useHardware = false;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }
    
    /* Check environment variable */
    const char* hwEnv = std::getenv("WLROOTS_QT_HARDWARE");
    if (hwEnv && (std::string(hwEnv) == "1" || std::string(hwEnv) == "true")) {
        useHardware = true;
    }
    
    std::cout << "Starting wlroots-qt-compositor in nested mode\n";
    if (waylandDisplay) {
        std::cout << "  Parent compositor: Wayland (" << waylandDisplay << ")\n";
    } else {
        std::cout << "  Parent compositor: X11 (" << x11Display << ")\n";
    }
    std::cout << "  Rendering: " << (useHardware ? "Hardware (GLES2)" : "Software (Pixman)") << "\n";
    std::cout << "  Hardware available: " << (CompositorWrapper::hardwareAvailable() ? "Yes" : "No") << "\n";
    
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
    QTimer::singleShot(100, [&, useHardware]() {
        if (!compositor.initialize(useHardware)) {
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
        std::cout << "  Renderer: " << (compositor.isHardwareRendering() ? "Hardware" : "Software") << "\n";
        std::cout << "===========================================\n";
        std::cout << "\n";
        std::cout << "To test, open a new terminal and run:\n";
        std::cout << "  WAYLAND_DISPLAY=" << compositor.socketName().toStdString() << " weston-terminal\n";
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
