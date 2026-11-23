#include "wolframalphaclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
const int maxAppIdLength = 256;
const int maxEquationLength = 2048;
const int maxResponseLength = 1024 * 1024;
const int requestTimeoutMs = 30000;
const int maxResultPods = 8;
const int maxResultLength = 6000;

QString appIdPath()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("wolframalpha/appid"));
}

QFile::Permissions unsafePermissions()
{
    return QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
}

bool isEscaped(const QString &text, int position)
{
    int backslashes = 0;
    for (int i = position - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++backslashes;
    return backslashes % 2 != 0;
}

int skipComment(const QString &text, int position)
{
    while (position < text.size() && text.at(position) != QLatin1Char('\n'))
        ++position;
    return position;
}

bool isMathEnvironment(const QString &environment)
{
    static const char *const environments[] = {
        "equation", "equation*", "align", "align*", "gather", "gather*",
        "multline", "multline*", "displaymath", "math", "eqnarray", "eqnarray*"
    };
    for (const char *candidate : environments) {
        if (environment == QLatin1String(candidate))
            return true;
    }
    return false;
}

int findClosingMarker(const QString &text, int from, const QString &marker)
{
    for (int i = from; i <= text.size() - marker.size();) {
        if (text.at(i) == QLatin1Char('%') && !isEscaped(text, i)) {
            i = skipComment(text, i);
            continue;
        }
        if (text.midRef(i, marker.size()) == marker && !isEscaped(text, i))
            return i;
        ++i;
    }
    return -1;
}

QString mathEnvironmentInput(const QString &text, const QString &environment)
{
    const bool aligned = environment == QLatin1String("eqnarray")
            || environment == QLatin1String("eqnarray*")
            || environment == QLatin1String("align")
            || environment == QLatin1String("align*");
    QString result;
    int braceDepth = 0;
    int nestedEnvironmentDepth = 0;
    for (int i = 0; i < text.size();) {
        if (text.at(i) == QLatin1Char('%') && !isEscaped(text, i)) {
            i = skipComment(text, i);
            continue;
        }
        if (text.at(i) == QLatin1Char('\\') && !isEscaped(text, i)) {
            if (text.midRef(i, 6) == QStringLiteral("\\label")) {
                int start = i + 6;
                while (start < text.size() && text.at(start).isSpace())
                    ++start;
                if (start < text.size() && text.at(start) == QLatin1Char('{')) {
                    int depth = 1;
                    int end = start + 1;
                    while (end < text.size() && depth > 0) {
                        if (!isEscaped(text, end)) {
                            if (text.at(end) == QLatin1Char('{'))
                                ++depth;
                            else if (text.at(end) == QLatin1Char('}'))
                                --depth;
                        }
                        ++end;
                    }
                    if (depth == 0) {
                        i = end;
                        continue;
                    }
                }
            }
            const bool nestedBegin = text.midRef(i, 7) == QStringLiteral("\\begin{");
            const bool nestedEnd = text.midRef(i, 5) == QStringLiteral("\\end{");
            if (nestedBegin || nestedEnd) {
                const int end = text.indexOf(QLatin1Char('}'), i + (nestedBegin ? 7 : 5));
                if (end >= 0) {
                    nestedEnvironmentDepth += nestedBegin ? 1 : -1;
                    result += text.mid(i, end - i + 1);
                    i = end + 1;
                    continue;
                }
            }
        }
        const QChar ch = text.at(i);
        if (!isEscaped(text, i)) {
            if (ch == QLatin1Char('{'))
                ++braceDepth;
            else if (ch == QLatin1Char('}') && braceDepth > 0)
                --braceDepth;
            else if (aligned && ch == QLatin1Char('&') && braceDepth == 0 && nestedEnvironmentDepth == 0) {
                ++i;
                continue;
            }
        }
        result += ch;
        ++i;
    }
    return result.trimmed();
}

QString plainTextResult(const QJsonObject &queryResult)
{
    const QJsonArray pods = queryResult.value(QStringLiteral("pods")).toArray();
    QStringList sections;
    for (const QJsonValue &podValue : pods) {
        const QJsonObject pod = podValue.toObject();
        if (pod.value(QStringLiteral("id")).toString() == QLatin1String("Input"))
            continue;

        QStringList subpods;
        const QJsonArray values = pod.value(QStringLiteral("subpods")).toArray();
        for (const QJsonValue &value : values) {
            const QString text = value.toObject().value(QStringLiteral("plaintext"))
                    .toString().simplified();
            if (!text.isEmpty())
                subpods.append(text);
        }
        if (subpods.isEmpty())
            continue;

        const QString title = pod.value(QStringLiteral("title")).toString().simplified();
        sections.append((title.isEmpty() ? QString() : title + QLatin1Char('\n'))
                        + subpods.join(QLatin1Char('\n')));
        if (sections.size() >= maxResultPods)
            break;
    }
    return sections.join(QStringLiteral("\n\n")).left(maxResultLength).trimmed();
}
}

