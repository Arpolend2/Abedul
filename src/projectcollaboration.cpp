#include "projectcollaboration.h"
#include "overleafhtml.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace {
QString person(const QJsonObject &user)
{
    const QString name = (user.value(QStringLiteral("first_name")).toString() + QLatin1Char(' ')
                          + user.value(QStringLiteral("last_name")).toString()).trimmed();
    if (!name.isEmpty()) return name;
    const QString displayName = user.value(QStringLiteral("name")).toString();
    return displayName.isEmpty() ? user.value(QStringLiteral("email")).toString() : displayName;
}

QString dateString(const QJsonValue &value)
{
    if (value.isDouble())
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(value.toDouble())).toLocalTime().toString(Qt::DefaultLocaleShortDate);
    return value.toString();
}

QString messageText(const QJsonObject &message)
{
    QString header = person(message.value(QStringLiteral("user")).toObject());
    const QString date = dateString(message.value(QStringLiteral("timestamp")));
    if (!date.isEmpty())
        header += (header.isEmpty() ? QString() : QStringLiteral(" · ")) + date;
    return (header.isEmpty() ? QString() : header + QLatin1Char('\n'))
            + message.value(QStringLiteral("content")).toString();
}
}

int CollaborationEntriesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant CollaborationEntriesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()
            || role != Qt::UserRole + 1)
        return QVariant();
    return m_entries.at(index.row());
}

QHash<int, QByteArray> CollaborationEntriesModel::roleNames() const
{
    return {{Qt::UserRole + 1, QByteArrayLiteral("entry")}};
}

void CollaborationEntriesModel::reset(const QVariantList &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

void CollaborationEntriesModel::update(int index, const QVariantMap &entry)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries[index] = entry;
    const QModelIndex changedIndex = this->index(index, 0);
    emit dataChanged(changedIndex, changedIndex, {Qt::UserRole + 1});
}

ProjectCollaboration::ProjectCollaboration(QNetworkAccessManager *network, const QUrl *server,
                                           const QByteArray *cookie, QObject *parent)
    : QObject(parent), m_network(network), m_server(server), m_cookie(cookie)
{
}

bool ProjectCollaboration::validId(const QString &id) const
{
    return QRegularExpression(QStringLiteral("^[a-zA-Z0-9_-]{1,128}$")).match(id).hasMatch();
}

bool ProjectCollaboration::validSession() const
{
    return m_network && m_server && m_cookie && *m_server == m_openServer
            && *m_cookie == m_openCookie && !m_openCookie.isEmpty()
            && m_openServer.scheme() == QStringLiteral("https")
            && !m_openServer.host().isEmpty() && m_openServer.userInfo().isEmpty();
}

void ProjectCollaboration::close()
{
    ++m_generation;
    cancelHistoryDiffs();
    if (m_reply) {
        m_reply->abort();
        m_reply.clear();
    }
    m_busy = false;
    m_project.clear();
    m_entriesModel.reset(QVariantList());
    m_error.clear();
    m_notice.clear();
    emit changed();
}

void ProjectCollaboration::open(const QString &projectId, const QString &mode)
{
    close();
    m_project = projectId;
    m_mode = mode;
    if (m_server) m_openServer = *m_server;
    if (m_cookie) m_openCookie = *m_cookie;
    if (!validId(projectId) || (mode != QStringLiteral("share") && mode != QStringLiteral("history")
            && mode != QStringLiteral("comments") && mode != QStringLiteral("chat")) || !validSession()) {
        fail(tr("Invalid project or session. Sign in again."));
        return;
    }
    refresh();
}

void ProjectCollaboration::fail(const QString &message)
{
    m_busy = false;
    m_error = message;
    emit changed();
}

