#include "socketprotocol.h"

#include <QCoreApplication>
#include <QJsonValue>
#include <QTextCodec>
#include <QNetworkCookieJar>
#include <QSet>

namespace {
bool validateFolder(const QJsonObject &folder, QSet<QString> *ids, int depth)
{
    const QString id = folder.value(QStringLiteral("_id")).toString();
    if (depth > 100 || id.isEmpty() || ids->contains(id))
        return false;
    ids->insert(id);
    QSet<QString> names;
    for (const QString &kind : {QStringLiteral("docs"), QStringLiteral("fileRefs"),
                                QStringLiteral("folders")}) {
        const QJsonValue children = folder.value(kind);
        if (!children.isArray())
            return false;
        for (const QJsonValue &value : children.toArray()) {
            if (!value.isObject())
                return false;
            const QJsonObject item = value.toObject();
            const QString name = item.value(QStringLiteral("name")).toString();
            const QString childId = item.value(QStringLiteral("_id")).toString();
            if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
                    || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
                    || name.contains(QChar::Null) || names.contains(name) || childId.isEmpty())
                return false;
            names.insert(name);
            if (kind == QStringLiteral("folders")) {
                if (!validateFolder(item, ids, depth + 1))
                    return false;
            } else {
                if (ids->contains(childId))
                    return false;
                ids->insert(childId);
            }
        }
    }
    return true;
}

void appendTreeEntries(const QJsonObject &folder, const QString &prefix, QVariantList *entries)
{
    for (const QString &kind : {QStringLiteral("docs"), QStringLiteral("fileRefs"),
                                QStringLiteral("folders")}) {
        const QString type = kind == QStringLiteral("docs") ? QStringLiteral("doc")
                : kind == QStringLiteral("fileRefs") ? QStringLiteral("file") : QStringLiteral("folder");
        for (const QJsonValue &value : folder.value(kind).toArray()) {
            const QJsonObject item = value.toObject();
            const QString path = prefix + QLatin1Char('/') + item.value(QStringLiteral("name")).toString();
            entries->append(QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("type"), type}});
            if (type == QStringLiteral("folder"))
                appendTreeEntries(item, path, entries);
        }
    }
}

void collectDocuments(const QJsonObject &folder, const QString &prefix, QList<QPair<QString, QString>> *docs)
{
    for (const QJsonValue &value : folder.value(QStringLiteral("docs")).toArray()) {
        const QJsonObject doc = value.toObject();
        docs->append(qMakePair(prefix + QLatin1Char('/') + doc.value(QStringLiteral("name")).toString(),
                               doc.value(QStringLiteral("_id")).toString()));
    }
    for (const QJsonValue &value : folder.value(QStringLiteral("folders")).toArray()) {
        const QJsonObject child = value.toObject();
        collectDocuments(child, prefix + QLatin1Char('/') + child.value(QStringLiteral("name")).toString(), docs);
    }
}

bool visitFolder(const QJsonObject &folder, const QString &prefix, const QString &wanted,
                 const QString &kind, QString *found, int depth)
{
    if (depth > 100)
        return false;
    for (const QJsonValue &value : folder.value(kind).toArray()) {
        const QJsonObject doc = value.toObject();
        const QString name = doc.value(QStringLiteral("name")).toString();
        if (prefix + name == wanted && !doc.value(QStringLiteral("_id")).toString().isEmpty()) {
            if (!found->isEmpty())
                return false;
            *found = doc.value(QStringLiteral("_id")).toString();
        }
    }
    for (const QJsonValue &value : folder.value(QStringLiteral("folders")).toArray()) {
        if (!value.isObject())
            return false;
        const QJsonObject child = value.toObject();
        const QString name = child.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || name.contains(QLatin1Char('/')) || name == QStringLiteral(".."))
            return false;
        if (!visitFolder(child, prefix + name + QLatin1Char('/'), wanted, kind, found, depth + 1))
            return false;
    }
    return true;
}
}

bool validProjectFileTree(const QJsonObject &project)
{
    const QJsonArray roots = project.value(QStringLiteral("rootFolder")).toArray();
    QSet<QString> ids;
    return roots.size() == 1 && roots.at(0).isObject()
            && validateFolder(roots.at(0).toObject(), &ids, 0);
}

QVariantList projectFileTreeEntries(const QJsonObject &project)
{
    QVariantList entries;
    if (validProjectFileTree(project))
        appendTreeEntries(project.value(QStringLiteral("rootFolder")).toArray().at(0).toObject(),
                          QString(), &entries);
    return entries;
}

