#include "overleafclient.h"
#include "overleafhtml.h"
#include "projectlist.h"
#include "compileoutput.h"
#include "socketprotocol.h"
#include "realtimesocket.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QNetworkRequest>
#include <QVariantMap>
#include <QTimer>
#include <QDateTime>
#include <QSettings>
#include <QUrlQuery>
#include <QRegularExpression>

OverleafClient::OverleafClient(QObject *parent) : QObject(parent)
{
    QSettings settings;
    m_pdfOnLeft = settings.value(QStringLiteral("pdfOnLeft"), false).toBool();
    m_editorFontScale = qBound(70, settings.value(QStringLiteral("editorFontScale"), 100).toInt(), 160);
    m_rememberedServers = settings.value(QStringLiteral("servers")).toStringList();
    m_rememberedServer = settings.value(QStringLiteral("lastServer")).toString();
    if (!m_rememberedServer.isEmpty() && !m_rememberedServers.contains(m_rememberedServer))
        m_rememberedServers.prepend(m_rememberedServer);
    if (m_rememberedServer.isEmpty() && !m_rememberedServers.isEmpty())
        m_rememberedServer = m_rememberedServers.first();
}
QString OverleafClient::server() const { return m_server.toString(); }
QString OverleafClient::rememberedServer() const { return m_rememberedServer; }
QStringList OverleafClient::rememberedServers() const { return m_rememberedServers; }
QString OverleafClient::error() const { return m_error; }
bool OverleafClient::busy() const { return m_busy; }
bool OverleafClient::authenticated() const { return m_authenticated; }
QVariantList OverleafClient::projects() const { return m_projects; }
QString OverleafClient::pdfPath() const { return m_pdfPath; }
QString OverleafClient::pdfError() const { return m_pdfError; }
bool OverleafClient::pdfBusy() const { return m_pdfBusy; }
QVariantList OverleafClient::projectFiles() const { return m_projectFiles; }
QString OverleafClient::filesError() const { return m_filesError; }
bool OverleafClient::filesBusy() const { return m_filesBusy; }
QString OverleafClient::sourceText() const { return m_sourceText; }
QString OverleafClient::sourcePath() const { return m_sourcePath; }
QString OverleafClient::sourceError() const { return m_sourceError; }
bool OverleafClient::sourceBusy() const { return m_sourceBusy; }
bool OverleafClient::pdfOnLeft() const { return m_pdfOnLeft; }
int OverleafClient::editorFontScale() const { return m_editorFontScale; }

void OverleafClient::setEditorFontScale(int percent)
{
    percent = qBound(70, percent, 160);
    if (m_editorFontScale == percent)
        return;
    m_editorFontScale = percent;
    QSettings settings;
    settings.setValue(QStringLiteral("editorFontScale"), percent);
    emit editorFontScaleChanged();
}

void OverleafClient::setPdfOnLeft(bool enabled)
{
    if (m_pdfOnLeft == enabled)
        return;
    m_pdfOnLeft = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("pdfOnLeft"), enabled);
    emit pdfOnLeftChanged();
}

void OverleafClient::closeProjectSource()
{
    ++m_sourceGeneration;
    if (m_sourceReply)
        m_sourceReply->abort();
    m_sourceReply.clear();
    if (m_sourceSocket) {
        m_sourceSocket->abort();
        m_sourceSocket->deleteLater();
        m_sourceSocket.clear();
    }
    m_sourceBusy = false;
    m_sourceError.clear();
    m_sourcePath.clear();
    m_sourceText.clear();
    m_savingSource = false;
    m_saveSubmitted = false;
    m_saveConcurrent = false;
    m_savePath.clear();
    m_saveBase.clear();
    m_saveText.clear();
    m_saveDocId.clear();
    emit sourceBusyChanged();
    emit sourceErrorChanged();
    emit sourceTextChanged();
}