void ProjectCollaboration::request(const QString &path, const QByteArray &body,
                                   const Handler &handler, const QByteArray &csrf)
{
    if (!validSession()) {
        fail(tr("The session changed. Reopen this page."));
        return;
    }
    const quint64 generation = m_generation;
    QNetworkRequest req(m_openServer.resolved(QUrl(path)));
    req.setRawHeader("Accept", "application/json, text/html");
    req.setRawHeader("Cookie", m_openCookie);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    QNetworkReply *reply;
    if (body.isNull()) {
        reply = m_network->get(req);
    } else {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("X-CSRF-Token", csrf);
        reply = m_network->post(req, body);
    }
    m_reply = reply;
    QTimer::singleShot(30000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, handler, body]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool redirected = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        const bool ok = !redirected && status >= 200 && status < 300
                && reply->error() == QNetworkReply::NoError;
        const QByteArray data = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply.clear();
        if (!validSession()) {
            fail(tr("The session changed. Reopen this page."));
            return;
        }
        if (!ok) {
            QString detail;
            const QJsonObject error = QJsonDocument::fromJson(data).object();
            for (const QString &key : {QStringLiteral("error"), QStringLiteral("errorReason"), QStringLiteral("message")}) {
                if (error.value(key).isString()) { detail = error.value(key).toString().left(300); break; }
            }
            QString msg = status > 0 ? tr("Server request failed (HTTP %1).").arg(status)
                                     : tr("Network request failed: %1").arg(networkError);
            if (redirected) msg = tr("Server redirected the request. Sign in again.");
            if (!detail.isEmpty()) msg += QLatin1Char(' ') + detail;
            if (!body.isNull() && status == 0)
                msg += QLatin1Char(' ') + tr("The server may have received the request; check before retrying.");
            fail(msg);
            return;
        }
        handler(data);
    });
}

void ProjectCollaboration::refresh()
{
    if (m_busy) return;
    if (!validId(m_project) || !validSession()) { fail(tr("The session changed. Reopen this page.")); return; }
    cancelHistoryDiffs();
    m_busy = true; m_error.clear(); m_notice.clear(); emit changed();
    QString endpoint;
    if (m_mode == QStringLiteral("share")) endpoint = QStringLiteral("/members");
    if (m_mode == QStringLiteral("history")) endpoint = QStringLiteral("/updates?min_count=50");
    if (m_mode == QStringLiteral("comments")) endpoint = QStringLiteral("/threads");
    if (m_mode == QStringLiteral("chat")) endpoint = QStringLiteral("/messages?limit=100");
    request(QStringLiteral("/project/") + m_project + endpoint, QByteArray(),
            [this](const QByteArray &data) { parseEntries(data); });
}