QByteArray socketCookieHeader(const QByteArray &browserCookies, const QList<QNetworkCookie> &responseCookies,
                              const QUrl &handshakeUrl, const QUrl &socketUrl,
                              const QNetworkCookieJar *networkCookies)
{
    QUrl cookieUrl = socketUrl;
    if (cookieUrl.scheme() == QStringLiteral("wss"))
        cookieUrl.setScheme(QStringLiteral("https"));
    if (cookieUrl.scheme() != handshakeUrl.scheme() || cookieUrl.host() != handshakeUrl.host()
            || cookieUrl.port(443) != handshakeUrl.port(443))
        return QByteArray();
    QNetworkCookieJar jar;
    if (networkCookies)
        jar.setCookiesFromUrl(networkCookies->cookiesForUrl(cookieUrl), handshakeUrl);
    jar.setCookiesFromUrl(responseCookies, handshakeUrl);
    QList<QByteArray> values = browserCookies.split(';');
    for (const QNetworkCookie &cookie : jar.cookiesForUrl(cookieUrl)) {
        const QByteArray name = cookie.name();
        for (int i = values.size() - 1; i >= 0; --i) {
            const QByteArray part = values.at(i).trimmed();
            if (part.left(part.indexOf('=')) == name)
                values.removeAt(i);
        }
        values.append(cookie.toRawForm(QNetworkCookie::NameAndValueOnly));
    }
    QByteArray result;
    for (const QByteArray &value : values) {
        if (value.trimmed().isEmpty())
            continue;
        if (!result.isEmpty())
            result += "; ";
        result += value.trimmed();
    }
    return result;
}

static bool findProjectEntity(const QJsonObject &project, const QString &path, const QString &kind,
                              QString *id, QString *error)
{
    id->clear();
    QString wanted = path;
    if (wanted.startsWith(QLatin1Char('/')))
        wanted.remove(0, 1);
    const QJsonArray roots = project.value(QStringLiteral("rootFolder")).toArray();
    if (wanted.isEmpty() || roots.size() != 1 || !roots.at(0).isObject()
            || !visitFolder(roots.at(0).toObject(), QString(), wanted, kind, id, 0) || id->isEmpty()) {
        id->clear();
        *error = QCoreApplication::translate("SocketProtocol", "Could not find the selected file in the project.");
        return false;
    }
    error->clear();
    return true;
}

bool findProjectDoc(const QJsonObject &project, const QString &path, QString *id, QString *error)
{
    return findProjectEntity(project, path, QStringLiteral("docs"), id, error);
}

bool findProjectMainDoc(const QJsonObject &project, QString *path, QString *id, QString *error)
{
    path->clear();
    id->clear();
    if (!validProjectFileTree(project)) {
        *error = QCoreApplication::translate("SocketProtocol", "The server returned an invalid project file tree.");
        return false;
    }
    QList<QPair<QString, QString>> docs;
    collectDocuments(project.value(QStringLiteral("rootFolder")).toArray().at(0).toObject(), QString(), &docs);
    const QJsonValue root = project.value(QStringLiteral("rootDoc_id"));
    if (!root.isUndefined() && !root.isNull() && !root.isString()) {
        *error = QCoreApplication::translate("SocketProtocol", "The server returned an invalid main document.");
        return false;
    }
    const QString rootId = root.toString();
    if (!rootId.isEmpty()) {
        for (const auto &doc : docs) {
            if (doc.second == rootId) {
                *path = doc.first;
                *id = doc.second;
                error->clear();
                return true;
            }
        }
        *error = QCoreApplication::translate("SocketProtocol", "The project's main document was not found. Choose a file.");
        return false;
    }
    QList<QPair<QString, QString>> tex;
    for (const auto &doc : docs) {
        if (doc.first == QStringLiteral("/main.tex")) {
            *path = doc.first;
            *id = doc.second;
            error->clear();
            return true;
        }
        if (doc.first.endsWith(QStringLiteral(".tex"), Qt::CaseInsensitive))
            tex.append(doc);
    }
    if (tex.size() == 1) {
        *path = tex.first().first;
        *id = tex.first().second;
        error->clear();
        return true;
    }
    *error = QCoreApplication::translate("SocketProtocol", "Could not determine the main document. Choose a file.");
    return false;
}

bool findProjectFile(const QJsonObject &project, const QString &path, QString *id, QString *error)
{
    return findProjectEntity(project, path, QStringLiteral("fileRefs"), id, error);
}

bool findProjectFolder(const QJsonObject &project, const QString &path, QString *id, QString *error)
{
    const QJsonArray roots = project.value(QStringLiteral("rootFolder")).toArray();
    if (path == QStringLiteral("/") || path.isEmpty()) {
        *id = roots.size() == 1 && roots.at(0).isObject()
                ? roots.at(0).toObject().value(QStringLiteral("_id")).toString() : QString();
        if (!id->isEmpty()) {
            error->clear();
            return true;
        }
    } else {
        return findProjectEntity(project, path, QStringLiteral("folders"), id, error);
    }
    *error = QCoreApplication::translate("SocketProtocol", "Could not find the selected file in the project.");
    return false;
}