void OverleafClient::sourceFailed(const QString &message)
{
    const bool uncertain = m_saveSubmitted;
    closeProjectSource();
    m_sourceError = uncertain
            ? message + tr(" The server may have saved the changes; the local draft was kept.")
            : message;
    emit sourceErrorChanged();
}

void OverleafClient::openProjectSource(const QString &projectId, const QString &path)
{
    startProjectSource(projectId, path, false, QString(), false, QString());
}

void OverleafClient::saveProjectSource(const QString &projectId, const QString &path,
                                       const QString &base, bool baseKnown, const QString &text)
{
    startProjectSource(projectId, path, true, base, baseKnown, text);
}

void OverleafClient::completeSourceSave()
{
    const QString path = m_savePath;
    const QString text = m_saveText;
    closeProjectSource();
    emit sourceSaved(path, text);
}

void OverleafClient::startProjectSource(const QString &projectId, const QString &path, bool saving,
                                        const QString &base, bool baseKnown, const QString &text)
{
    if (m_sourceBusy)
        return;
    bool found = false;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            found = true;
            break;
        }
    }
    if (!m_authenticated || !found || path.isEmpty()) {
        m_sourceError = tr("This project document is not available in the current session.");
        emit sourceErrorChanged();
        return;
    }
    closeProjectSource();
    m_savingSource = saving;
    m_savePath = path;
    m_saveBase = base;
    m_saveBaseKnown = baseKnown;
    m_saveText = text;
    m_sourceBusy = true;
    emit sourceBusyChanged();
    const quint64 generation = m_sourceGeneration;
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/project/") + id));
    m_sourceReply = reply;
    QTimer::singleShot(30000, this, [this, generation]() {
        if (generation == m_sourceGeneration && m_sourceBusy)
            sourceFailed(tr("Timed out connecting to the project."));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectId, path]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString html = QString::fromUtf8(reply->readAll());
        const QString networkError = reply->errorString();
        reply->deleteLater();
        if (generation != m_sourceGeneration)
            return;
        m_sourceReply.clear();
        if (!ok) {
            sourceFailed(tr("Could not open the project (HTTP %1): %2").arg(status).arg(networkError));
            return;
        }
        const QString csrf = extractCsrfToken(html);
        if (!csrf.isEmpty())
            m_csrf = csrf.toUtf8();
        const QString wsValue = extractMetaContent(html, QStringLiteral("ol-wsUrl"));
        QUrl endpoint = m_server.resolved(QUrl(wsValue.isEmpty() ? QStringLiteral("/socket.io") : wsValue));
        if (endpoint.scheme() == QStringLiteral("wss"))
            endpoint.setScheme(QStringLiteral("https"));
        if (!endpoint.isValid() || endpoint.scheme() != QStringLiteral("https")
                || endpoint.host() != m_server.host() || endpoint.port(443) != m_server.port(443)
                || !endpoint.userInfo().isEmpty() || endpoint.hasQuery() || endpoint.hasFragment()) {
            sourceFailed(tr("The project uses an unsupported or cross-origin real-time server."));
            return;
        }
        QString resource = endpoint.path();
        if (resource.isEmpty() || resource == QStringLiteral("/"))
            resource = QStringLiteral("/socket.io");
        while (resource.endsWith(QLatin1Char('/')))
            resource.chop(1);
        endpoint.setPath(resource + QStringLiteral("/1/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("projectId"), projectId);
        query.addQueryItem(QStringLiteral("t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
        endpoint.setQuery(query);
        QNetworkRequest handshake(endpoint);
        handshake.setRawHeader("Cookie", m_cookieHeader);
        handshake.setRawHeader("Origin", m_server.toString().toUtf8());
        if (!m_csrf.isEmpty())
            handshake.setRawHeader("X-CSRF-Token", m_csrf);
        QNetworkReply *handshakeReply = m_network.get(handshake);
        m_sourceReply = handshakeReply;
        connect(handshakeReply, &QNetworkReply::finished, this,
                [this, handshakeReply, generation, endpoint, resource, projectId, path]() {
            const int status = handshakeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = handshakeReply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
            const QString handshake = QString::fromUtf8(handshakeReply->readAll()).trimmed();
            const QList<QNetworkCookie> responseCookies =
                    qvariant_cast<QList<QNetworkCookie>>(handshakeReply->header(QNetworkRequest::SetCookieHeader));
            const QString networkError = handshakeReply->errorString();
            handshakeReply->deleteLater();
            if (generation != m_sourceGeneration)
                return;
            m_sourceReply.clear();
            const QStringList parts = handshake.split(QLatin1Char(':'));
            if (!ok) {
                sourceFailed(tr("Real-time handshake failed (HTTP %1): %2").arg(status).arg(networkError));
                return;
            }
            if (parts.size() != 4
                    || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(parts.at(0)).hasMatch()
                    || !parts.at(3).split(QLatin1Char(',')).contains(QStringLiteral("websocket"))) {
                sourceFailed(tr("Unsupported real-time handshake from the server."));
                return;
            }
            QUrl socketUrl = endpoint;
            socketUrl.setScheme(QStringLiteral("wss"));
            socketUrl.setPath(resource + QStringLiteral("/1/websocket/") + parts.at(0));
            QUrlQuery socketQuery;
            socketQuery.addQueryItem(QStringLiteral("projectId"), projectId);
            socketUrl.setQuery(socketQuery);
            RealtimeSocket *socket = new RealtimeSocket(this);
            m_sourceSocket = socket;
            connect(socket, &RealtimeSocket::failed, this, [this, generation](const QString &error) {
                if (generation == m_sourceGeneration && m_sourceBusy)
                    sourceFailed(tr("Real-time connection failed: %1").arg(error));
            });
            connect(socket, &RealtimeSocket::textMessageReceived, this,
                    [this, socket, generation, path](const QString &message) {
                if (generation != m_sourceGeneration || !m_sourceBusy)
                    return;
                if (message == QStringLiteral("2::")) {
                    socket->sendText(message);
                    return;
                }
                if (message.startsWith(QStringLiteral("5:::"))) {
                    QJsonParseError jsonError;
                    const QJsonObject event = QJsonDocument::fromJson(message.mid(4).toUtf8(), &jsonError).object();
                    if (jsonError.error != QJsonParseError::NoError) {
                        sourceFailed(tr("Invalid real-time message from server."));
                        return;
                    }
                    const QString eventName = event.value(QStringLiteral("name")).toString();
                    if (eventName == QStringLiteral("connectionRejected")) {
                        sourceFailed(tr("The server rejected the project connection."));
                        return;
                    }
                    if (m_savingSource && eventName == QStringLiteral("otUpdateApplied")) {
                        const QJsonArray updateArgs = event.value(QStringLiteral("args")).toArray();
                        const QJsonObject update = updateArgs.isEmpty() ? QJsonObject() : updateArgs.at(0).toObject();
                        if (!m_saveDocId.isEmpty() && update.value(QStringLiteral("doc")).toString() == m_saveDocId) {
                            if (!m_saveSubmitted) {
                                m_saveConcurrent = true;
                            } else if (update.value(QStringLiteral("v")).isDouble()
                                       && update.value(QStringLiteral("v")).toDouble() == static_cast<double>(m_saveVersion)
                                       && !update.contains(QStringLiteral("op"))) {
                                completeSourceSave();
                            } else {
                                sourceFailed(tr("The document changed on the server while saving."));
                            }
                        }
                        return;
                    }
                    if (m_savingSource && eventName == QStringLiteral("otUpdateError")) {
                        sourceFailed(tr("The server rejected the document update."));
                        return;
                    }
                    if (eventName != QStringLiteral("joinProjectResponse"))
                        return;
                    const QJsonArray args = event.value(QStringLiteral("args")).toArray();
                    const QJsonObject response = args.isEmpty() ? QJsonObject() : args.at(0).toObject();
                    QString docId;
                    QString error;
                    if (response.value(QStringLiteral("protocolVersion")).toInt() != 2
                            || !findProjectDoc(response.value(QStringLiteral("project")).toObject(), path, &docId, &error)) {
                        sourceFailed(error.isEmpty() ? tr("Unsupported real-time project protocol.") : error);
                        return;
                    }
                    if (m_savingSource)
                        m_saveDocId = docId;
                    const QJsonArray joinArgs{docId, QJsonObject{
                        {QStringLiteral("encodeRanges"), true},
                        {QStringLiteral("supportsHistoryOT"), false}
                    }};
                    const QJsonObject eventObject{{QStringLiteral("name"), QStringLiteral("joinDoc")},
                                                  {QStringLiteral("args"), joinArgs}};
                    socket->sendText(QStringLiteral("5:1+::")
                                            + QString::fromUtf8(QJsonDocument(eventObject).toJson(QJsonDocument::Compact)));
                } else if (message.startsWith(QStringLiteral("6:::1+"))) {
                    QJsonParseError jsonError;
                    const QJsonDocument doc = QJsonDocument::fromJson(message.mid(6).toUtf8(), &jsonError);
                    QString text;
                    QString error;
                    qint64 version = 0;
                    if (jsonError.error != QJsonParseError::NoError || !doc.isArray()
                            || !decodeJoinedDoc(doc.array(), &text, &error, &version)) {
                        sourceFailed(error.isEmpty() ? tr("Invalid document response from server.") : error);
                        return;
                    }
                    if (m_savingSource) {
                        if (m_saveConcurrent) {
                            sourceFailed(tr("The document changed on the server while opening it. Try again."));
                            return;
                        }
                        if (text == m_saveText) {
                            completeSourceSave();
                            return;
                        }
                        if (!m_saveBaseKnown || text != m_saveBase) {
                            sourceFailed(tr("The server document differs from the original local draft. "
                                            "The local draft was kept; resolve the conflict before saving."));
                            return;
                        }
                        m_saveVersion = version;
                        const QJsonObject update{{QStringLiteral("doc"), m_saveDocId},
                                                 {QStringLiteral("v"), static_cast<double>(version)},
                                                 {QStringLiteral("op"), makeTextOperation(text, m_saveText)}};
                        const QJsonObject requestObject{{QStringLiteral("name"), QStringLiteral("applyOtUpdate")},
                                                        {QStringLiteral("args"), QJsonArray{m_saveDocId, update}}};
                        m_saveSubmitted = true;
                        socket->sendText(QStringLiteral("5:2+::")
                                         + QString::fromUtf8(QJsonDocument(requestObject).toJson(QJsonDocument::Compact)));
                        return;
                    }
                    m_sourcePath = path;
                    m_sourceText = text;
                    m_sourceBusy = false;
                    emit sourceBusyChanged();
                    emit sourceTextChanged();
                    socket->abort();
                    socket->deleteLater();
                    m_sourceSocket.clear();
                } else if (m_savingSource && message.startsWith(QStringLiteral("6:::2+"))) {
                    QJsonParseError jsonError;
                    const QJsonDocument response = QJsonDocument::fromJson(message.mid(6).toUtf8(), &jsonError);
                    if (jsonError.error != QJsonParseError::NoError || !response.isArray()
                            || (!response.array().isEmpty() && !response.array().at(0).isNull()))
                        sourceFailed(tr("The server rejected the document update."));
                } else if (message.startsWith(QStringLiteral("0::")) || message.startsWith(QStringLiteral("7::"))) {
                    sourceFailed(tr("The server rejected the real-time connection."));
                }
            });
            socket->open(socketUrl, socketCookieHeader(m_cookieHeader, responseCookies, endpoint, socketUrl,
                                                       m_network.cookieJar()),
                         m_server.toString().toUtf8());
        });
    });
}

void OverleafClient::closeProjectFiles()
{
    ++m_filesGeneration;
    if (m_filesReply)
        m_filesReply->abort();
    m_filesReply.clear();
    m_projectFiles.clear();
    m_filesError.clear();
    m_filesBusy = false;
    emit projectFilesChanged();
    emit filesErrorChanged();
    emit filesBusyChanged();
}

void OverleafClient::loadProjectFiles(const QString &projectId)
{
    if (m_filesBusy)
        return;
    bool found = false;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            found = true;
            break;
        }
    }
    if (!m_authenticated || !found) {
        m_filesError = tr("This project is not available in the current session.");
        emit filesErrorChanged();
        return;
    }
    closeProjectFiles();
    m_filesBusy = true;
    emit filesBusyChanged();
    const quint64 generation = m_filesGeneration;
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/project/") + id
                                                + QStringLiteral("/entities")));
    m_filesReply = reply;
    QTimer::singleShot(60000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QByteArray body = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();
        if (generation != m_filesGeneration)
            return;
        m_filesReply.clear();
        m_filesBusy = false;
        emit filesBusyChanged();
        if (!ok) {
            m_filesError = tr("Could not load project files (HTTP %1): %2").arg(status).arg(networkError);
        } else {
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
            const QJsonValue entities = doc.object().value(QStringLiteral("entities"));
            if (error.error != QJsonParseError::NoError || !doc.isObject() || !entities.isArray()) {
                m_filesError = tr("Invalid project file list from server.");
            } else {
                for (const QJsonValue &value : entities.toArray()) {
                    const QJsonObject item = value.toObject();
                    if (!value.isObject() || !item.value(QStringLiteral("path")).isString()
                            || !item.value(QStringLiteral("type")).isString()) {
                        m_projectFiles.clear();
                        m_filesError = tr("Invalid project file list from server.");
                        break;
                    }
                    m_projectFiles.append(item.toVariantMap());
                }
                emit projectFilesChanged();
            }
        }
        emit filesErrorChanged();
    });
}