void ProjectCollaboration::parseEntries(const QByteArray &data)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(tr("The server returned an unsupported response."));
        return;
    }
    QVariantList result;
    if (m_mode == QStringLiteral("chat") && doc.isArray()) {
        for (const QJsonValue &item : doc.array()) {
            QVariantMap entry; entry[QStringLiteral("body")] = messageText(item.toObject()); result.append(entry);
        }
    } else if (m_mode == QStringLiteral("share") && doc.isObject()
               && doc.object().value(QStringLiteral("members")).isArray()) {
        for (const QJsonValue &item : doc.object().value(QStringLiteral("members")).toArray()) {
            const QJsonObject member = item.toObject();
            QString name = person(member);
            if (name.isEmpty()) name = person(member.value(QStringLiteral("user")).toObject());
            QVariantMap entry;
            QString privilege = member.value(QStringLiteral("privileges")).toString();
            if (privilege.isEmpty())
                privilege = member.value(QStringLiteral("privilegeLevel")).toString();
            const QJsonObject user = member.value(QStringLiteral("user")).toObject();
            QString email = member.value(QStringLiteral("email")).toString();
            if (email.isEmpty()) email = user.value(QStringLiteral("email")).toString();
            entry[QStringLiteral("name")] = name.isEmpty() ? tr("Unknown collaborator") : name;
            entry[QStringLiteral("email")] = email == name ? QString() : email;
            entry[QStringLiteral("access")] = privilege == QStringLiteral("readAndWrite") ? tr("Can edit")
                    : privilege == QStringLiteral("readOnly") ? tr("Can view")
                    : privilege == QStringLiteral("owner") ? tr("Owner") : tr("Access not specified");
            result.append(entry);
        }
    } else if (m_mode == QStringLiteral("history") && doc.isObject()
               && doc.object().value(QStringLiteral("updates")).isArray()) {
        for (const QJsonValue &item : doc.object().value(QStringLiteral("updates")).toArray()) {
            const QJsonObject update = item.toObject();
            const QJsonObject meta = update.value(QStringLiteral("meta")).toObject();
            QStringList users;
            QVariantList changes;
            for (const QJsonValue &path : update.value(QStringLiteral("pathnames")).toArray()) {
                if (path.isString() && !path.toString().isEmpty())
                    changes.append(QVariantMap{{QStringLiteral("action"), tr("Edited")},
                                               {QStringLiteral("path"), path.toString()},
                                               {QStringLiteral("kind"), QStringLiteral("edited")}});
            }
            for (const QJsonValue &value : update.value(QStringLiteral("project_ops")).toArray()) {
                const QJsonObject op = value.toObject();
                for (const QString &kind : {QStringLiteral("add"), QStringLiteral("remove"), QStringLiteral("rename")}) {
                    if (!op.value(kind).isObject())
                        continue;
                    const QJsonObject detail = op.value(kind).toObject();
                    QString path = detail.value(QStringLiteral("pathname")).toString();
                    if (path.isEmpty())
                        continue;
                    if (kind == QStringLiteral("rename")) {
                        const QString newPath = detail.value(QStringLiteral("newPathname")).toString();
                        if (newPath.isEmpty())
                            continue;
                        path += QStringLiteral(" → ") + newPath;
                    }
                    changes.append(QVariantMap{{QStringLiteral("action"),
                        kind == QStringLiteral("add") ? tr("Created") :
                        kind == QStringLiteral("remove") ? tr("Deleted") : tr("Renamed")},
                        {QStringLiteral("path"), path}, {QStringLiteral("kind"), kind}});
                }
            }
            for (const QJsonValue &user : meta.value(QStringLiteral("users")).toArray()) {
                const QString name = person(user.toObject());
                if (!name.isEmpty()) users << name;
            }
            QVariantMap entry;
            entry[QStringLiteral("body")] = dateString(meta.value(QStringLiteral("end_ts"))) + QLatin1Char('\n')
                    + tr("Versions %1–%2").arg(update.value(QStringLiteral("fromV")).toInt()).arg(update.value(QStringLiteral("toV")).toInt())
                    + (users.isEmpty() ? QString() : QLatin1Char('\n') + users.join(QStringLiteral(", ")));
            entry[QStringLiteral("changes")] = changes;
            const QJsonArray pathnames = update.value(QStringLiteral("pathnames")).toArray();
            bool validPaths = true;
            for (const QJsonValue &path : pathnames)
                validPaths = validPaths && path.isString() && !path.toString().isEmpty();
            if (pathnames.size() > 0 && pathnames.size() <= 2
                    && validPaths
                    && update.value(QStringLiteral("project_ops")).toArray().isEmpty()
                    && update.value(QStringLiteral("fromV")).isDouble()
                    && update.value(QStringLiteral("toV")).isDouble()
                    && update.value(QStringLiteral("fromV")).toInt(-1) >= 0
                    && update.value(QStringLiteral("toV")).toInt(-1) >= update.value(QStringLiteral("fromV")).toInt()) {
                for (const QJsonValue &path : pathnames) {
                    if (path.isString() && !path.toString().isEmpty()) {
                        m_historyQueue.append(QVariantMap{
                            {QStringLiteral("index"), result.size()},
                            {QStringLiteral("path"), path.toString()},
                            {QStringLiteral("from"), update.value(QStringLiteral("fromV")).toInt()},
                            {QStringLiteral("to"), update.value(QStringLiteral("toV")).toInt()}});
                    }
                }
                entry[QStringLiteral("previewPending")] = pathnames.size();
                entry[QStringLiteral("previewParts")] = QVariantList();
                entry[QStringLiteral("previewSize")] = 0;
                entry[QStringLiteral("previewLines")] = 0;
            }
            result.append(entry);
        }
    } else if (m_mode == QStringLiteral("comments") && doc.isObject()) {
        const QJsonObject threads = doc.object();
        for (auto it = threads.begin(); it != threads.end(); ++it) {
            if (!it.value().isObject() || !validId(it.key())) continue;
            const QJsonObject thread = it.value().toObject();
            QStringList messages;
            for (const QJsonValue &message : thread.value(QStringLiteral("messages")).toArray())
                messages << messageText(message.toObject());
            QVariantMap entry;
            entry[QStringLiteral("target")] = it.key();
            entry[QStringLiteral("body")] = (thread.value(QStringLiteral("resolved")).toBool() ? tr("Resolved") : tr("Open"))
                    + QStringLiteral(" · ") + it.key() + QLatin1Char('\n') + messages.join(QStringLiteral("\n\n"));
            result.append(entry);
        }
    } else {
        fail(tr("The server returned an unsupported response."));
        return;
    }
    m_entriesModel.reset(result);
    m_busy = false;
    emit changed();
    if (m_mode == QStringLiteral("history"))
        fetchNextHistoryDiff();
}