WolframAlphaClient::WolframAlphaClient(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : &m_ownedNetwork)
{
    loadAppId();
}

void WolframAlphaClient::setAppId(const QString &appId)
{
    const QString normalized = appId.trimmed();
    if (normalized == m_appId)
        return;
    if (normalized.size() > maxAppIdLength || normalized.contains(QLatin1Char('\n'))
            || normalized.contains(QLatin1Char('\r'))) {
        setError(tr("The WolframAlpha AppID is invalid."));
        return;
    }

    QString saveError;
    if (!saveAppId(normalized, &saveError)) {
        setError(saveError);
        return;
    }
    m_appId = normalized;
    setError(QString());
    emit appIdChanged();
}

void WolframAlphaClient::reload()
{
    const QString previous = m_appId;
    m_appId.clear();
    setError(QString());
    loadAppId();
    if (m_appId != previous)
        emit appIdChanged();
}

void WolframAlphaClient::loadAppId()
{
    const QString path = appIdPath();
    if (path.isEmpty())
        return;

    const QFileInfo directory(QFileInfo(path).absolutePath());
    const QFileInfo info(path);
    if (!info.exists())
        return;
    if (directory.isSymLink() || !directory.isDir() || (directory.permissions() & unsafePermissions())
            || info.isSymLink() || !info.isFile() || (info.permissions() & unsafePermissions())) {
        setError(tr("The saved WolframAlpha AppID is not private. Set it again in Settings."));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(tr("Could not read the saved WolframAlpha AppID."));
        return;
    }
    const QByteArray stored = file.readAll();
    if (stored.size() > maxAppIdLength || stored.contains('\n') || stored.contains('\r')) {
        setError(tr("The saved WolframAlpha AppID is invalid. Set it again in Settings."));
        return;
    }
    m_appId = QString::fromUtf8(stored).trimmed();
}

bool WolframAlphaClient::saveAppId(const QString &appId, QString *error)
{
    const QString path = appIdPath();
    const QString directory = QFileInfo(path).absolutePath();
    if (path.isEmpty()) {
        *error = tr("Could not locate private application storage for the WolframAlpha AppID.");
        return false;
    }
    if (appId.isEmpty()) {
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() && !QFile::remove(path))) {
            *error = tr("Could not remove the saved WolframAlpha AppID.");
            return false;
        }
        return true;
    }
    if (!QDir().mkpath(directory) || QFileInfo(directory).isSymLink()
            || !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)
            || (QFileInfo(directory).permissions() & unsafePermissions())) {
        *error = tr("Could not protect the WolframAlpha AppID directory.");
        return false;
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const QByteArray bytes = appId.toUtf8();
    if (!file.open(QIODevice::WriteOnly)
            || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
            || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = tr("Could not save the WolframAlpha AppID.");
        return false;
    }
    return true;
}

void WolframAlphaClient::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void WolframAlphaClient::setResult(const QString &result)
{
    if (m_result == result)
        return;
    m_result = result;
    emit resultChanged();
}