void OverleafClient::setPdfError(const QString &message)
{
    if (m_pdfError != message) {
        m_pdfError = message;
        emit pdfErrorChanged();
    }
}

void OverleafClient::setPdfBusy(bool busy)
{
    if (m_pdfBusy != busy) {
        m_pdfBusy = busy;
        emit pdfBusyChanged();
    }
}

void OverleafClient::closeProjectPdf()
{
    ++m_pdfGeneration;
    if (m_pdfReply)
        m_pdfReply->abort();
    m_pdfReply.clear();
    if (!m_pdfPath.isEmpty()) {
        m_pdfPath.clear();
        emit pdfPathChanged();
    }
    m_pdfFile.reset();
    setPdfBusy(false);
    setPdfError(QString());
}

void OverleafClient::compileProjectPdf(const QString &projectId, const QString &rootPath)
{
    if (m_pdfBusy)
        return;
    bool found = false;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            found = true;
            break;
        }
    }
    if (!m_authenticated || !found) {
        setPdfError(tr("This project is not available in the current session."));
        return;
    }
    QByteArray payload;
    QString payloadError;
    if (!compileRequestBody(rootPath, &payload, &payloadError)) {
        setPdfError(payloadError);
        return;
    }
    closeProjectPdf();
    const quint64 generation = m_pdfGeneration;
    setPdfBusy(true);
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    const QString projectPath = QStringLiteral("/project/") + id;
    QNetworkReply *reply = m_network.get(request(projectPath));
    m_pdfReply = reply;
    QTimer::singleShot(60000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectPath, payload]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QByteArray html = reply->readAll();
        reply->deleteLater();
        if (generation != m_pdfGeneration)
            return;
        if (!ok) {
            setPdfError(tr("Could not open project for compilation (HTTP %1): %2")
                        .arg(status).arg(reply->errorString()));
            setPdfBusy(false);
            return;
        }
        const QString token = extractCsrfToken(QString::fromUtf8(html));
        if (token.isEmpty()) {
            setPdfError(tr("Could not obtain the project security token."));
            setPdfBusy(false);
            return;
        }
        m_csrf = token.toUtf8();
        QNetworkRequest req = request(projectPath + QStringLiteral("/compile"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *compileReply = m_network.post(req, payload);
        m_pdfReply = compileReply;
        QTimer::singleShot(120000, compileReply, [compileReply]() {
            if (compileReply->isRunning()) compileReply->abort();
        });
        connect(compileReply, &QNetworkReply::finished, this, [this, compileReply, generation]() {
            const int status = compileReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = compileReply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
            const QByteArray response = compileReply->readAll();
            const QString error = compileReply->errorString();
            compileReply->deleteLater();
            if (generation != m_pdfGeneration)
                return;
            if (!ok) {
                const QString detail = compileServerError(response);
                setPdfError(tr("Compilation request failed (HTTP %1): %2")
                            .arg(status).arg(detail.isEmpty() ? error : detail));
                setPdfBusy(false);
                return;
            }
            downloadProjectPdf(response, generation);
        });
    });
}