void ProjectCollaboration::cancelHistoryDiffs()
{
    ++m_historyGeneration;
    m_historyQueue.clear();
    if (m_historyReply)
        m_historyReply->abort();
    m_historyReply.clear();
}

void ProjectCollaboration::fetchNextHistoryDiff()
{
    if (m_historyQueue.isEmpty() || m_historyReply || !validSession())
        return;
    const QVariantMap job = m_historyQueue.takeFirst().toMap();
    const int index = job.value(QStringLiteral("index")).toInt();
    QUrl url = m_openServer.resolved(QUrl(QStringLiteral("/project/") + m_project + QStringLiteral("/diff")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), job.value(QStringLiteral("from")).toString());
    query.addQueryItem(QStringLiteral("to"), job.value(QStringLiteral("to")).toString());
    query.addQueryItem(QStringLiteral("pathname"), job.value(QStringLiteral("path")).toString());
    url.setQuery(query);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Cookie", m_openCookie);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    QNetworkReply *reply = m_network->get(req);
    m_historyReply = reply;
    const quint64 generation = m_historyGeneration;
    QTimer::singleShot(30000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, index, job]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                && !reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        if (generation != m_historyGeneration)
            return;
        m_historyReply.clear();
        if (!validSession() || index < 0 || index >= m_entriesModel.rowCount())
            return;
        QVariantMap entry = m_entriesModel.data(m_entriesModel.index(index, 0), Qt::UserRole + 1).toMap();
        QVariantList parts = entry.value(QStringLiteral("previewParts")).toList();
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        const QJsonValue diff = doc.object().value(QStringLiteral("diff"));
        bool small = !entry.value(QStringLiteral("previewTooLarge")).toBool();
        int size = entry.value(QStringLiteral("previewSize")).toInt();
        int lines = entry.value(QStringLiteral("previewLines")).toInt();
        if (!ok || parseError.error != QJsonParseError::NoError || !doc.isObject()
                || (!diff.isArray() && !diff.isObject())) {
            entry[QStringLiteral("previewError")] = tr("Change preview unavailable");
            small = false;
        } else if (diff.isObject()) {
            small = false;
        } else if (small) {
            QVariantList fileParts;
            for (const QJsonValue &value : diff.toArray()) {
                if (!value.isObject()) { small = false; break; }
                const QJsonObject chunk = value.toObject();
                for (const QString &kind : {QStringLiteral("d"), QStringLiteral("i")}) {
                    if (!chunk.contains(kind))
                        continue;
                    if (!chunk.value(kind).isString()) { small = false; break; }
                    const QString text = chunk.value(kind).toString();
                    if (text.isEmpty()) continue;
                    size += text.size();
                    lines += text.count(QLatin1Char('\n')) + 1;
                    fileParts.append(QVariantMap{{QStringLiteral("kind"), kind},
                                                 {QStringLiteral("text"), text}});
                }
            }
            if (size > 200 || lines > 6)
                small = false;
            if (small && !fileParts.isEmpty()) {
                parts.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("path")},
                                         {QStringLiteral("text"), job.value(QStringLiteral("path"))}});
                parts.append(fileParts);
            }
        }
        entry[QStringLiteral("previewTooLarge")] = !small;
        entry[QStringLiteral("previewParts")] = parts;
        entry[QStringLiteral("previewSize")] = size;
        entry[QStringLiteral("previewLines")] = lines;
        const int pending = entry.value(QStringLiteral("previewPending")).toInt() - 1;
        entry[QStringLiteral("previewPending")] = pending;
        if (pending == 0 && small && !parts.isEmpty())
            entry[QStringLiteral("preview")] = parts;
        m_entriesModel.update(index, entry);
        emit changed();
        fetchNextHistoryDiff();
    });
}

