#include "compileoutput.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrlQuery>

bool compileRequestBody(const QString &rootPath, QByteArray *body, QString *error,
                        const QString &editorId)
{
    QString path = rootPath;
    if (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    if (path.isEmpty() || path.startsWith(QLatin1Char('/'))
            || path.endsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\'))
            || path.contains(QChar::Null)
            || path.split(QLatin1Char('/')).contains(QStringLiteral(".."))
            || path.split(QLatin1Char('/')).contains(QStringLiteral("."))) {
        *error = QCoreApplication::translate("CompileOutput", "Select a valid main document.");
        return false;
    }
    QJsonObject payload{
        {QStringLiteral("check"), QStringLiteral("silent")},
        {QStringLiteral("draft"), false},
        {QStringLiteral("incrementalCompilesEnabled"), true},
        {QStringLiteral("rootResourcePath"), path},
        {QStringLiteral("stopOnFirstError"), false}
    };
    if (!editorId.isEmpty())
        payload.insert(QStringLiteral("editorId"), editorId);
    *body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    error->clear();
    return true;
}

QString compileServerError(const QByteArray &response)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return QString();
    const QJsonObject object = doc.object();
    QString message = object.value(QStringLiteral("error")).toString();
    if (message.isEmpty())
        message = object.value(QStringLiteral("message")).toString();
    return message.simplified().left(300);
}

bool compilePdfUrl(const QByteArray &response, const QUrl &server,
                   QUrl *url, bool *sendCookies, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QCoreApplication::translate("CompileOutput", "Invalid compile response.");
        return false;
    }
    const QJsonObject result = document.object();
    if (result.value(QStringLiteral("status")).toString() != QStringLiteral("success")) {
        *error = QCoreApplication::translate("CompileOutput", "The server could not compile the project.");
        return false;
    }
    const QJsonValue files = result.value(QStringLiteral("outputFiles"));
    if (!files.isArray()) {
        *error = QCoreApplication::translate("CompileOutput", "The compile response has no output files.");
        return false;
    }
    QString path;
    for (const QJsonValue &value : files.toArray()) {
        const QJsonObject file = value.toObject();
        if (file.value(QStringLiteral("type")).toString() == QStringLiteral("pdf")
                || file.value(QStringLiteral("path")).toString() == QStringLiteral("output.pdf")) {
            path = file.value(QStringLiteral("url")).toString();
            break;
        }
    }
    const QUrl relative(path);
    if (path.isEmpty() || !relative.isValid() || !relative.isRelative()
            || relative.path().startsWith(QStringLiteral("//")) || relative.hasFragment()) {
        *error = QCoreApplication::translate("CompileOutput", "The compile response has an invalid PDF URL.");
        return false;
    }
    QString relativePath = relative.path();
    while (relativePath.startsWith(QLatin1Char('/')))
        relativePath.remove(0, 1);
    if (relativePath.isEmpty() || relativePath.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
        *error = QCoreApplication::translate("CompileOutput", "The compile response has an invalid PDF path.");
        return false;
    }
    const QString cdn = result.value(QStringLiteral("pdfDownloadDomain")).toString();
    const QString clsi = result.value(QStringLiteral("clsiServerId")).toString();
    QUrl base = server;
    *sendCookies = true;
    if (!cdn.isEmpty() && !clsi.isEmpty()) {
        base = QUrl(cdn);
        if (!base.isValid() || base.scheme() != QStringLiteral("https")
                || base.host().isEmpty() || !base.userInfo().isEmpty()
                || base.hasQuery() || base.hasFragment()) {
            *error = QCoreApplication::translate("CompileOutput", "The server returned an invalid PDF download domain.");
            return false;
        }
        *sendCookies = false;
    }
    QString basePath = base.path();
    if (!basePath.endsWith(QLatin1Char('/')))
        basePath += QLatin1Char('/');
    base.setPath(basePath + relativePath);
    base.setQuery(relative.query());
    if (!*sendCookies) {
        QUrlQuery query(base);
        query.addQueryItem(QStringLiteral("compileGroup"),
                           result.value(QStringLiteral("compileGroup")).toString(QStringLiteral("standard")));
        query.addQueryItem(QStringLiteral("clsiserverid"), clsi);
        query.addQueryItem(QStringLiteral("enable_pdf_caching"), QStringLiteral("true"));
        base.setQuery(query);
    }
    *url = base;
    error->clear();
    return true;
}
