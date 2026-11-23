#include "reversoclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QVariantMap>

namespace {
const int maxInputLength = 1000;
const int maxResponseLength = 1024 * 1024;
const int requestTimeoutMs = 15000;
const int maxTranslations = 40;
const int maxExamples = 8;

QRegularExpressionMatch wordMatchAtCursor(const QString &text, int position)
{
    static const QRegularExpression words(
        QStringLiteral("[\\p{L}\\p{M}\\p{N}]+(?:['\\x{2019}-][\\p{L}\\p{M}\\p{N}]+)*"),
        QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatchIterator matches = words.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (position >= match.capturedStart() && position <= match.capturedEnd())
            return match;
        if (match.capturedStart() > position)
            break;
    }
    return QRegularExpressionMatch();
}

bool overlaps(int start, int end, int targetStart, int targetEnd)
{
    return start < targetEnd && end > targetStart;
}

int groupEnd(const QString &text, int start, QChar open, QChar close)
{
    int depth = 1;
    for (int i = start + 1; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size()) {
            ++i;
        } else if (text.at(i) == open) {
            ++depth;
        } else if (text.at(i) == close && --depth == 0) {
            return i + 1;
        }
    }
    return text.size();
}

bool isPlainTextRange(const QString &text, int start, int end)
{
    static const QStringList mathEnvironments = {
        QStringLiteral("math"), QStringLiteral("displaymath"), QStringLiteral("equation"),
        QStringLiteral("equation*"), QStringLiteral("align"), QStringLiteral("align*"),
        QStringLiteral("alignat"), QStringLiteral("alignat*"), QStringLiteral("gather"),
        QStringLiteral("gather*"), QStringLiteral("multline"), QStringLiteral("multline*"),
        QStringLiteral("eqnarray"), QStringLiteral("eqnarray*"), QStringLiteral("flalign"),
        QStringLiteral("flalign*"), QStringLiteral("split"), QStringLiteral("aligned"),
        QStringLiteral("alignedat"), QStringLiteral("gathered"), QStringLiteral("cases"),
        QStringLiteral("array"), QStringLiteral("matrix"), QStringLiteral("smallmatrix"),
        QStringLiteral("pmatrix"), QStringLiteral("bmatrix"), QStringLiteral("Bmatrix"),
        QStringLiteral("vmatrix"), QStringLiteral("Vmatrix"), QStringLiteral("subequations")
    };
    static const QStringList literalEnvironments = {
        QStringLiteral("verbatim"), QStringLiteral("verbatim*"), QStringLiteral("lstlisting"),
        QStringLiteral("minted"), QStringLiteral("comment")
    };
    static const QStringList proseCommands = {
        QStringLiteral("textbf"), QStringLiteral("textit"), QStringLiteral("texttt"),
        QStringLiteral("textsl"), QStringLiteral("textsc"), QStringLiteral("textrm"),
        QStringLiteral("textsf"), QStringLiteral("emph"), QStringLiteral("mbox"),
        QStringLiteral("section"), QStringLiteral("subsection"), QStringLiteral("subsubsection"),
        QStringLiteral("chapter"), QStringLiteral("part"), QStringLiteral("paragraph"),
        QStringLiteral("subparagraph"), QStringLiteral("caption"), QStringLiteral("footnote"),
        QStringLiteral("title"), QStringLiteral("author")
    };
    static const QRegularExpression environment(
        QStringLiteral("\\\\(begin|end)\\s*\\{([^{}\\s]+)\\}"));
    static const QRegularExpression equation(
        QStringLiteral("[\\p{L}\\p{N})}]\\s*(?:=|[<>^_])\\s*[\\p{L}\\p{N}({\\\\]"),
        QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatchIterator equations = equation.globalMatch(text);
    while (equations.hasNext()) {
        const QRegularExpressionMatch match = equations.next();
        if (overlaps(match.capturedStart(), match.capturedEnd(), start, end))
            return false;
    }

    QString mathMarker;
    QStringList mathStack;
    for (int i = 0; i < end;) {
        if (text.at(i) == QLatin1Char('%')) {
            int next = text.indexOf(QLatin1Char('\n'), i);
            if (next < 0)
                next = text.size();
            if (overlaps(i, next, start, end))
                return false;
            i = next;
            continue;
        }
        const QRegularExpressionMatch env = environment.match(
            text, i, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
        if (env.hasMatch()) {
            const int next = env.capturedEnd();
            const QString name = env.captured(2);
            const bool begin = env.captured(1) == QLatin1String("begin");
            if (overlaps(i, next, start, end))
                return false;
            if (!mathStack.isEmpty()) {
                if (begin && mathEnvironments.contains(name))
                    mathStack.append(name);
                else if (!begin && name == mathStack.last())
                    mathStack.removeLast();
            } else if (mathMarker.isEmpty() && begin && mathEnvironments.contains(name)) {
                mathStack.append(name);
            } else if (mathMarker.isEmpty() && begin && literalEnvironments.contains(name)) {
                const QString closing = QStringLiteral("\\end{") + name + QLatin1Char('}');
                int close = text.indexOf(closing, next);
                close = close < 0 ? text.size() : close + closing.size();
                if (overlaps(i, close, start, end))
                    return false;
                i = close;
                continue;
            }
            i = next;
            continue;
        }
        if (!mathStack.isEmpty()) {
            if (overlaps(i, i + 1, start, end))
                return false;
            if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size())
                i += 2;
            else
                ++i;
            continue;
        }
        if (!mathMarker.isEmpty()) {
            const bool close = text.midRef(i, mathMarker.size()) == mathMarker;
            const int next = close ? i + mathMarker.size()
                                   : i + (text.at(i) == QLatin1Char('\\') && i + 1 < text.size() ? 2 : 1);
            if (overlaps(i, next, start, end))
                return false;
            if (close)
                mathMarker.clear();
            i = next;
            continue;
        }
        if (text.at(i) == QLatin1Char('$')) {
            const bool doubled = i + 1 < text.size() && text.at(i + 1) == QLatin1Char('$');
            const int next = i + (doubled ? 2 : 1);
            if (overlaps(i, next, start, end))
                return false;
            mathMarker = doubled ? QStringLiteral("$$") : QStringLiteral("$");
            i = next;
            continue;
        }
        if (text.at(i) == QLatin1Char('\\')) {
            const int commandStart = i++;
            if (i < text.size() && (text.at(i) == QLatin1Char('(') || text.at(i) == QLatin1Char('['))) {
                mathMarker = text.at(i) == QLatin1Char('(') ? QStringLiteral("\\)") : QStringLiteral("\\]");
                ++i;
            } else if (i < text.size() && text.at(i).isLetter()) {
                while (i < text.size() && (text.at(i).isLetter() || text.at(i) == QLatin1Char('@')))
                    ++i;
            } else if (i < text.size()) {
                ++i;
            }
            const QString command = text.mid(commandStart + 1, i - commandStart - 1);
            if (overlaps(commandStart, i, start, end))
                return false;
            if (!mathMarker.isEmpty())
                continue;
            if (command == QLatin1String("verb")) {
                if (i < text.size() && text.at(i) == QLatin1Char('*'))
                    ++i;
                if (i < text.size() && !text.at(i).isSpace()) {
                    const QChar delimiter = text.at(i);
                    int close = text.indexOf(delimiter, i + 1);
                    const int newline = text.indexOf(QLatin1Char('\n'), i + 1);
                    if (close < 0 || (newline >= 0 && newline < close))
                        close = newline < 0 ? text.size() : newline;
                    else
                        ++close;
                    if (overlaps(i, close, start, end))
                        return false;
                    i = close;
                }
                continue;
            }
            int next = i;
            if (next < text.size() && text.at(next) == QLatin1Char('*'))
                ++next;
            while (next < text.size() && text.at(next).isSpace())
                ++next;
            while (next < text.size() && text.at(next) == QLatin1Char('[')) {
                next = groupEnd(text, next, QLatin1Char('['), QLatin1Char(']'));
                while (next < text.size() && text.at(next).isSpace())
                    ++next;
            }
            if (!proseCommands.contains(command)) {
                while (next < text.size() && (text.at(next) == QLatin1Char('{')
                                               || text.at(next) == QLatin1Char('['))) {
                    const QChar open = text.at(next);
                    next = groupEnd(text, next, open,
                                    open == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(']'));
                    int after = next;
                    while (after < text.size() && text.at(after).isSpace())
                        ++after;
                    if (after >= text.size() || (text.at(after) != QLatin1Char('{')
                                                 && text.at(after) != QLatin1Char('[')))
                        break;
                    next = after;
                }
            }
            if (overlaps(i, next, start, end))
                return false;
            i = next;
            continue;
        }
        if ((text.at(i) == QLatin1Char('{') || text.at(i) == QLatin1Char('}'))
                && overlaps(i, i + 1, start, end))
            return false;
        ++i;
    }
    return true;
}
}

