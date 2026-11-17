#pragma once

#include <QObject>
#include <QPointer>

class QQuickItem;

class CompletionKeyFilter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
public:
    explicit CompletionKeyFilter(QObject *parent = nullptr);
    QQuickItem *target() const;
    void setTarget(QQuickItem *target);
    bool enabled() const;
    void setEnabled(bool enabled);
    Q_INVOKABLE bool replace(int start, int end, const QString &text);

signals:
    void targetChanged();
    void enabledChanged();
    void accepted();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QQuickItem> m_target;
    bool m_enabled = false;
};
