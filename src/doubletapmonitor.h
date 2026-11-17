#pragma once

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QPointF>

class QQuickItem;
class QQuickWindow;

class DoubleTapMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
public:
    explicit DoubleTapMonitor(QObject *parent = nullptr);
    QQuickItem *target() const;
    void setTarget(QQuickItem *target);
signals:
    void targetChanged();
    void doubleTapped(qreal x, qreal y);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void updateWindow(QQuickWindow *window);
    void handlePoint(const QPointF &point);
    QPointer<QQuickItem> m_target;
    QPointer<QQuickWindow> m_window;
    QElapsedTimer m_touchTimer;
    QElapsedTimer m_pressTimer;
    QPointF m_lastTouch;
    QPointF m_touchStart;
    bool m_touchMoved = false;
};
