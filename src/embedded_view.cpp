/*
 * embedded_view.cpp - QML Item implementation for Wayland surface display
 */
#include "embedded_view.h"
#include "compositor_wrapper.h"

#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
#include <QDebug>

#include <linux/input-event-codes.h>

CompositorWrapper* EmbeddedView::s_compositor = nullptr;

EmbeddedView::EmbeddedView(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, true);
    setFocus(true);
    
    /* Connect to compositor if available */
    if (s_compositor) {
        connect(s_compositor, &CompositorWrapper::viewsChanged,
                this, &EmbeddedView::onViewsChanged);
        connect(s_compositor, &CompositorWrapper::frameReady,
                this, &EmbeddedView::updateFrame);
    }
    
    /* Frame update timer - poll for new frames */
    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &EmbeddedView::updateFrame);
    m_frameTimer->start(16);  /* ~60fps */
}

EmbeddedView::~EmbeddedView() {
    if (m_frameTimer) {
        m_frameTimer->stop();
    }
}

void EmbeddedView::setCompositor(CompositorWrapper* compositor) {
    s_compositor = compositor;
}

void EmbeddedView::setViewIndex(int index) {
    if (m_viewIndex != index) {
        m_viewIndex = index;
        emit viewIndexChanged();
        updateViewState();
    }
}

void EmbeddedView::updateViewState() {
    if (!s_compositor) {
        if (m_hasView) {
            m_hasView = false;
            emit hasViewChanged();
        }
        return;
    }
    
    bool hasView = m_viewIndex >= 0 && m_viewIndex < s_compositor->viewCount();
    
    if (hasView != m_hasView) {
        m_hasView = hasView;
        emit hasViewChanged();
    }
    
    if (m_hasView) {
        QString newTitle = s_compositor->viewTitle(m_viewIndex);
        if (newTitle != m_title) {
            m_title = newTitle;
            emit titleChanged();
        }
        
        /* Focus this view in the compositor */
        s_compositor->focusView(m_viewIndex);
    }
    
    update();
}

void EmbeddedView::onViewsChanged() {
    updateViewState();
}

void EmbeddedView::updateFrame() {
    if (!m_hasView || !s_compositor) return;
    
    /* Request buffer from compositor for this view */
    QImage frame = s_compositor->getViewFrame(m_viewIndex);
    if (!frame.isNull() && frame.width() > 0 && frame.height() > 0) {
        QMutexLocker lock(&m_bufferMutex);
        m_frameBuffer = frame.copy();  /* Deep copy */
        m_needsUpdate = true;
        update();
    }
}

QSGNode* EmbeddedView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
    }
    
    QMutexLocker lock(&m_bufferMutex);
    
    if (!m_frameBuffer.isNull() && window()) {
        QSGTexture* texture = window()->createTextureFromImage(m_frameBuffer);
        if (texture) {
            node->setTexture(texture);
            node->setRect(boundingRect());
            node->markDirty(QSGNode::DirtyMaterial);
        }
    } else {
        /* No frame - show placeholder */
        QImage placeholder(qMax(1, (int)width()), qMax(1, (int)height()), QImage::Format_ARGB32);
        placeholder.fill(m_hasView ? QColor(40, 40, 40) : QColor(60, 60, 60));
        
        if (window()) {
            QSGTexture* texture = window()->createTextureFromImage(placeholder);
            if (texture) {
                node->setTexture(texture);
                node->setRect(boundingRect());
            }
        }
    }
    
    return node;
}

void EmbeddedView::keyPressEvent(QKeyEvent* event) {
    if (!s_compositor || !m_hasView) {
        QQuickItem::keyPressEvent(event);
        return;
    }
    
    if (event->isAutoRepeat()) {
        return;
    }
    
    /* Focus this view before sending input */
    s_compositor->focusView(m_viewIndex);
    
    quint32 linuxKey = qtKeyToLinux(event->key());
    if (linuxKey != 0) {
        qDebug() << "Key press:" << event->key() << "->" << linuxKey;
        s_compositor->sendKey(linuxKey, true);
    }
    
    event->accept();
}

