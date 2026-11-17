#include "latexhighlighter.h"

#include <QTextCharFormat>

namespace {
const QStringList mathEnvironments = {
    QStringLiteral("math"), QStringLiteral("displaymath"), QStringLiteral("equation"),
    QStringLiteral("equation*"), QStringLiteral("align"), QStringLiteral("align*"),
    QStringLiteral("alignat"), QStringLiteral("alignat*"), QStringLiteral("gather"),
    QStringLiteral("gather*"), QStringLiteral("multline"), QStringLiteral("multline*")
};
const QStringList verbatimEnvironments = {
    QStringLiteral("verbatim"), QStringLiteral("verbatim*"), QStringLiteral("lstlisting"),
    QStringLiteral("minted")
};
}

LatexSyntaxHighlighter::LatexSyntaxHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
LatexHighlighter::LatexHighlighter(QObject *parent) : QObject(parent) {}
LatexHighlighter::~LatexHighlighter() { delete m_highlighter; }
QQuickTextDocument *LatexHighlighter::textDocument() const { return m_document; }
QColor LatexHighlighter::commandColor() const { return m_commandColor; }
QColor LatexHighlighter::commentColor() const { return m_commentColor; }
QColor LatexHighlighter::mathColor() const { return m_mathColor; }
QColor LatexHighlighter::argumentColor() const { return m_argumentColor; }
bool LatexHighlighter::bibMode() const { return m_bibMode; }

void LatexHighlighter::setTextDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    delete m_highlighter;
    m_document = document;
    if (document && document->textDocument()) {
        m_highlighter = new LatexSyntaxHighlighter(document->textDocument());
        m_highlighter->commandColor = m_commandColor;
        m_highlighter->commentColor = m_commentColor;
        m_highlighter->mathColor = m_mathColor;
        m_highlighter->argumentColor = m_argumentColor;
        m_highlighter->bibMode = m_bibMode;
        m_highlighter->rehighlight();
    }
    emit textDocumentChanged();
}

void LatexHighlighter::setBibMode(bool bibMode)
{
    if (m_bibMode == bibMode)
        return;
    m_bibMode = bibMode;
    if (m_highlighter) {
        m_highlighter->bibMode = bibMode;
        m_highlighter->rehighlight();
    }
    emit bibModeChanged();
}

void LatexHighlighter::setCommandColor(const QColor &color)
{
    if (m_commandColor == color)
        return;
    m_commandColor = color;
    if (m_highlighter) {
        m_highlighter->commandColor = color;
        m_highlighter->rehighlight();
    }
    emit colorsChanged();
}

void LatexHighlighter::setCommentColor(const QColor &color)
{
    if (m_commentColor == color)
        return;
    m_commentColor = color;
    if (m_highlighter) {
        m_highlighter->commentColor = color;
        m_highlighter->rehighlight();
    }
    emit colorsChanged();
}

void LatexHighlighter::setMathColor(const QColor &color)
{
    if (m_mathColor == color)
        return;
    m_mathColor = color;
    if (m_highlighter) {
        m_highlighter->mathColor = color;
        m_highlighter->rehighlight();
    }
    emit colorsChanged();
}

void LatexHighlighter::setArgumentColor(const QColor &color)
{
    if (m_argumentColor == color)
        return;
    m_argumentColor = color;
    if (m_highlighter) {
        m_highlighter->argumentColor = color;
        m_highlighter->rehighlight();
    }
    emit colorsChanged();
}