void OverleafClient::downloadProjectPdf(const QByteArray &response, quint64 generation)
{
    QUrl url;
    bool sendCookies = false;
    QString error;
    if (!compilePdfUrl(response, m_server, &url, &sendCookies, &error)) {
        setPdfError(error);
        setPdfBusy(false);
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    if (sendCookies)
        req.setRawHeader("Cookie", m_cookieHeader);
    QNetworkReply *reply = m_network.get(req);
    m_pdfReply = reply;
    QTimer::singleShot(60000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QByteArray data = reply->readAll();
        const QString error = reply->errorString();
        reply->deleteLater();
        if (generation != m_pdfGeneration)
            return;
        m_pdfReply.clear();
        if (!ok) {
            setPdfError(tr("PDF download failed (HTTP %1): %2").arg(status).arg(error));
        } else if (!data.startsWith("%PDF-")) {
            setPdfError(tr("The server did not return a PDF document."));
        } else {
            QScopedPointer<QTemporaryFile> file(new QTemporaryFile(
                QDir::tempPath() + QStringLiteral("/abedul-pdf-XXXXXX")));
            if (!file->open() || file->write(data) != data.size() || !file->flush()) {
                setPdfError(tr("Could not save the preview PDF: %1").arg(file->errorString()));
            } else {
                m_pdfPath = file->fileName();
                file->close();
                m_pdfFile.swap(file);
                emit pdfPathChanged();
            }
        }
        setPdfBusy(false);
    });
}