void EmbeddedView::keyReleaseEvent(QKeyEvent* event) {
    if (!s_compositor || !m_hasView) {
        QQuickItem::keyReleaseEvent(event);
        return;
    }
    
    if (event->isAutoRepeat()) {
        return;
    }
    
    quint32 linuxKey = qtKeyToLinux(event->key());
    if (linuxKey != 0) {
        s_compositor->sendKey(linuxKey, false);
    }
    
    event->accept();
}

void EmbeddedView::mousePressEvent(QMouseEvent* event) {
    if (!s_compositor || !m_hasView) {
        QQuickItem::mousePressEvent(event);
        return;
    }
    
    /* Focus this view */
    s_compositor->focusView(m_viewIndex);
    forceActiveFocus();
    
    /* Send coordinates relative to this item */
    s_compositor->sendPointerMotion(event->position().x(), event->position().y());
    
    quint32 button = qtButtonToLinux(event->button());
    if (button != 0) {
        s_compositor->sendPointerButton(button, true);
    }
    
    event->accept();
}

void EmbeddedView::mouseReleaseEvent(QMouseEvent* event) {
    if (!s_compositor || !m_hasView) {
        QQuickItem::mouseReleaseEvent(event);
        return;
    }
    
    quint32 button = qtButtonToLinux(event->button());
    if (button != 0) {
        s_compositor->sendPointerButton(button, false);
    }
    
    event->accept();
}

void EmbeddedView::mouseMoveEvent(QMouseEvent* event) {
    if (!s_compositor || !m_hasView) {
        QQuickItem::mouseMoveEvent(event);
        return;
    }
    
    s_compositor->sendPointerMotion(event->position().x(), event->position().y());
    event->accept();
}

void EmbeddedView::hoverMoveEvent(QHoverEvent* event) {
    if (!s_compositor || !m_hasView) {
        QQuickItem::hoverMoveEvent(event);
        return;
    }
    
    s_compositor->sendPointerMotion(event->position().x(), event->position().y());
    event->accept();
}

void EmbeddedView::wheelEvent(QWheelEvent* event) {
    if (!s_compositor || !m_hasView) {
        QQuickItem::wheelEvent(event);
        return;
    }
    
    QPoint delta = event->angleDelta();
    
    if (delta.y() != 0) {
        s_compositor->sendPointerAxis(false, -delta.y() / 120.0 * 15.0);
    }
    if (delta.x() != 0) {
        s_compositor->sendPointerAxis(true, delta.x() / 120.0 * 15.0);
    }
    
    event->accept();
}

void EmbeddedView::focusInEvent(QFocusEvent* event) {
    qDebug() << "EmbeddedView" << m_viewIndex << "got focus";
    if (s_compositor && m_hasView) {
        s_compositor->focusView(m_viewIndex);
    }
    QQuickItem::focusInEvent(event);
}

void EmbeddedView::focusOutEvent(QFocusEvent* event) {
    qDebug() << "EmbeddedView" << m_viewIndex << "lost focus";
    QQuickItem::focusOutEvent(event);
}