void LatexSyntaxHighlighter::highlightBlock(const QString &text)
{
    QTextCharFormat math;
    math.setForeground(mathColor);
    math.setFontItalic(true);
    QTextCharFormat command;
    command.setForeground(commandColor);
    command.setFontWeight(QFont::DemiBold);
    QTextCharFormat comment;
    comment.setForeground(commentColor);
    comment.setFontItalic(true);
    QTextCharFormat argument;
    argument.setForeground(argumentColor);
    QTextCharFormat literal = argument;
    literal.setFontItalic(true);
    if (bibMode) {
        bool quoted = previousBlockState() == 1;
        int i = 0;
        while (i < text.size()) {
            if (quoted || text.at(i) == QLatin1Char('"')) {
                const int start = i;
                if (!quoted) ++i;
                quoted = true;
                while (i < text.size()) {
                    if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size()) {
                        i += 2;
                    } else if (text.at(i++) == QLatin1Char('"')) {
                        quoted = false;
                        break;
                    }
                }
                setFormat(start, i - start, math);
            } else if (text.at(i) == QLatin1Char('%')) {
                setFormat(i, text.size() - i, comment);
                break;
            } else if (text.at(i) == QLatin1Char('@')) {
                const int start = i++;
                while (i < text.size() && text.at(i).isLetter()) ++i;
                setFormat(start, i - start, command);
                while (i < text.size() && text.at(i).isSpace()) ++i;
                if (i < text.size() && (text.at(i) == QLatin1Char('{') || text.at(i) == QLatin1Char('('))) {
                    const int keyStart = ++i;
                    while (i < text.size() && text.at(i) != QLatin1Char(',')
                           && text.at(i) != QLatin1Char('}') && text.at(i) != QLatin1Char(')')
                           && text.at(i) != QLatin1Char('%')) ++i;
                    setFormat(keyStart, i - keyStart, argument);
                }
            } else if (text.at(i).isLetter()) {
                const int start = i++;
                while (i < text.size() && (text.at(i).isLetterOrNumber()
                                            || text.at(i) == QLatin1Char('_')
                                            || text.at(i) == QLatin1Char('-'))) ++i;
                int next = i;
                while (next < text.size() && text.at(next).isSpace()) ++next;
                if (next < text.size() && text.at(next) == QLatin1Char('='))
                    setFormat(start, i - start, command);
            } else {
                if (text.at(i) == QLatin1Char('{') || text.at(i) == QLatin1Char('}'))
                    setFormat(i, 1, argument);
                ++i;
            }
        }
        setCurrentBlockState(quoted ? 1 : 0);
        return;
    }
    static const QStringList namedArguments = {
        QStringLiteral("label"), QStringLiteral("ref"), QStringLiteral("eqref"),
        QStringLiteral("pageref"), QStringLiteral("autoref"), QStringLiteral("cref"),
        QStringLiteral("Cref"), QStringLiteral("cite"), QStringLiteral("citep"),
        QStringLiteral("citet"), QStringLiteral("parencite"), QStringLiteral("textcite"),
        QStringLiteral("input"), QStringLiteral("include"), QStringLiteral("includegraphics"),
        QStringLiteral("usepackage"), QStringLiteral("documentclass"),
        QStringLiteral("bibliography"), QStringLiteral("addbibresource")
    };
    int mode = previousBlockState() > 0 ? previousBlockState() : 0;
    int i = 0;
    while (i < text.size()) {
        if (mode >= 100) {
            const int index = mode - 100;
            const QString end = QStringLiteral("\\end{") + verbatimEnvironments.at(index) + QLatin1Char('}');
            const int endAt = text.indexOf(end, i);
            if (endAt < 0) {
                setFormat(i, text.size() - i, literal);
                break;
            }
            setFormat(i, endAt - i, literal);
            i = endAt;
            mode = 0;
        }
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('%')) {
            setFormat(i, text.size() - i, comment);
            break;
        }
        if (ch == QLatin1Char('\\')) {
            const int start = i++;
            if (i < text.size() && (text.at(i).isLetter() || text.at(i) == QLatin1Char('@'))) {
                while (i < text.size() && (text.at(i).isLetter() || text.at(i) == QLatin1Char('@')))
                    ++i;
            } else if (i < text.size()) {
                ++i;
            }
            const QString name = text.mid(start + 1, i - start - 1);
            setFormat(start, i - start, command);
            if (name == QStringLiteral("verb")) {
                if (i < text.size() && text.at(i) == QLatin1Char('*'))
                    ++i;
                if (i < text.size() && !text.at(i).isSpace()) {
                    const QChar delimiter = text.at(i);
                    const int end = text.indexOf(delimiter, i + 1);
                    const int length = (end < 0 ? text.size() : end + 1) - i;
                    setFormat(i, length, literal);
                    i += length;
                }
                continue;
            }
            if ((name == QStringLiteral("begin") || name == QStringLiteral("end"))
                    && i < text.size() && text.at(i) == QLatin1Char('{')) {
                const int close = text.indexOf(QLatin1Char('}'), i + 1);
                if (close >= 0) {
                    const QString env = text.mid(i + 1, close - i - 1);
                    setFormat(i, close - i + 1, argument);
                    const int mathIndex = mathEnvironments.indexOf(env);
                    const int verbIndex = verbatimEnvironments.indexOf(env);
                    if (name == QStringLiteral("begin") && mode == 0) {
                        if (mathIndex >= 0) mode = 10 + mathIndex;
                        if (verbIndex >= 0) mode = 100 + verbIndex;
                    } else if (name == QStringLiteral("end") && mathIndex >= 0 && mode == 10 + mathIndex) {
                        mode = 0;
                    }
                    i = close + 1;
                }
                continue;
            }
            if (name == QStringLiteral("(") && mode == 0) mode = 3;
            else if (name == QStringLiteral("[") && mode == 0) mode = 4;
            else if ((name == QStringLiteral(")") && mode == 3)
                     || (name == QStringLiteral("]") && mode == 4)) mode = 0;
            if (namedArguments.contains(name)) {
                int next = i;
                if (next < text.size() && text.at(next) == QLatin1Char('*')) ++next;
                while (next < text.size() && text.at(next).isSpace()) ++next;
                if (next < text.size() && text.at(next) == QLatin1Char('[')) {
                    const int end = text.indexOf(QLatin1Char(']'), next + 1);
                    if (end >= 0 && !text.mid(next, end - next).contains(QLatin1Char('%'))) {
                        setFormat(next, end - next + 1, argument);
                        next = end + 1;
                        while (next < text.size() && text.at(next).isSpace()) ++next;
                    }
                }
                if (next < text.size() && text.at(next) == QLatin1Char('{')) {
                    const int end = text.indexOf(QLatin1Char('}'), next + 1);
                    const QString contents = text.mid(next + 1, end - next - 1);
                    if (end >= 0 && !contents.contains(QLatin1Char('%'))
                            && !contents.contains(QLatin1Char('\\')) && !contents.contains(QLatin1Char('{'))) {
                        setFormat(next, end - next + 1, argument);
                        i = end + 1;
                    }
                }
            }
            continue;
        }
        if (ch == QLatin1Char('$')) {
            const bool doubled = i + 1 < text.size() && text.at(i + 1) == QLatin1Char('$');
            const int length = doubled ? 2 : 1;
            if (mode == 0) mode = doubled ? 2 : 1;
            else if (mode == (doubled ? 2 : 1)) mode = 0;
            setFormat(i, length, math);
            i += length;
            continue;
        }
        if (ch == QLatin1Char('{') || ch == QLatin1Char('}')
                || ch == QLatin1Char('[') || ch == QLatin1Char(']')) {
            setFormat(i, 1, argument);
        } else if (mode > 0) {
            setFormat(i, 1, math);
        }
        ++i;
    }
    setCurrentBlockState(mode);
}