void OverleafClient::setError(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void OverleafClient::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

bool OverleafClient::setServer(const QString &address)
{
    if (m_busy)
        return false;
    const QUrl url = QUrl::fromUserInput(address.trimmed());
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
            || url.host().isEmpty() || !url.userInfo().isEmpty()
            || (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
            || url.hasQuery() || url.hasFragment()) {
        setError(tr("Enter an HTTPS server address without a path."));
        return false;
    }
    closeProjectPdf();
    closeProjectFiles();
    closeProjectSource();
    m_server = url;
    m_server.setPath(QString());
    m_rememberedServer = m_server.toString();
    m_rememberedServers.removeAll(m_rememberedServer);
    m_rememberedServers.prepend(m_rememberedServer);
    QSettings settings;
    settings.setValue(QStringLiteral("lastServer"), m_rememberedServer);
    settings.setValue(QStringLiteral("servers"), m_rememberedServers);
    settings.sync();
    m_cookieHeader.clear();
    m_csrf.clear();
    m_projects.clear();
    if (m_authenticated) {
        m_authenticated = false;
        emit authenticatedChanged();
    }
    emit projectsChanged();
    emit serverChanged();
    emit rememberedServerChanged();
    emit rememberedServersChanged();
    setError(settings.status() == QSettings::NoError
             ? QString() : tr("Could not remember the server address."));
    return true;
}

bool OverleafClient::isProjectListUrl(const QUrl &url) const
{
    return !m_server.isEmpty() && url.isValid() && url.userInfo().isEmpty()
            && url.scheme() == m_server.scheme()
            && url.host() == m_server.host()
            && url.port(443) == m_server.port(443)
            && (url.path() == QStringLiteral("/project")
                || url.path() == QStringLiteral("/project/"));
}

QNetworkRequest OverleafClient::request(const QString &path) const
{
    QNetworkRequest result(m_server.resolved(QUrl(path)));
    result.setRawHeader("Accept", "application/json, text/html");
    if (!m_cookieHeader.isEmpty())
        result.setRawHeader("Cookie", m_cookieHeader);
    if (!m_csrf.isEmpty())
        result.setRawHeader("X-CSRF-Token", m_csrf);
    return result;
}

bool OverleafClient::checkReply(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        setError(tr("Server request failed (HTTP %1): %2")
                 .arg(status).arg(reply->errorString()));
        setBusy(false);
        return false;
    }
    return true;
}