quint32 EmbeddedView::qtKeyToLinux(int qtKey) const {
    switch (qtKey) {
    case Qt::Key_Escape: return KEY_ESC;
    case Qt::Key_1: return KEY_1;
    case Qt::Key_2: return KEY_2;
    case Qt::Key_3: return KEY_3;
    case Qt::Key_4: return KEY_4;
    case Qt::Key_5: return KEY_5;
    case Qt::Key_6: return KEY_6;
    case Qt::Key_7: return KEY_7;
    case Qt::Key_8: return KEY_8;
    case Qt::Key_9: return KEY_9;
    case Qt::Key_0: return KEY_0;
    case Qt::Key_Minus: return KEY_MINUS;
    case Qt::Key_Equal: return KEY_EQUAL;
    case Qt::Key_Backspace: return KEY_BACKSPACE;
    case Qt::Key_Tab: return KEY_TAB;
    case Qt::Key_Q: return KEY_Q;
    case Qt::Key_W: return KEY_W;
    case Qt::Key_E: return KEY_E;
    case Qt::Key_R: return KEY_R;
    case Qt::Key_T: return KEY_T;
    case Qt::Key_Y: return KEY_Y;
    case Qt::Key_U: return KEY_U;
    case Qt::Key_I: return KEY_I;
    case Qt::Key_O: return KEY_O;
    case Qt::Key_P: return KEY_P;
    case Qt::Key_BracketLeft: return KEY_LEFTBRACE;
    case Qt::Key_BracketRight: return KEY_RIGHTBRACE;
    case Qt::Key_Return: return KEY_ENTER;
    case Qt::Key_Enter: return KEY_ENTER;
    case Qt::Key_Control: return KEY_LEFTCTRL;
    case Qt::Key_A: return KEY_A;
    case Qt::Key_S: return KEY_S;
    case Qt::Key_D: return KEY_D;
    case Qt::Key_F: return KEY_F;
    case Qt::Key_G: return KEY_G;
    case Qt::Key_H: return KEY_H;
    case Qt::Key_J: return KEY_J;
    case Qt::Key_K: return KEY_K;
    case Qt::Key_L: return KEY_L;
    case Qt::Key_Semicolon: return KEY_SEMICOLON;
    case Qt::Key_Apostrophe: return KEY_APOSTROPHE;
    case Qt::Key_QuoteLeft: return KEY_GRAVE;
    case Qt::Key_Shift: return KEY_LEFTSHIFT;
    case Qt::Key_Backslash: return KEY_BACKSLASH;
    case Qt::Key_Z: return KEY_Z;
    case Qt::Key_X: return KEY_X;
    case Qt::Key_C: return KEY_C;
    case Qt::Key_V: return KEY_V;
    case Qt::Key_B: return KEY_B;
    case Qt::Key_N: return KEY_N;
    case Qt::Key_M: return KEY_M;
    case Qt::Key_Comma: return KEY_COMMA;
    case Qt::Key_Period: return KEY_DOT;
    case Qt::Key_Slash: return KEY_SLASH;
    case Qt::Key_Alt: return KEY_LEFTALT;
    case Qt::Key_Space: return KEY_SPACE;
    case Qt::Key_CapsLock: return KEY_CAPSLOCK;
    case Qt::Key_F1: return KEY_F1;
    case Qt::Key_F2: return KEY_F2;
    case Qt::Key_F3: return KEY_F3;
    case Qt::Key_F4: return KEY_F4;
    case Qt::Key_F5: return KEY_F5;
    case Qt::Key_F6: return KEY_F6;
    case Qt::Key_F7: return KEY_F7;
    case Qt::Key_F8: return KEY_F8;
    case Qt::Key_F9: return KEY_F9;
    case Qt::Key_F10: return KEY_F10;
    case Qt::Key_F11: return KEY_F11;
    case Qt::Key_F12: return KEY_F12;
    case Qt::Key_Home: return KEY_HOME;
    case Qt::Key_Up: return KEY_UP;
    case Qt::Key_PageUp: return KEY_PAGEUP;
    case Qt::Key_Left: return KEY_LEFT;
    case Qt::Key_Right: return KEY_RIGHT;
    case Qt::Key_End: return KEY_END;
    case Qt::Key_Down: return KEY_DOWN;
    case Qt::Key_PageDown: return KEY_PAGEDOWN;
    case Qt::Key_Insert: return KEY_INSERT;
    case Qt::Key_Delete: return KEY_DELETE;
    case Qt::Key_Meta: return KEY_LEFTMETA;
    default: return 0;
    }
}

quint32 EmbeddedView::qtButtonToLinux(Qt::MouseButton button) const {
    switch (button) {
    case Qt::LeftButton: return BTN_LEFT;
    case Qt::RightButton: return BTN_RIGHT;
    case Qt::MiddleButton: return BTN_MIDDLE;
    case Qt::BackButton: return BTN_SIDE;
    case Qt::ForwardButton: return BTN_EXTRA;
    default: return 0;
    }
}