void WolframAlphaClient::request(const QString &equation)
{
    if (busy()) {
        setError(tr("Wait for the current WolframAlpha request to finish."));
        return;
    }
    const QString input = equation.trimmed();
    setResult(QString());
    if (input.isEmpty() || input.size() > maxEquationLength) {
        setError(input.isEmpty() ? tr("No equation was found at the cursor.")
                                 : tr("The equation is too long to send to WolframAlpha."));
        return;
    }
    if (m_appId.isEmpty()) {
        if (m_error.isEmpty())
            setError(tr("Add your WolframAlpha AppID in Settings first."));
        return;
    }

    QUrl url(QStringLiteral("https://api.wolframalpha.com/v2/query"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("appid"), m_appId);
    query.addQueryItem(QStringLiteral("input"), QString::fromLatin1(QUrl::toPercentEncoding(input)));
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("plaintext"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");

    setError(QString());
    QNetworkReply *reply = m_network->get(request);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool current = m_reply == reply;
        if (current)
            m_reply.clear();
        if (current)
            emit busyChanged();

        QByteArray body = reply->property("abedulResponse").toByteArray();
        body += reply->readAll();
        if (reply->property("abedulResponseTooLarge").toBool() || body.size() > maxResponseLength) {
            setError(tr("WolframAlpha returned a response that is too large."));
            reply->deleteLater();
            return;
        }
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            if (reply->property("abedulTimeout").toBool())
                setError(tr("The WolframAlpha request timed out."));
            reply->deleteLater();
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            setError(status
                     ? tr("WolframAlpha request failed (HTTP %1).").arg(status)
                     : tr("WolframAlpha request failed. Check your connection."));
            reply->deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setError(tr("WolframAlpha returned an invalid response."));
            reply->deleteLater();
            return;
        }
        const QJsonObject queryResult = document.object().value(QStringLiteral("queryresult")).toObject();
        if (queryResult.isEmpty()) {
            setError(tr("WolframAlpha returned an invalid response."));
            reply->deleteLater();
            return;
        }
        if (queryResult.value(QStringLiteral("error")).toBool()
                || queryResult.value(QStringLiteral("error")).isObject()) {
            const QString detail = queryResult.value(QStringLiteral("error")).toObject()
                    .value(QStringLiteral("msg")).toString().simplified().left(300);
            setError(detail.isEmpty() ? tr("WolframAlpha could not process this equation.")
                                      : detail);
            reply->deleteLater();
            return;
        }
        if (!queryResult.value(QStringLiteral("success")).toBool()) {
            setError(tr("WolframAlpha could not solve this equation."));
            reply->deleteLater();
            return;
        }
        const QString text = plainTextResult(queryResult);
        if (text.isEmpty()) {
            setError(tr("WolframAlpha returned no text result for this equation."));
            reply->deleteLater();
            return;
        }
        setResult(text);
        emit resultReady(text);
        reply->deleteLater();
    });
    emit busyChanged();
}

void WolframAlphaClient::cancel()
{
    if (m_reply)
        m_reply->abort();
}

QString WolframAlphaClient::equationAtCursor(const QString &source, int cursorPosition) const
{
    if (cursorPosition < 0 || cursorPosition > source.size())
        return QString();

    int i = 0;
    while (i < source.size()) {
        if (source.at(i) == QLatin1Char('%') && !isEscaped(source, i)) {
            i = skipComment(source, i);
            continue;
        }

        if (source.midRef(i, 7) == QStringLiteral("\\begin{") && !isEscaped(source, i)) {
            const int nameStart = i + 7;
            const int brace = source.indexOf(QLatin1Char('}'), nameStart);
            if (brace >= 0) {
                const QString environment = source.mid(nameStart, brace - nameStart);
                if (isMathEnvironment(environment)) {
                    const QString closeTag = QStringLiteral("\\end{") + environment + QLatin1Char('}');
                    const int end = findClosingMarker(source, brace + 1, closeTag);
                    if (end >= 0) {
                        if (cursorPosition >= i && cursorPosition <= end + closeTag.size())
                            return mathEnvironmentInput(source.mid(brace + 1, end - brace - 1), environment);
                        i = end + closeTag.size();
                        continue;
                    }
                }
            }
        }

        QString opening;
        QString closing;
        if (source.midRef(i, 2) == QStringLiteral("\\(") && !isEscaped(source, i)) {
            opening = QStringLiteral("\\(");
            closing = QStringLiteral("\\)");
        } else if (source.midRef(i, 2) == QStringLiteral("\\[") && !isEscaped(source, i)) {
            opening = QStringLiteral("\\[");
            closing = QStringLiteral("\\]");
        } else if (source.at(i) == QLatin1Char('$') && !isEscaped(source, i)) {
            if (i + 1 < source.size() && source.at(i + 1) == QLatin1Char('$')) {
                opening = QStringLiteral("$$");
                closing = opening;
            } else if ((i == 0 || source.at(i - 1) != QLatin1Char('$'))
                       && (i + 1 == source.size() || source.at(i + 1) != QLatin1Char('$'))) {
                opening = QStringLiteral("$");
                closing = opening;
            }
        }

        if (!opening.isEmpty()) {
            const int contentStart = i + opening.size();
            const int end = findClosingMarker(source, contentStart, closing);
            if (end >= 0) {
                if (opening == QLatin1String("$")
                        && ((end > 0 && source.at(end - 1) == QLatin1Char('$'))
                            || (end + 1 < source.size() && source.at(end + 1) == QLatin1Char('$')))) {
                    i = end + 1;
                    continue;
                }
                if (cursorPosition >= contentStart && cursorPosition <= end)
                    return source.mid(contentStart, end - contentStart).trimmed();
                i = end + closing.size();
                continue;
            }
        }
        ++i;
    }
    return QString();
}