void ProjectCollaboration::submit(const QString &text, const QString &target, bool readOnly)
{
    if (m_busy) return;
    if (!validSession() || !validId(m_project)) { fail(tr("The session changed. Reopen this page.")); return; }
    if (text.trimmed().isEmpty()) { fail(tr("Enter a value first.")); return; }
    if (m_mode != QStringLiteral("share") && m_mode != QStringLiteral("chat") && m_mode != QStringLiteral("comments")) return;
    if (m_mode == QStringLiteral("comments") && !validId(target)) { fail(tr("Select a comment thread first.")); return; }
    m_busy = true; m_error.clear(); m_notice.clear(); emit changed();
    const QString base = QStringLiteral("/project/") + m_project;
    request(base, QByteArray(), [this, base, text, target, readOnly](const QByteArray &html) {
        const QByteArray csrf = extractCsrfToken(QString::fromUtf8(html)).toUtf8();
        if (csrf.isEmpty()) { fail(tr("Could not obtain a CSRF token. Sign in again.")); return; }
        QJsonObject payload;
        QString path;
        if (m_mode == QStringLiteral("share")) {
            path = base + QStringLiteral("/invite");
            payload[QStringLiteral("email")] = text.trimmed();
            payload[QStringLiteral("privileges")] = readOnly ? QStringLiteral("readOnly") : QStringLiteral("readAndWrite");
        } else {
            path = base + (m_mode == QStringLiteral("chat") ? QStringLiteral("/messages")
                    : QStringLiteral("/thread/") + target + QStringLiteral("/messages"));
            payload[QStringLiteral("content")] = text;
        }
        request(path, QJsonDocument(payload).toJson(QJsonDocument::Compact), [this](const QByteArray &data) {
            if (m_mode == QStringLiteral("share")) {
                const QJsonObject response = QJsonDocument::fromJson(data).object();
                if (!response.value(QStringLiteral("invite")).isObject()) {
                    const QString reason = response.value(QStringLiteral("error")).toString();
                    fail(reason.isEmpty() ? tr("Invitation was not created. The server may limit collaborators.")
                                          : tr("Invitation was not created: %1").arg(reason));
                    return;
                }
            }
            m_busy = false;
            m_notice = tr("Sent.");
            emit changed();
            emit submitted();
            refresh();
        }, csrf);
    });
}
