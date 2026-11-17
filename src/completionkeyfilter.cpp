#include "completionkeyfilter.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTextCursor>
#include <QTextDocument>

CompletionKeyFilter::CompletionKeyFilter(QObject *parent) : QObject(parent) {}
QQuickItem *CompletionKeyFilter::target() const { return m_target.data(); }
bool CompletionKeyFilter::enabled() const { return m_enabled; }

void CompletionKeyFilter::setTarget(QQuickItem *target)
{
    if (m_target == target)
        return;
    if (m_target)
        m_target->removeEventFilter(this);
    m_target = target;
    if (m_target)
        m_target->installEventFilter(this);
    emit targetChanged();
}

void CompletionKeyFilter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

bool CompletionKeyFilter::replace(int start, int end, const QString &text)
{
    QQuickTextDocument *quickDocument = m_target
            ? m_target->property("textDocument").value<QQuickTextDocument *>() : nullptr;
    QTextDocument *document = quickDocument ? quickDocument->textDocument() : nullptr;
    if (!document || start < 0 || end < start || end >= document->characterCount())
        return false;
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.insertText(text);
    cursor.endEditBlock();
    return true;
}

bool CompletionKeyFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_target || !m_enabled || !m_target || !m_target->hasActiveFocus()
            || event->type() != QEvent::KeyPress)
        return false;
    QKeyEvent *key = static_cast<QKeyEvent *>(event);
    if (key->key() != Qt::Key_Tab || key->modifiers() != Qt::NoModifier)
        return false;
    emit accepted();
    key->accept();
    return true;
}
