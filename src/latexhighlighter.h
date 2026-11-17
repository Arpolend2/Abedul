#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>

class LatexSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    explicit LatexSyntaxHighlighter(QTextDocument *document);
    QColor commandColor;
    QColor commentColor;
    QColor mathColor;
    QColor argumentColor;
    bool bibMode = false;

protected:
    void highlightBlock(const QString &text) override;
};

class LatexHighlighter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    Q_PROPERTY(QColor commandColor READ commandColor WRITE setCommandColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor commentColor READ commentColor WRITE setCommentColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor mathColor READ mathColor WRITE setMathColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor argumentColor READ argumentColor WRITE setArgumentColor NOTIFY colorsChanged)
    Q_PROPERTY(bool bibMode READ bibMode WRITE setBibMode NOTIFY bibModeChanged)
public:
    explicit LatexHighlighter(QObject *parent = nullptr);
    ~LatexHighlighter() override;
    QQuickTextDocument *textDocument() const;
    void setTextDocument(QQuickTextDocument *document);
    QColor commandColor() const;
    QColor commentColor() const;
    QColor mathColor() const;
    QColor argumentColor() const;
    bool bibMode() const;
    void setCommandColor(const QColor &color);
    void setCommentColor(const QColor &color);
    void setMathColor(const QColor &color);
    void setArgumentColor(const QColor &color);
    void setBibMode(bool bibMode);

signals:
    void textDocumentChanged();
    void colorsChanged();
    void bibModeChanged();

private:
    QPointer<QQuickTextDocument> m_document;
    QPointer<LatexSyntaxHighlighter> m_highlighter;
    QColor m_commandColor;
    QColor m_commentColor;
    QColor m_mathColor;
    QColor m_argumentColor;
    bool m_bibMode = false;
};