QJsonArray makeTextOperation(const QString &before, const QString &after)
{
    int prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before.at(prefix) == after.at(prefix))
        ++prefix;
    if (prefix > 0 && ((prefix < before.size() && before.at(prefix).isLowSurrogate())
                       || (prefix < after.size() && after.at(prefix).isLowSurrogate())))
        --prefix;
    int suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before.at(before.size() - suffix - 1) == after.at(after.size() - suffix - 1))
        ++suffix;
    if (suffix > 0 && (before.at(before.size() - suffix).isLowSurrogate()
                       || after.at(after.size() - suffix).isLowSurrogate()))
        --suffix;
    QJsonArray operation;
    const QString removed = before.mid(prefix, before.size() - prefix - suffix);
    const QString inserted = after.mid(prefix, after.size() - prefix - suffix);
    if (!removed.isEmpty())
        operation.append(QJsonObject{{QStringLiteral("p"), prefix}, {QStringLiteral("d"), removed}});
    if (!inserted.isEmpty())
        operation.append(QJsonObject{{QStringLiteral("p"), prefix}, {QStringLiteral("i"), inserted}});
    return operation;
}

bool decodeJoinedDoc(const QJsonArray &args, QString *text, QString *error, qint64 *version)
{
    if (args.size() < 3 || !args.at(0).isNull() || !args.at(1).isArray()
            || !args.at(2).isDouble()
            || args.at(2).toDouble() < 0
            || args.at(2).toDouble() > 9007199254740991.0
            || args.at(2).toDouble() != static_cast<qint64>(args.at(2).toDouble())
            || (args.size() > 5 && !args.at(5).isNull()
                && args.at(5).toString() != QStringLiteral("sharejs-text-ot"))) {
        *error = QCoreApplication::translate("SocketProtocol", "The server could not open this document or uses an unsupported format.");
        return false;
    }
    QStringList lines;
    for (const QJsonValue &line : args.at(1).toArray()) {
        if (!line.isString()) {
            *error = QCoreApplication::translate("SocketProtocol", "Invalid document returned by server.");
            return false;
        }
        const QString encoded = line.toString();
        const QByteArray bytes = encoded.toLatin1();
        QTextCodec::ConverterState state;
        const QString decoded = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
        if (state.invalidChars || state.remainingChars || QString::fromLatin1(bytes) != encoded) {
            *error = QCoreApplication::translate("SocketProtocol", "Invalid text encoding in server document.");
            return false;
        }
        lines.append(decoded);
    }
    QString result = lines.join(QLatin1Char('\n'));
    if (args.size() > 3 && !args.at(3).isNull()) {
        if (!args.at(3).isArray()) {
            *error = QCoreApplication::translate("SocketProtocol", "Invalid document updates from server.");
            return false;
        }
        for (const QJsonValue &updateValue : args.at(3).toArray()) {
            const QJsonObject update = updateValue.toObject();
            if (!updateValue.isObject() || !update.value(QStringLiteral("op")).isArray()) {
                *error = QCoreApplication::translate("SocketProtocol", "Invalid document update from server.");
                return false;
            }
            for (const QJsonValue &opValue : update.value(QStringLiteral("op")).toArray()) {
                const QJsonObject op = opValue.toObject();
                if (!opValue.isObject() || !op.value(QStringLiteral("p")).isDouble()) {
                    *error = QCoreApplication::translate("SocketProtocol", "Unsupported document update from server.");
                    return false;
                }
                const double position = op.value(QStringLiteral("p")).toDouble();
                if (position < 0 || position > result.size() || position != static_cast<int>(position)) {
                    *error = QCoreApplication::translate("SocketProtocol", "Invalid document update position.");
                    return false;
                }
                const int p = static_cast<int>(position);
                if (op.value(QStringLiteral("i")).isString()) {
                    result.insert(p, op.value(QStringLiteral("i")).toString());
                } else if (op.value(QStringLiteral("d")).isString()) {
                    const QString removed = op.value(QStringLiteral("d")).toString();
                    if (result.mid(p, removed.size()) != removed) {
                        *error = QCoreApplication::translate("SocketProtocol", "Document updates do not match the snapshot.");
                        return false;
                    }
                    result.remove(p, removed.size());
                } else if (!op.value(QStringLiteral("c")).isString()) {
                    *error = QCoreApplication::translate("SocketProtocol", "Unsupported document update from server.");
                    return false;
                }
            }
        }
    }
    *text = result;
    if (version) {
        const qint64 initialVersion = static_cast<qint64>(args.at(2).toDouble());
        const int updateCount = args.size() > 3 && args.at(3).isArray() ? args.at(3).toArray().size() : 0;
        if (initialVersion > 9007199254740991LL - updateCount) {
            *error = QCoreApplication::translate("SocketProtocol", "Invalid document version.");
            return false;
        }
        *version = initialVersion + updateCount;
    }
    error->clear();
    return true;
}
