#include "synctex.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QtMath>

bool syncTeXBuildFromCompile(const QByteArray &response, const QString &editorId,
                             SyncTeXBuild *build, QString *error)
{
    *build = SyncTeXBuild();
    const QJsonDocument document = QJsonDocument::fromJson(response);
    if (!document.isObject() || !document.object().value(QStringLiteral("outputFiles")).isArray()) {
        *error = QStringLiteral("The compile response has no SyncTeX build information.");
        return false;
    }
    const QJsonObject result = document.object();
    for (const QJsonValue &value : result.value(QStringLiteral("outputFiles")).toArray()) {
        const QJsonObject file = value.toObject();
        if (file.value(QStringLiteral("path")).toString() == QStringLiteral("output.pdf")
                    || file.value(QStringLiteral("type")).toString() == QStringLiteral("pdf")) {
            build->buildId = file.value(QStringLiteral("build")).toString();
            break;
        }
    }
    static const QRegularExpression buildPattern(QStringLiteral("^[a-f0-9]+-[a-f0-9]+$"));
    static const QRegularExpression editorPattern(
        QStringLiteral("^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$"));
    if (!buildPattern.match(build->buildId).hasMatch() || !editorPattern.match(editorId).hasMatch()) {
        *build = SyncTeXBuild();
        *error = QStringLiteral("The server did not provide a usable SyncTeX build identifier.");
        return false;
    }
    build->editorId = editorId;
    build->clsiServerId = result.value(QStringLiteral("clsiServerId")).toString();
    error->clear();
    return true;
}

bool syncTeXResult(const QByteArray &response, bool toPdf, QVariantList *positions, QString *error)
{
    positions->clear();
    const QJsonDocument document = QJsonDocument::fromJson(response);
    const QJsonValue value = document.object().value(toPdf ? QStringLiteral("pdf") : QStringLiteral("code"));
    if (!document.isObject() || !value.isArray() || value.toArray().isEmpty()) {
        *error = QStringLiteral("SyncTeX did not find a matching location.");
        return false;
    }
    QVariantList parsed;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isObject()) {
            *error = QStringLiteral("The server returned an invalid SyncTeX location.");
            return false;
        }
        const QJsonObject position = entry.toObject();
        if (toPdf) {
            const QJsonValue page = position.value(QStringLiteral("page"));
            const QJsonValue h = position.value(QStringLiteral("h"));
            const QJsonValue v = position.value(QStringLiteral("v"));
            if (!page.isDouble() || page.toDouble() < 1 || page.toDouble() != page.toInt()
                    || !h.isDouble() || !v.isDouble() || !qIsFinite(h.toDouble()) || !qIsFinite(v.toDouble())) {
                *error = QStringLiteral("The server returned an invalid PDF location.");
                return false;
            }
        } else {
            const QJsonValue file = position.value(QStringLiteral("file"));
            const QJsonValue line = position.value(QStringLiteral("line"));
            const QJsonValue column = position.value(QStringLiteral("column"));
            if (!file.isString() || file.toString().isEmpty() || !line.isDouble()
                    || line.toDouble() < 1 || line.toDouble() != line.toInt()
                    || (!column.isUndefined() && (!column.isDouble()
                        || column.toDouble() < -1 || column.toDouble() != column.toInt()))) {
                *error = QStringLiteral("The server returned an invalid source location.");
                return false;
            }
        }
        parsed.append(position.toVariantMap());
    }
    *positions = parsed;
    error->clear();
    return true;
}

static QUrl syncUrl(const QUrl &server, const QString &projectId, const SyncTeXBuild &build,
                    const QString &direction, QUrlQuery query)
{
    QUrl url = server.resolved(QUrl(QStringLiteral("/project/")
                    + QString::fromLatin1(QUrl::toPercentEncoding(projectId))
                    + QStringLiteral("/sync/") + direction));
    query.addQueryItem(QStringLiteral("buildId"), build.buildId);
    query.addQueryItem(QStringLiteral("editorId"), build.editorId);
    if (!build.clsiServerId.isEmpty())
        query.addQueryItem(QStringLiteral("clsiserverid"),
                           QString::fromLatin1(QUrl::toPercentEncoding(build.clsiServerId)));
    url.setQuery(query);
    return url;
}

QUrl syncTeXUrl(const QUrl &server, const QString &projectId, const SyncTeXBuild &build,
                const QString &file, int line, int column)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file"), QString::fromLatin1(QUrl::toPercentEncoding(file)));
    query.addQueryItem(QStringLiteral("line"), QString::number(line));
    query.addQueryItem(QStringLiteral("column"), QString::number(column));
    return syncUrl(server, projectId, build, QStringLiteral("code"), query);
}

QUrl syncTeXUrl(const QUrl &server, const QString &projectId, const SyncTeXBuild &build,
                int page, double h, double v)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("h"), QString::number(h, 'f', 2));
    query.addQueryItem(QStringLiteral("v"), QString::number(v, 'f', 2));
    return syncUrl(server, projectId, build, QStringLiteral("pdf"), query);
}