void OverleafClient::finishBrowserLogin(const QVariantList &cookies)
{
    if (m_busy || m_server.isEmpty())
        return;
    QByteArray header;
    for (const QVariant &entry : cookies) {
        const QVariantMap cookie = entry.toMap();
        const QByteArray name = cookie.value(QStringLiteral("name")).toString().toUtf8();
        const QByteArray value = cookie.value(QStringLiteral("value")).toString().toUtf8();
        if (name.isEmpty() || name.contains(';') || name.contains('=') || name.contains('\n')
                || name.contains('\r') || value.contains(';') || value.contains('\n')
                || value.contains('\r')) {
            setError(tr("The browser returned an invalid session cookie."));
            return;
        }
        if (!header.isEmpty())
            header += "; ";
        header += name + '=' + value;
    }
    if (header.isEmpty()) {
        setError(tr("No session cookies were found. Complete sign-in first."));
        return;
    }
    m_cookieHeader = header;
    m_csrf.clear();
    setError(QString());
    setBusy(true);
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/project")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool ok = checkReply(reply);
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!ok)
            return;
        const QString token = extractCsrfToken(QString::fromUtf8(body));
        if (token.isEmpty()) {
            m_cookieHeader.clear();
            setError(tr("The server did not return an authenticated project page."));
            setBusy(false);
            return;
        }
        m_csrf = token.toUtf8();
        loadProjects();
    });
}

void OverleafClient::refreshProjects()
{
    if (m_busy || !m_authenticated)
        return;
    setError(QString());
    setBusy(true);
    loadProjects();
}

void OverleafClient::loadProjects()
{
    QNetworkRequest req = request(QStringLiteral("/api/project"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject payload{
        {QStringLiteral("filters"), QJsonObject()},
        {QStringLiteral("page"), QJsonObject{{QStringLiteral("size"), 10000}}},
        {QStringLiteral("sort"), QJsonObject{{QStringLiteral("by"), QStringLiteral("lastUpdated")},
                                             {QStringLiteral("order"), QStringLiteral("desc")}}}
    };
    QNetworkReply *reply = m_network.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool ok = checkReply(reply);
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!ok)
            return;
        QVariantList projects;
        QString parseError;
        if (!parseProjectList(body, &projects, &parseError)) {
            setError(parseError);
            setBusy(false);
            return;
        }
        m_projects = projects;
        emit projectsChanged();
        if (!m_authenticated) {
            m_authenticated = true;
            emit authenticatedChanged();
        }
        setBusy(false);
    });
}
