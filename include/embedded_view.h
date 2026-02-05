/*
 * embedded_view.h - QML item for displaying Wayland surfaces
 *
 * EmbeddedView is a QQuickItem that renders a Wayland client surface
 * and forwards input events back to the compositor.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024
 */
#ifndef EMBEDDED_VIEW_H
#define EMBEDDED_VIEW_H

#include <QQuickItem>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <QImage>
#include <QMutex>

class CompositorWrapper;
struct comp_view;

class EmbeddedView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int viewIndex READ viewIndex WRITE setViewIndex NOTIFY viewIndexChanged)
    Q_PROPERTY(bool hasView READ hasView NOTIFY hasViewChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    QML_ELEMENT

public:
    explicit EmbeddedView(QQuickItem* parent = nullptr);
    ~EmbeddedView() override;

    int viewIndex() const { return m_viewIndex; }
    void setViewIndex(int index);
    
    bool hasView() const { return m_hasView; }
    QString title() const { return m_title; }

    /* Set compositor reference (called from main) */
    static void setCompositor(CompositorWrapper* compositor);
    static CompositorWrapper* compositor() { return s_compositor; }

signals:
    void viewIndexChanged();
    void hasViewChanged();
    void titleChanged();

public slots:
    void updateFrame();
    void onViewsChanged();
    void onSizeChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    quint32 qtKeyToLinux(int qtKey) const;
    quint32 qtButtonToLinux(Qt::MouseButton button) const;
    void updateViewState();

    static CompositorWrapper* s_compositor;
    
    int m_viewIndex = -1;
    bool m_hasView = false;
    QString m_title;
    
    QImage m_frameBuffer;
    QMutex m_bufferMutex;
    bool m_needsUpdate = false;
};

#endif /* EMBEDDED_VIEW_H */
