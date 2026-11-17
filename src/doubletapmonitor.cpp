#include "doubletapmonitor.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QLineF>

DoubleTapMonitor::DoubleTapMonitor(QObject *parent) : QObject(parent) {}
QQuickItem *DoubleTapMonitor::target() const { return m_target.data(); }

void DoubleTapMonitor::setTarget(QQuickItem *target)
{
    if (m_target == target)
        return;
    if (m_target)
        disconnect(m_target, nullptr, this, nullptr);
    m_target = target;
    if (target)
        connect(target, &QQuickItem::windowChanged, this, &DoubleTapMonitor::updateWindow);
    updateWindow(target ? target->window() : nullptr);
    emit targetChanged();
}

void DoubleTapMonitor::updateWindow(QQuickWindow *window)
{
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
}

void DoubleTapMonitor::handlePoint(const QPointF &point)
{
    if (!m_target || !m_target->isVisible() || !m_target->isEnabled())
        return;
    const QPointF local = m_target->mapFromScene(point);
    if (!m_target->contains(local))
        return;
    for (QQuickItem *item = m_target->parentItem(); item; item = item->parentItem()) {
        if (item->clip() && !item->contains(item->mapFromScene(point)))
            return;
    }
    emit doubleTapped(local.x(), local.y());
}

bool DoubleTapMonitor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window)
        return false;
    if (event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && mouse->source() == Qt::MouseEventNotSynthesized)
            handlePoint(mouse->localPos());
    } else if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
               || event->type() == QEvent::TouchEnd) {
        QTouchEvent *touch = static_cast<QTouchEvent *>(event);
        if (touch->touchPoints().size() != 1) {
            m_touchMoved = true;
            m_touchTimer.invalidate();
        } else if (event->type() == QEvent::TouchBegin) {
            m_touchStart = touch->touchPoints().first().pos();
            m_touchMoved = false;
            m_pressTimer.restart();
        } else if (event->type() == QEvent::TouchUpdate) {
            if (QLineF(touch->touchPoints().first().pos(), m_touchStart).length() >= 25)
                m_touchMoved = true;
        } else if (!m_touchMoved && m_pressTimer.isValid() && m_pressTimer.elapsed() < 350
                   && QLineF(touch->touchPoints().first().pos(), m_touchStart).length() < 25) {
            const QPointF point = touch->touchPoints().first().pos();
            if (m_touchTimer.isValid() && m_touchTimer.elapsed() < 350
                    && QLineF(point, m_lastTouch).length() < 25) {
                m_touchTimer.invalidate();
                handlePoint(point);
            } else {
                m_lastTouch = point;
                m_touchTimer.restart();
            }
        } else {
            m_touchTimer.invalidate();
        }
    } else if (event->type() == QEvent::TouchCancel) {
        m_touchTimer.invalidate();
        m_touchMoved = true;
    }
    return false;
}