ReversoClient::ReversoClient(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : &m_ownedNetwork)
{
}

void ReversoClient::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void ReversoClient::setTranslations(const QStringList &translations)
{
    if (m_translations == translations)
        return;
    m_translations = translations;
    emit translationsChanged();
}

void ReversoClient::setExamples(const QVariantList &examples)
{
    if (m_examples == examples)
        return;
    m_examples = examples;
    emit examplesChanged();
}

void ReversoClient::request(const QString &text, const QString &source, const QString &target)
{
    if (busy()) {
        setError(tr("Wait for the current Reverso request to finish."));
        return;
    }
    setTranslations(QStringList());
    setExamples(QVariantList());
    const QString input = text.trimmed();
    static const QStringList languages = {
        QStringLiteral("en"), QStringLiteral("ru"), QStringLiteral("de"),
        QStringLiteral("fr"), QStringLiteral("es"), QStringLiteral("it")
    };
    if (input.isEmpty() || input.size() > maxInputLength) {
        setError(input.isEmpty() ? tr("Enter text to translate.")
                                 : tr("The text is too long to translate."));
        return;
    }
    if (!languages.contains(source) || !languages.contains(target) || source == target) {
        setError(tr("Select two different supported languages."));
        return;
    }

    const int words = input.split(QRegularExpression(QStringLiteral("\\s+")), QString::SkipEmptyParts).size();
    setError(QString());
    startRequest(input, source, target, words > 3, false);
}

void ReversoClient::startRequest(const QString &input, const QString &source, const QString &target,
                                 bool sentence, bool contextOnly)
{
    const int words = input.split(QRegularExpression(QStringLiteral("\\s+")), QString::SkipEmptyParts).size();
    const bool word = words == 1;
    const QString endpoint = sentence ? QStringLiteral("TranslateText")
                                      : word ? QStringLiteral("TranslateWord")
                                             : QStringLiteral("TranslateSimple");
    QJsonObject payload;
    payload.insert(QStringLiteral("Source"), input);
    payload.insert(QStringLiteral("Direction"), source + QLatin1Char('-') + target);
    payload.insert(QStringLiteral("DeviceId"), QStringLiteral("0"));
    payload.insert(QStringLiteral("UiLang"), QStringLiteral("en"));
    payload.insert(QStringLiteral("Origin"), QStringLiteral("chromeextension"));
    payload.insert(QStringLiteral("AccessToken"), QString());
    payload.insert(QStringLiteral("AppId"), QStringLiteral("0"));
    if (word && !sentence) {
        payload.insert(QStringLiteral("Word"), input);
        payload.insert(QStringLiteral("WordPos"), QStringLiteral("0"));
        payload.insert(QStringLiteral("PageUrl"), QStringLiteral("0"));
        payload.insert(QStringLiteral("PageTitle"), QStringLiteral("0"));
        payload.insert(QStringLiteral("ReversoPage"), QStringLiteral("null"));
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://cps.reverso.net/api2/") + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        QByteArray response = reply->property("abedulResponse").toByteArray();
        response += reply->readAll();
        reply->setProperty("abedulResponse", response);
        if (response.size() > maxResponseLength) {
            reply->setProperty("abedulResponseTooLarge", true);
            reply->abort();
        }
    });
    QTimer *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("abedulTimeout", true);
        reply->abort();
    });
    timeout->start(requestTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, input, source, target, sentence, contextOnly]() {
        if (m_reply == reply) {
            m_reply.clear();
            emit busyChanged();
        }
        QByteArray body = reply->property("abedulResponse").toByteArray();
        body += reply->readAll();
        if (reply->property("abedulResponseTooLarge").toBool() || body.size() > maxResponseLength) {
            setError(tr("Reverso returned a response that is too large."));
        } else if (reply->error() == QNetworkReply::OperationCanceledError) {
            if (reply->property("abedulTimeout").toBool())
                setError(tr("The Reverso request timed out."));
        } else {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
                setError(status ? tr("Reverso request failed (HTTP %1).").arg(status)
                                : tr("Reverso request failed. Check your connection."));
            } else {
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
                const QJsonObject response = document.object();
                if (parseError.error != QJsonParseError::NoError || !document.isObject()
                        || !response.value(QStringLiteral("success")).toBool()
                        || response.value(QStringLiteral("error")).toBool()) {
                    setError(tr("Reverso returned an invalid response."));
                } else if (sentence) {
                    const QString translation = response.value(QStringLiteral("translation")).toString().simplified();
                    if (translation.isEmpty()) {
                        setError(tr("Reverso found no translations."));
                    } else {
                        setTranslations(QStringList(translation));
                        reply->deleteLater();
                        startRequest(input, source, target, false, true);
                        return;
                    }
                } else {
                    QStringList translations;
                    QVariantList examples;
                    if (!response.value(QStringLiteral("sources")).isArray()) {
                        setError(tr("Reverso returned an invalid response."));
                        reply->deleteLater();
                        return;
                    }
                    for (const QJsonValue &sourceValue : response.value(QStringLiteral("sources")).toArray()) {
                        const QJsonArray entries = sourceValue.toObject().value(QStringLiteral("translations")).toArray();
                        for (const QJsonValue &entry : entries) {
                            const QJsonObject item = entry.toObject();
                            const QString term = item.value(QStringLiteral("translation")).toString().simplified();
                            if (!term.isEmpty() && term != QLatin1String("...")
                                    && translations.size() < maxTranslations
                                    && !translations.contains(term, Qt::CaseInsensitive))
                                translations.append(term.left(maxInputLength));
                            const QJsonArray contexts = item.value(QStringLiteral("contexts")).toArray();
                            for (const QJsonValue &context : contexts) {
                                const QJsonObject pair = context.toObject();
                                const QString sourceText = QTextDocumentFragment::fromHtml(
                                    pair.value(QStringLiteral("source")).toString()).toPlainText().simplified().left(2000);
                                const QString targetText = QTextDocumentFragment::fromHtml(
                                    pair.value(QStringLiteral("target")).toString()).toPlainText().simplified().left(2000);
                                if (!sourceText.isEmpty() && !targetText.isEmpty() && examples.size() < maxExamples) {
                                    QVariantMap example;
                                    example.insert(QStringLiteral("source"), sourceText);
                                    example.insert(QStringLiteral("target"), targetText);
                                    if (!examples.contains(example))
                                        examples.append(example);
                                }
                            }
                        }
                    }
                    if (!contextOnly) {
                        if (translations.isEmpty())
                            setError(tr("Reverso found no translations."));
                        else
                            setTranslations(translations);
                    }
                    setExamples(examples);
                }
            }
        }
        reply->deleteLater();
    });
    emit busyChanged();
}

void ReversoClient::cancel()
{
    if (m_reply)
        m_reply->abort();
}

QString ReversoClient::wordAtCursor(const QString &text, int position) const
{
    if (position < 0 || position > text.size())
        return QString();
    return wordMatchAtCursor(text, position).captured();
}

QString ReversoClient::plainTextAtCursor(const QString &text, int position,
                                         int selectionStart, int selectionEnd) const
{
    if (position < 0 || position > text.size() || selectionStart < 0
            || selectionEnd < selectionStart || selectionEnd > text.size())
        return QString();
    int start = selectionStart;
    int end = selectionEnd;
    if (start == end) {
        const QRegularExpressionMatch word = wordMatchAtCursor(text, position);
        if (!word.hasMatch())
            return QString();
        start = word.capturedStart();
        end = word.capturedEnd();
    }
    static const QRegularExpression letter(QStringLiteral("\\p{L}"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    const QString selected = text.mid(start, end - start);
    if (!selected.contains(letter) || !isPlainTextRange(text, start, end))
        return QString();
    return selected;
}
