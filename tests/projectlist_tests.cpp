#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QRegularExpression>
#include <QNetworkCookieJar>
#include <QProcess>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QCryptographicHash>
#include "overleafclient.h"
#include "overleafhtml.h"
#include "projectlist.h"
#include "compileoutput.h"
#include "socketprotocol.h"
#include "synctex.h"

class ArchiveServer : public QTcpServer
{
public:
    QSslCertificate certificate;
    QSslKey key;
    QByteArray archive;
    int archiveStatus = 200;
    int archiveRequests = 0;
    QByteArray archiveRequest;
    int createStatus = 200;
    int createRequests = 0;
    QByteArray createRequest;
    QByteArray createResponse = "{\"project_id\":\"new-project\"}";
    int actionStatus = 200;
    int actionRequests = 0;
    QByteArray actionRequest;
    int projectsStatus = 200;
    int commentsStatus = 200;
    QByteArray userId = "user-one";
    QByteArray projectRequest;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        QSslSocket *socket = new QSslSocket(this);
        socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(certificate);
        socket->setPrivateKey(key);
        connect(socket, &QSslSocket::readyRead, this, [this, socket]() {
            auto sendFrame = [socket](const QByteArray &text) {
                QByteArray frame;
                frame.append(char(0x81));
                if (text.size() < 126)
                    frame.append(char(text.size()));
                else {
                    frame.append(char(126));
                    frame.append(char((text.size() >> 8) & 0xff));
                    frame.append(char(text.size() & 0xff));
                }
                socket->write(frame + text);
            };
            if (socket->property("websocket").toBool()) {
                socket->readAll();
                if (!socket->property("ackSent").toBool()) {
                    socket->setProperty("ackSent", true);
                    sendFrame("6:::1+[null,[\"cached text\"],1,[]]");
                }
                return;
            }
            socket->setProperty("request", socket->property("request").toByteArray() + socket->readAll());
            const QByteArray request = socket->property("request").toByteArray();
            if (!request.contains("\r\n\r\n") || socket->property("replied").toBool())
                return;
            const int headersEnd = request.indexOf("\r\n\r\n") + 4;
            if (request.startsWith("GET /socket.io/1/websocket/")) {
                QByteArray wsKey;
                for (const QByteArray &line : request.left(headersEnd).split('\n')) {
                    if (line.toLower().startsWith("sec-websocket-key:"))
                        wsKey = line.mid(line.indexOf(':') + 1).trimmed();
                }
                const QByteArray accept = QCryptographicHash::hash(
                    wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                    QCryptographicHash::Sha1).toBase64();
                socket->setProperty("websocket", true);
                socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                              "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
                sendFrame("5:::{\"name\":\"joinProjectResponse\",\"args\":[{\"protocolVersion\":2,"
                          "\"project\":{\"rootDoc_id\":\"doc\",\"rootFolder\":[{\"_id\":\"root\","
                          "\"docs\":[{\"_id\":\"doc\",\"name\":\"main.tex\"}],"
                          "\"fileRefs\":[{\"_id\":\"image\",\"name\":\"image.png\"}],\"folders\":[]}]}}]}");
                return;
            }
            const QRegularExpression lengthPattern(QStringLiteral("(?im)^content-length:\\s*(\\d+)\\s*$"));
            const auto lengthMatch = lengthPattern.match(QString::fromLatin1(request.left(headersEnd)));
            if (lengthMatch.hasMatch() && request.size() < headersEnd + lengthMatch.captured(1).toInt())
                return;
            socket->setProperty("replied", true);
            QByteArray body;
            QByteArray status = "200 OK";
            if (request.startsWith("GET /project HTTP/")) {
                projectRequest = request;
                body = "<meta name=\"ol-csrfToken\" content=\"test\">"
                       "<meta name=\"ol-user\" content='{&quot;id&quot;:&quot;" + userId + "&quot;}'>";
            } else if (request.startsWith("GET /project/one HTTP/")) {
                body = "<meta name=\"ol-csrfToken\" content=\"test\">";
            } else if (request.startsWith("GET /project/one/entities HTTP/")) {
                body = "{\"entities\":[{\"path\":\"/main.tex\",\"type\":\"doc\"},"
                       "{\"path\":\"/image.png\",\"type\":\"file\"}]}";
            } else if (request.startsWith("GET /socket.io/1/?")) {
                body = "sid:15:60:websocket";
            } else if (request.startsWith("GET /project/one/file/image HTTP/")) {
                body = "image bytes";
            } else if (request.startsWith("POST /project/one/compile HTTP/")) {
                body = "{\"status\":\"success\",\"outputFiles\":[{\"type\":\"pdf\",\"url\":\"/project/one/output/output.pdf\"}]}";
            } else if (request.startsWith("GET /project/one/output/output.pdf HTTP/")) {
                body = "%PDF-test";
            } else if (request.startsWith("POST /api/project HTTP/")) {
                body = "{\"projects\":[{\"id\":\"one\",\"name\":\"My Project\",\"accessLevel\":\"owner\"}]}";
                if (projectsStatus == 503) status = "503 Service Unavailable";
            } else if (request.startsWith("GET /project/one/threads HTTP/")) {
                body = "{}";
                if (commentsStatus == 404) status = "404 Not Found";
                if (commentsStatus == 403) status = "403 Forbidden";
            } else if (request.startsWith("POST /project/new HTTP/")) {
                ++createRequests;
                createRequest = request;
                body = createResponse;
                if (createStatus == 403)
                    status = "403 Forbidden";
            } else if (request.startsWith("GET /project/one/download/zip HTTP/")) {
                ++archiveRequests;
                archiveRequest = request;
                body = archive;
                if (archiveStatus == 503)
                    status = "503 Service Unavailable";
                else if (archiveStatus == 302)
                    status = "302 Found";
            } else if (request.startsWith("POST /project/one/trash HTTP/")
                       || request.startsWith("DELETE /project/one/trash HTTP/")
                       || request.startsWith("DELETE /project/one HTTP/")) {
                ++actionRequests;
                actionRequest = request;
                if (actionStatus == 403) status = "403 Forbidden";
                if (actionStatus == 503) status = "503 Service Unavailable";
                if (actionStatus == 302) status = "302 Found";
            }
            socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size())
                          + "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
        socket->startServerEncryption();
    }
};

class ProjectListTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesLists();
    void rejectsMalformedResponses();
    void extractsCsrfFromMetaTag();
    void identifiesAuthenticatedProjectPage();
    void resolvesCompilePdf();
    void buildsCompileRequest();
    void parsesRealtimeDocument();
    void findsBinaryProjectFiles();
    void findsProjectFolders();
    void findsMainDocument();
    void handlesSyncTeXProtocol();
    void createsTextOperations();
    void preservesHandshakeCookies();
    void remembersServerHistory();
    void downloadsProjectArchive();
};

void ProjectListTests::downloadsProjectArchive()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
    const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
    const QByteArray oldData = qgetenv("XDG_DATA_HOME");
    struct RestoreEnvironment {
        QByteArray home;
        QByteArray config;
        QByteArray cache;
        QByteArray data;
        QString organization;
        QString application;
        QSslConfiguration ssl;
        ~RestoreEnvironment() {
            qputenv("HOME", home);
            qputenv("XDG_CONFIG_HOME", config);
            qputenv("XDG_CACHE_HOME", cache);
            qputenv("XDG_DATA_HOME", data);
            QCoreApplication::setOrganizationName(organization);
            QCoreApplication::setApplicationName(application);
            QSslConfiguration::setDefaultConfiguration(ssl);
        }
    } restore{oldHome, oldConfig, oldCache, oldData, QCoreApplication::organizationName(),
              QCoreApplication::applicationName(), QSslConfiguration::defaultConfiguration()};
    QCoreApplication::setOrganizationName(QStringLiteral("AbedulTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ArchiveTest"));
    qputenv("HOME", directory.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", QDir(directory.path()).filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", QDir(directory.path()).filePath(QStringLiteral("cache")).toUtf8());
    qputenv("XDG_DATA_HOME", QDir(directory.path()).filePath(QStringLiteral("data")).toUtf8());
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!QDir::cleanPath(downloads).startsWith(QDir::cleanPath(directory.path()) + QLatin1Char('/')))
        QSKIP("Cannot isolate Downloads for this platform");

    const QString keyPath = QDir(directory.path()).filePath(QStringLiteral("key.pem"));
    const QString certPath = QDir(directory.path()).filePath(QStringLiteral("cert.pem"));
    const QStringList args{QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
                           QStringLiteral("rsa:2048"), QStringLiteral("-nodes"), QStringLiteral("-keyout"),
                           keyPath, QStringLiteral("-out"), certPath, QStringLiteral("-days"),
                           QStringLiteral("1"), QStringLiteral("-subj"), QStringLiteral("/CN=localhost")};
    QProcess openssl;
    openssl.start(QStringLiteral("openssl"), args);
    QVERIFY(openssl.waitForFinished());
    QCOMPARE(openssl.exitCode(), 0);
    QFile certFile(certPath);
    QFile keyFile(keyPath);
    QVERIFY(certFile.open(QIODevice::ReadOnly));
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    ArchiveServer server;
    server.certificate = QSslCertificate(&certFile);
    server.key = QSslKey(&keyFile, QSsl::Rsa);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QSslConfiguration trusted = restore.ssl;
    trusted.setCaCertificates(QList<QSslCertificate>() << server.certificate);
    QSslConfiguration::setDefaultConfiguration(trusted);

    OverleafClient client;
    QSignalSpy downloaded(&client, SIGNAL(archiveDownloaded(QString)));
    QVERIFY(client.setServer(QStringLiteral("https://localhost:%1").arg(server.serverPort())));
    client.finishBrowserLogin({QVariantMap{{QStringLiteral("name"), QStringLiteral("session")},
                                           {QStringLiteral("value"), QStringLiteral("test")}}});
    QTRY_VERIFY(!client.busy());
    QVERIFY2(client.authenticated(), qPrintable(client.error()));
    QCOMPARE(client.rememberedServer(), client.server());
    client.compileProjectPdf(QStringLiteral("one"), QStringLiteral("/main.tex"));
    QTRY_VERIFY(!client.pdfBusy());
    QVERIFY2(!client.pdfPath().isEmpty(), qPrintable(client.pdfError()));
    client.closeProjectPdf();
    client.restoreProjectPdf(QStringLiteral("one"));
    QVERIFY2(!client.pdfPath().isEmpty(), qPrintable(client.pdfError()));
    QFile cachedPdf(client.pdfPath());
    QVERIFY(cachedPdf.open(QIODevice::ReadOnly));
    QCOMPARE(cachedPdf.readAll(), QByteArray("%PDF-test"));
    server.projectsStatus = 503;
    {
        OverleafClient cached;
        QVERIFY(cached.setServer(QStringLiteral("https://localhost:%1").arg(server.serverPort())));
        cached.finishBrowserLogin({QVariantMap{{QStringLiteral("name"), QStringLiteral("session")},
                                               {QStringLiteral("value"), QStringLiteral("test")}}});
        QTRY_VERIFY(!cached.busy());
        QVERIFY(cached.authenticated());
        QVERIFY(cached.projectsCached());
        QCOMPARE(cached.projects().size(), 1);
        cached.changeProjectTrash(QStringLiteral("one"), QStringLiteral("trash"));
        QVERIFY(!cached.projectActionError().isEmpty());
    }
    server.userId = "another-user";
    {
        OverleafClient other;
        QVERIFY(other.setServer(QStringLiteral("https://localhost:%1").arg(server.serverPort())));
        other.finishBrowserLogin({QVariantMap{{QStringLiteral("name"), QStringLiteral("session")},
                                              {QStringLiteral("value"), QStringLiteral("test")}}});
        QTRY_VERIFY(!other.busy());
        QVERIFY(!other.authenticated());
        QVERIFY(other.projects().isEmpty());
    }
    server.userId = "user-one";
    server.projectsStatus = 200;
    server.commentsStatus = 404;
    QSignalSpy commentsChanged(&client, SIGNAL(commentsAvailableChanged()));
    client.probeProjectComments(QStringLiteral("one"));
    QTRY_VERIFY(commentsChanged.count() >= 2);
    QVERIFY(!client.commentsAvailable());
    server.commentsStatus = 403;
    client.probeProjectComments(QStringLiteral("one"));
    QTRY_VERIFY(client.commentsAvailable());
    server.commentsStatus = 200;
    client.probeProjectComments(QStringLiteral("one"));
    QTRY_VERIFY(client.commentsAvailable());
    client.createProject(QStringLiteral("  New thesis  "));
    QVERIFY(client.projectCreationBusy());
    QTRY_VERIFY(!client.busy());
    QVERIFY2(client.projectCreationError().isEmpty(), qPrintable(client.projectCreationError()));
    QCOMPARE(server.createRequests, 1);
    QVERIFY(server.createRequest.toLower().contains("cookie: session=test"));
    QVERIFY(server.createRequest.toLower().contains("x-csrf-token: test"));
    QCOMPARE(QJsonDocument::fromJson(server.createRequest.mid(server.createRequest.indexOf("\r\n\r\n") + 4))
             .object().value(QStringLiteral("projectName")).toString(), QStringLiteral("New thesis"));
    server.createStatus = 403;
    client.createProject(QStringLiteral("Rejected"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.projectCreationError().contains(QStringLiteral("403")));
    server.createStatus = 200;
    server.createResponse = "{}";
    client.createProject(QStringLiteral("Unknown outcome"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.projectCreationError().contains(QStringLiteral("Refresh")));
    server.archive = QByteArray::fromHex("504b0506000000000000000000000000000000000000");
    client.downloadProjectArchive(QStringLiteral("one"));
    QVERIFY(client.archiveBusy());
    client.downloadProjectArchive(QStringLiteral("one"));
    QTRY_VERIFY(!client.archiveBusy());
    QVERIFY2(client.archiveError().isEmpty(), qPrintable(client.archiveError()));
    QCOMPARE(server.archiveRequests, 1);
    QVERIFY(server.archiveRequest.toLower().contains("cookie: session=test"));
    const QString saved = client.archivePath();
    QCOMPARE(downloaded.size(), 1);
    QCOMPARE(downloaded.first().first().toString(), saved);
    QVERIFY(QFileInfo(saved).fileName().startsWith(QStringLiteral("My Project-")));
    QFile file(saved);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), server.archive);
    file.close();
    QVERIFY(file.remove());

    server.archive = "<html>not an archive</html>";
    client.downloadProjectArchive(QStringLiteral("one"));
    QTRY_VERIFY(!client.archiveBusy());
    QVERIFY(!client.archiveError().isEmpty());
    QVERIFY(client.archivePath().isEmpty());
    QCOMPARE(QDir(downloads).entryList(QDir::Files).size(), 0);
    QCOMPARE(downloaded.size(), 1);

    server.archiveStatus = 503;
    client.downloadProjectArchive(QStringLiteral("one"));
    QTRY_VERIFY(!client.archiveBusy());
    QVERIFY(client.archiveError().contains(QStringLiteral("503")));
    QVERIFY(client.archivePath().isEmpty());
    QCOMPARE(QDir(downloads).entryList(QDir::Files).size(), 0);
    QCOMPARE(downloaded.size(), 1);

    QSignalSpy actionFinished(&client, SIGNAL(projectTrashChanged(QString,QString)));
    client.changeProjectTrash(QStringLiteral("../one"), QStringLiteral("trash"));
    QVERIFY(!client.projectActionError().isEmpty());
    QCOMPARE(server.actionRequests, 0);
    server.actionStatus = 403;
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("trash"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.projectActionError().contains(QStringLiteral("403")));
    QCOMPARE(actionFinished.count(), 0);
    QVERIFY(!client.projects().at(0).toMap().value(QStringLiteral("trashed")).toBool());
    server.actionStatus = 503;
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("trash"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.projectActionError().contains(QStringLiteral("may have succeeded")));
    server.actionStatus = 302;
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("trash"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(actionFinished.count(), 0);
    server.actionStatus = 200;
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("trash"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(actionFinished.count(), 1);
    QVERIFY(client.projects().at(0).toMap().value(QStringLiteral("trashed")).toBool());
    QVERIFY(server.actionRequest.startsWith("POST /project/one/trash HTTP/"));
    QVERIFY(server.actionRequest.toLower().contains("cookie: session=test"));
    QVERIFY(server.actionRequest.toLower().contains("x-csrf-token: test"));
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("restore"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(!client.projects().at(0).toMap().value(QStringLiteral("trashed")).toBool());
    QVERIFY(server.actionRequest.startsWith("DELETE /project/one/trash HTTP/"));
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("delete"));
    QVERIFY(!client.projectActionError().isEmpty());
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("trash"));
    QTRY_VERIFY(!client.busy());
    client.changeProjectTrash(QStringLiteral("one"), QStringLiteral("delete"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(server.actionRequest.startsWith("DELETE /project/one HTTP/"));
    QVERIFY(client.projects().isEmpty());

    client.refreshProjects();
    QTRY_VERIFY(!client.busy());
    client.loadProjectFiles(QStringLiteral("one"));
    QTRY_VERIFY(!client.filesBusy());
    QVERIFY2(client.filesError().isEmpty(), qPrintable(client.filesError()));
    QCOMPARE(client.projectFiles().size(), 2);
    client.openMainProjectSource(QStringLiteral("one"));
    QTRY_VERIFY(!client.sourceBusy());
    QVERIFY2(client.sourceError().isEmpty(), qPrintable(client.sourceError()));
    QCOMPARE(client.sourceText(), QStringLiteral("cached text"));
    QSignalSpy onlineOpened(&client, SIGNAL(externalFileReady(QUrl)));
    client.openProjectFileExternally(QStringLiteral("one"), QStringLiteral("/image.png"));
    QTRY_COMPARE(onlineOpened.size(), 1);

    server.userId = "another-user";
    OverleafClient second;
    QVERIFY(second.setServer(client.server()));
    second.finishBrowserLogin({QVariantMap{{QStringLiteral("name"), QStringLiteral("session")},
                                           {QStringLiteral("value"), QStringLiteral("second")}}});
    QTRY_VERIFY(!second.busy());
    QVERIFY(second.authenticated());
    QCOMPARE(second.cachedAccounts().size(), 2);

    ArchiveServer otherServer;
    otherServer.certificate = server.certificate;
    otherServer.key = server.key;
    QVERIFY(otherServer.listen(QHostAddress::LocalHost));
    OverleafClient otherServerClient;
    QVERIFY(otherServerClient.setServer(QStringLiteral("https://localhost:%1").arg(otherServer.serverPort())));
    otherServerClient.finishBrowserLogin({QVariantMap{{QStringLiteral("name"), QStringLiteral("session")},
                                                      {QStringLiteral("value"), QStringLiteral("test")}}});
    QTRY_VERIFY(!otherServerClient.busy());
    QVERIFY(otherServerClient.authenticated());
    QCOMPARE(otherServerClient.cachedAccounts().size(), 3);

    OverleafClient secondResume;
    QVERIFY(secondResume.openOfflineAccount(client.server(), QStringLiteral("another-user")));
    secondResume.resumeSavedSession();
    QTRY_VERIFY(!secondResume.busy());
    QVERIFY(!secondResume.offline());
    QVERIFY(server.projectRequest.toLower().contains("cookie: session=second"));

    OverleafClient reconnecting;
    QVERIFY(reconnecting.openOfflineAccount(client.server(), QStringLiteral("user-one")));
    const QByteArray key = client.server().toUtf8() + '\0' + QByteArray("user-one");
    const QString sessionPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("sessions/projects-")
                      + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
                      + QStringLiteral(".json"));
    QVERIFY(QFileInfo::exists(sessionPath));
    const QFile::Permissions unsafe = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    QVERIFY(!(QFileInfo(sessionPath).permissions() & unsafe));
    QVERIFY(QFile::rename(sessionPath, sessionPath + QStringLiteral(".test")));
    reconnecting.resumeSavedSession();
    QVERIFY(reconnecting.offline());
    QVERIFY(!reconnecting.error().isEmpty());
    QCOMPARE(reconnecting.projects().size(), 1);
    QVERIFY(QFile::rename(sessionPath + QStringLiteral(".test"), sessionPath));
    server.userId = "another-user";
    reconnecting.finishBrowserLogin({QVariantMap{{QStringLiteral("name"), QStringLiteral("session")},
                                                  {QStringLiteral("value"), QStringLiteral("test")}}});
    QTRY_VERIFY(!reconnecting.busy());
    QVERIFY(reconnecting.offline());
    QVERIFY(reconnecting.error().contains(QStringLiteral("different account")));
    reconnecting.openMainProjectSource(QStringLiteral("one"));
    QCOMPARE(reconnecting.sourceText(), QStringLiteral("cached text"));
    server.userId = "user-one";
    server.projectsStatus = 503;
    reconnecting.finishBrowserLogin({QVariantMap{{QStringLiteral("name"), QStringLiteral("session")},
                                                  {QStringLiteral("value"), QStringLiteral("test")}}});
    QTRY_VERIFY(!reconnecting.busy());
    QVERIFY(reconnecting.offline());
    QVERIFY(!reconnecting.error().isEmpty());
    QCOMPARE(reconnecting.projects().size(), 1);
    server.projectsStatus = 200;
    reconnecting.resumeSavedSession();
    QTRY_VERIFY(!reconnecting.busy());
    QVERIFY(!reconnecting.offline());
    QVERIFY(server.projectRequest.toLower().contains("cookie: session=test"));
    QVERIFY(reconnecting.error().isEmpty());
    QVERIFY(!reconnecting.projectsCached());
    server.close();
    otherServer.close();
    reconnecting.loadProjectFiles(QStringLiteral("one"));
    QTRY_VERIFY(!reconnecting.filesBusy());
    QVERIFY(reconnecting.offline());
    QVERIFY(!reconnecting.error().isEmpty());
    QCOMPARE(reconnecting.projectFiles().size(), 2);
    reconnecting.openMainProjectSource(QStringLiteral("one"));
    QCOMPARE(reconnecting.sourceText(), QStringLiteral("cached text"));

    OverleafClient offline;
    QVERIFY(!offline.openOfflineAccount(client.server(), QStringLiteral("unknown")));
    QVERIFY(offline.openOfflineAccount(client.server(), QStringLiteral("user-one")));
    QVERIFY(offline.offline());
    QCOMPARE(offline.projects().size(), 1);
    offline.loadProjectFiles(QStringLiteral("one"));
    QCOMPARE(offline.projectFiles().size(), 2);
    offline.openMainProjectSource(QStringLiteral("one"));
    QCOMPARE(offline.sourcePath(), QStringLiteral("/main.tex"));
    QCOMPARE(offline.sourceText(), QStringLiteral("cached text"));
    QSignalSpy opened(&offline, SIGNAL(externalFileReady(QUrl)));
    offline.openProjectFileExternally(QStringLiteral("one"), QStringLiteral("/image.png"));
    QCOMPARE(opened.size(), 1);
    QFile copy(opened.first().first().toUrl().toLocalFile());
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(copy.readAll(), QByteArray("image bytes"));
    offline.restoreProjectPdf(QStringLiteral("one"));
    QVERIFY(!offline.pdfPath().isEmpty());
    offline.openProjectSource(QStringLiteral("one"), QStringLiteral("/missing.tex"));
    QVERIFY(!offline.sourceError().isEmpty());
    offline.saveProjectSource(QStringLiteral("one"), QStringLiteral("/main.tex"), QString(), false, QString());
    QVERIFY(!offline.sourceError().isEmpty());
    QSignalSpy retry(&offline, SIGNAL(reconnectRequested()));
    offline.refreshProjects();
    QCOMPARE(retry.size(), 1);
    QVERIFY(offline.openOfflineAccount(client.server(), QStringLiteral("another-user")));
    offline.loadProjectFiles(QStringLiteral("one"));
    QVERIFY(!offline.filesError().isEmpty());
    offline.openProjectSource(QStringLiteral("one"), QStringLiteral("/main.tex"));
    QVERIFY(!offline.sourceError().isEmpty());
    offline.restoreProjectPdf(QStringLiteral("one"));
    QVERIFY(offline.pdfPath().isEmpty());
    QVERIFY(offline.openOfflineAccount(otherServerClient.server(), QStringLiteral("user-one")));
    offline.openProjectSource(QStringLiteral("one"), QStringLiteral("/main.tex"));
    QVERIFY(!offline.sourceError().isEmpty());
    offline.restoreProjectPdf(QStringLiteral("one"));
    QVERIFY(offline.pdfPath().isEmpty());

    const QString accountName = QFileInfo(sessionPath).fileName();
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString offlineBase = QDir(appData).filePath(QStringLiteral("offline/") + accountName);
    const QString draftDir = QDir(appData).filePath(QStringLiteral("drafts/") + accountName);
    QVERIFY(QDir().mkpath(draftDir));
    QFile accountDraft(QDir(draftDir).filePath(QStringLiteral("draft.json")));
    QVERIFY(accountDraft.open(QIODevice::WriteOnly));
    QVERIFY(accountDraft.write("draft") > 0);
    accountDraft.close();
    const QString legacyBase = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .filePath(accountName);
    QFile legacy(legacyBase);
    QVERIFY(legacy.open(QIODevice::WriteOnly));
    QVERIFY(legacy.write("old cache") > 0);
    legacy.close();
    QVERIFY(offline.cookieRemovalUrl(QStringLiteral("https://example.org"),
                                    QStringLiteral(".example.org"), QStringLiteral("/private"))
            == QStringLiteral("https://example.org/private"));
    QVERIFY(offline.cookieRemovalUrl(QStringLiteral("https://example.org"),
                                    QStringLiteral("other.example.org"), QStringLiteral("/")).isEmpty());
    QVERIFY(offline.cookieRemovalUrl(QStringLiteral("https://example.org"),
                                    QStringLiteral(".evil-example.org"), QStringLiteral("/")).isEmpty());
    QVERIFY2(offline.removeCachedAccount(client.server(), QStringLiteral("user-one")), qPrintable(offline.error()));
    QVERIFY(!QFileInfo::exists(sessionPath));
    QVERIFY(!QFileInfo::exists(offlineBase));
    QVERIFY(!QFileInfo::exists(draftDir));
    QVERIFY(!QFileInfo::exists(legacyBase));
    QVERIFY(!QFileInfo::exists(copy.fileName()));
    QCOMPARE(offline.cachedAccounts().size(), 2);
    QVERIFY(offline.openOfflineAccount(client.server(), QStringLiteral("another-user")));
    QVERIFY(offline.removeSavedServer(client.server()));
    QCOMPARE(offline.cachedAccounts().size(), 1);
    QVERIFY(!offline.rememberedServers().contains(client.server()));
    QVERIFY(offline.openOfflineAccount(otherServerClient.server(), QStringLiteral("user-one")));
    QVERIFY(offline.removeSavedServer(otherServerClient.server()));
    QVERIFY(offline.cachedAccounts().isEmpty());
    QVERIFY(!offline.autoRestoreServer());
    QVERIFY(!offline.authenticated());
}

void ProjectListTests::parsesLists()
{
    QVariantList projects;
    QString error;
    QVERIFY(parseProjectList(
        "{\"projects\":[{\"id\":\"1\",\"name\":\"One\"},"
        "{\"_id\":\"2\",\"name\":\"Old\",\"archived\":true},"
        "{\"project_id\":\"3\",\"projectName\":\"Three\"}]}",
        &projects, &error));
    QCOMPARE(projects.size(), 3);
    QVERIFY(projects.at(1).toMap().value(QStringLiteral("archived")).toBool());
    QCOMPARE(projects.at(2).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Three"));
    QVERIFY(error.isEmpty());
    QVERIFY(parseProjectList("[]", &projects, &error));
    QVERIFY(projects.isEmpty());
}

void ProjectListTests::rejectsMalformedResponses()
{
    QVariantList projects;
    QString error;
    QVERIFY(!parseProjectList("not-json", &projects, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!parseProjectList("{}", &projects, &error));
    QVERIFY(!parseProjectList("[{\"name\":\"Missing ID\"}]", &projects, &error));
}

void ProjectListTests::extractsCsrfFromMetaTag()
{
    QCOMPARE(extractCsrfToken(QStringLiteral("<meta name=\"ol-csrfToken\" content=\"abc\">")),
             QStringLiteral("abc"));
    QCOMPARE(extractCsrfToken(QStringLiteral("<META content='xyz' name='ol-csrfToken'>")),
             QStringLiteral("xyz"));
    QVERIFY(extractCsrfToken(QStringLiteral("<meta name='other' content='abc'>")).isEmpty());
}

void ProjectListTests::identifiesAuthenticatedProjectPage()
{
    OverleafClient client;
    QVERIFY(client.setServer(QStringLiteral("https://www.overleaf.com")));
    QVERIFY(client.isProjectListUrl(QUrl(QStringLiteral("https://www.overleaf.com/project"))));
    QVERIFY(client.isProjectListUrl(QUrl(QStringLiteral("https://www.overleaf.com:443/project/?page=1"))));
    QVERIFY(!client.isProjectListUrl(QUrl(QStringLiteral("https://www.overleaf.com/login"))));
    QVERIFY(!client.isProjectListUrl(QUrl(QStringLiteral("https://www.overleaf.com/project/other"))));
    QVERIFY(!client.isProjectListUrl(QUrl(QStringLiteral("https://www.overleaf.com.example.org/project"))));
    QVERIFY(!client.isProjectListUrl(QUrl(QStringLiteral("http://www.overleaf.com/project"))));
    QVERIFY(!client.isProjectListUrl(QUrl(QStringLiteral("https://www.overleaf.com:444/project"))));
    QVERIFY(client.setServer(QStringLiteral("https://latex.example.org:8443")));
    QVERIFY(client.isProjectListUrl(QUrl(QStringLiteral("https://latex.example.org:8443/project"))));
    QVERIFY(!client.isProjectListUrl(QUrl(QStringLiteral("https://latex.example.org/project"))));
}

void ProjectListTests::resolvesCompilePdf()
{
    const QUrl server(QStringLiteral("https://www.overleaf.com"));
    QUrl url;
    bool cookies = false;
    QString error;
    const QByteArray output = R"({"status":"success","outputFiles":[{"type":"pdf","url":"/project/abc/output/output.pdf?build=one"}]})";
    QVERIFY(compilePdfUrl(output, server, &url, &cookies, &error));
    QCOMPARE(url.toString(), QStringLiteral("https://www.overleaf.com/project/abc/output/output.pdf?build=one"));
    QVERIFY(cookies);
    const QByteArray cdn = R"({"status":"success","outputFiles":[{"type":"pdf","url":"/project/abc/output/output.pdf"}],"pdfDownloadDomain":"https://pdf.example","clsiServerId":"node1","compileGroup":"standard"})";
    QVERIFY(compilePdfUrl(cdn, server, &url, &cookies, &error));
    QCOMPARE(url.host(), QStringLiteral("pdf.example"));
    QVERIFY(!cookies);
    QVERIFY(url.query().contains(QStringLiteral("clsiserverid=node1")));
    const QByteArray failed = R"({"status":"failure","outputFiles":[]})";
    const QByteArray absolute = R"({"status":"success","outputFiles":[{"type":"pdf","url":"https://attacker.example/file"}]})";
    const QByteArray traversal = R"({"status":"success","outputFiles":[{"type":"pdf","url":"/../secret"}]})";
    const QByteArray insecure = R"({"status":"success","outputFiles":[{"type":"pdf","url":"/file"}],"pdfDownloadDomain":"http://bad.example","clsiServerId":"node"})";
    QVERIFY(!compilePdfUrl(failed, server, &url, &cookies, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!compilePdfUrl(absolute, server, &url, &cookies, &error));
    QVERIFY(!compilePdfUrl(traversal, server, &url, &cookies, &error));
    QVERIFY(!compilePdfUrl(insecure, server, &url, &cookies, &error));
}

void ProjectListTests::buildsCompileRequest()
{
    QByteArray body;
    QString error;
    QVERIFY(compileRequestBody(QStringLiteral("/chapters/main.tex"), &body, &error));
    const QJsonObject payload = QJsonDocument::fromJson(body).object();
    QCOMPARE(payload.value(QStringLiteral("rootResourcePath")).toString(),
             QStringLiteral("chapters/main.tex"));
    QVERIFY(payload.value(QStringLiteral("incrementalCompilesEnabled")).toBool());
    const QString editorId = QStringLiteral("12345678-1234-4234-8234-123456789abc");
    QVERIFY(compileRequestBody(QStringLiteral("/main.tex"), &body, &error, editorId));
    QCOMPARE(QJsonDocument::fromJson(body).object().value(QStringLiteral("editorId")).toString(), editorId);
    QVERIFY(!compileRequestBody(QString(), &body, &error));
    QVERIFY(!compileRequestBody(QStringLiteral("/../main.tex"), &body, &error));
    QVERIFY(!compileRequestBody(QStringLiteral("//main.tex"), &body, &error));
    const QByteArray serverError = R"({"error":"rootResourcePath is missing in request body"})";
    QCOMPARE(compileServerError(serverError), QStringLiteral("rootResourcePath is missing in request body"));
    QVERIFY(compileServerError("<html>error</html>").isEmpty());
}

void ProjectListTests::findsBinaryProjectFiles()
{
    const QJsonObject tree = QJsonDocument::fromJson(
        R"({"rootFolder":[{"docs":[{"_id":"doc","name":"main.tex"}],"folders":[{"name":"img","fileRefs":[{"_id":"abc123","name":"figure.png"}]}]}]})").object();
    QString id;
    QString error;
    QVERIFY(findProjectFile(tree, QStringLiteral("/img/figure.png"), &id, &error));
    QCOMPARE(id, QStringLiteral("abc123"));
    QVERIFY(!findProjectFile(tree, QStringLiteral("/main.tex"), &id, &error));
    QVERIFY(id.isEmpty());
    QVERIFY(!error.isEmpty());
}

void ProjectListTests::findsProjectFolders()
{
    const QJsonObject tree = QJsonDocument::fromJson(
        R"({"rootFolder":[{"_id":"root","folders":[{"_id":"a","name":"images","folders":[{"_id":"b","name":"figures"}]}]}]})").object();
    QString id, error;
    QVERIFY(findProjectFolder(tree, QStringLiteral("/"), &id, &error));
    QCOMPARE(id, QStringLiteral("root"));
    QVERIFY(findProjectFolder(tree, QStringLiteral("/images/figures"), &id, &error));
    QCOMPARE(id, QStringLiteral("b"));
    QVERIFY(!findProjectFolder(tree, QStringLiteral("/missing"), &id, &error));
    QVERIFY(id.isEmpty());
    const QJsonObject duplicate = QJsonDocument::fromJson(
        R"({"rootFolder":[{"_id":"root","folders":[{"_id":"a","name":"same"},{"_id":"b","name":"same"}]}]})").object();
    QVERIFY(!findProjectFolder(duplicate, QStringLiteral("/same"), &id, &error));
    const QJsonObject valid = QJsonDocument::fromJson(
        R"({"rootFolder":[{"_id":"root","docs":[{"_id":"doc","name":"a.tex"}],"fileRefs":[],"folders":[{"_id":"f","name":"empty","docs":[],"fileRefs":[],"folders":[]}]}]})").object();
    QVERIFY(validProjectFileTree(valid));
    const QVariantList entries = projectFileTreeEntries(valid);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(1).toMap().value(QStringLiteral("path")).toString(), QStringLiteral("/empty"));
    QCOMPARE(entries.at(1).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("folder"));
    QVERIFY(!validProjectFileTree(duplicate));
    const QJsonObject duplicateName = QJsonDocument::fromJson(
        R"({"rootFolder":[{"_id":"root","docs":[{"_id":"1","name":"a"}],"fileRefs":[{"_id":"2","name":"a"}],"folders":[]}]})").object();
    QVERIFY(!validProjectFileTree(duplicateName));
}

void ProjectListTests::findsMainDocument()
{
    const QByteArray json = R"({"rootDoc_id":"nested","rootFolder":[{"_id":"root","docs":[{"_id":"main","name":"main.tex"}],"fileRefs":[],"folders":[{"_id":"folder","name":"chapters","docs":[{"_id":"nested","name":"thesis.tex"}],"fileRefs":[],"folders":[]}]}]})";
    QJsonObject project = QJsonDocument::fromJson(json).object();
    QString path, id, error;
    QVERIFY(findProjectMainDoc(project, &path, &id, &error));
    QCOMPARE(path, QStringLiteral("/chapters/thesis.tex"));
    QCOMPARE(id, QStringLiteral("nested"));
    project.remove(QStringLiteral("rootDoc_id"));
    QVERIFY(findProjectMainDoc(project, &path, &id, &error));
    QCOMPARE(path, QStringLiteral("/main.tex"));
    project.insert(QStringLiteral("rootDoc_id"), QStringLiteral("missing"));
    QVERIFY(!findProjectMainDoc(project, &path, &id, &error));
    QVERIFY(!error.isEmpty());
    project.insert(QStringLiteral("rootDoc_id"), 123);
    QVERIFY(!findProjectMainDoc(project, &path, &id, &error));
    project = QJsonDocument::fromJson(R"({"rootFolder":[{"_id":"r","docs":[],"fileRefs":[],"folders":[{"_id":"f","name":"src","docs":[{"_id":"a","name":"paper.TEX"}],"fileRefs":[],"folders":[]}]}]})").object();
    QVERIFY(findProjectMainDoc(project, &path, &id, &error));
    QCOMPARE(path, QStringLiteral("/src/paper.TEX"));
    project = QJsonDocument::fromJson(R"({"rootFolder":[{"_id":"r","docs":[{"_id":"a","name":"a.tex"},{"_id":"b","name":"b.tex"}],"fileRefs":[],"folders":[]}]})").object();
    QVERIFY(!findProjectMainDoc(project, &path, &id, &error));
    QVERIFY(path.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!findProjectMainDoc(QJsonObject(), &path, &id, &error));
}

void ProjectListTests::handlesSyncTeXProtocol()
{
    const QString editorId = QStringLiteral("12345678-1234-4234-8234-123456789abc");
    const QByteArray response = R"({"status":"success","outputFiles":[{"path":"output.pdf","type":"pdf","build":"abcdef-123456"}],"clsiServerId":"node1"})";
    SyncTeXBuild build;
    QString error;
    QVERIFY(syncTeXBuildFromCompile(response, editorId, &build, &error));
    QCOMPARE(build.buildId, QStringLiteral("abcdef-123456"));
    const QUrl server(QStringLiteral("https://example.org"));
    const QUrl url = syncTeXUrl(server, QStringLiteral("project"), build,
                                QString::fromUtf8("folder/a & b.tex"), 7, 3);
    QCOMPARE(url.host(), server.host());
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("file"), QUrl::FullyDecoded), QStringLiteral("folder/a & b.tex"));
    QCOMPARE(query.queryItemValue(QStringLiteral("line")), QStringLiteral("7"));
    QCOMPARE(query.queryItemValue(QStringLiteral("buildId")), build.buildId);
    QCOMPARE(query.queryItemValue(QStringLiteral("editorId")), editorId);
    const QUrl pdfUrl = syncTeXUrl(server, QStringLiteral("project"), build, 2, 1.5, 23);
    QCOMPARE(QUrlQuery(pdfUrl).queryItemValue(QStringLiteral("page")), QStringLiteral("2"));
    QCOMPARE(QUrlQuery(pdfUrl).queryItemValue(QStringLiteral("h")), QStringLiteral("1.50"));
    QVariantList positions;
    QVERIFY(syncTeXResult(R"({"pdf":[{"page":1,"h":10.25,"v":15,"width":20,"height":3}]})",
                         true, &positions, &error));
    QCOMPARE(positions.first().toMap().value(QStringLiteral("page")).toInt(), 1);
    QVERIFY(syncTeXResult(R"({"code":[{"file":"folder/./main.tex","line":3,"column":-1}]})",
                         false, &positions, &error));
    QVERIFY(!syncTeXResult(R"({"code":[{"file":"main.tex","line":3,"column":"bad"}]})",
                          false, &positions, &error));
    QVERIFY(!syncTeXResult(R"({"code":[{"file":"main.tex","line":3,"column":-2}]})",
                          false, &positions, &error));
    QVERIFY(!syncTeXResult(R"({"pdf":[{"page":0,"h":1,"v":2}]})", true, &positions, &error));
    QVERIFY(positions.isEmpty());
    QVERIFY(!syncTeXBuildFromCompile(R"({"outputFiles":[{"type":"pdf"}]})", editorId, &build, &error));
    QVERIFY(build.buildId.isEmpty());
}

void ProjectListTests::parsesRealtimeDocument()
{
    const QByteArray tree = R"({"rootFolder":[{"docs":[{"_id":"one","name":"main.tex"}],"folders":[{"name":"chapters","docs":[{"_id":"two","name":"intro.tex"}]}]}]})";
    const QJsonObject project = QJsonDocument::fromJson(tree).object();
    QString id;
    QString error;
    QVERIFY(findProjectDoc(project, QStringLiteral("/chapters/intro.tex"), &id, &error));
    QCOMPARE(id, QStringLiteral("two"));
    id.clear();
    QVERIFY(!findProjectDoc(project, QStringLiteral("/missing.tex"), &id, &error));
    const QByteArray ack = R"([null,["caf\u00c3\u00a9","test"],7,[{"op":[{"p":5,"d":"test"},{"p":5,"i":"next"}]}],null,"sharejs-text-ot"])";
    QString text;
    qint64 version = 0;
    QVERIFY(decodeJoinedDoc(QJsonDocument::fromJson(ack).array(), &text, &error, &version));
    QCOMPARE(text, QString::fromUtf8("caf\xc3\xa9\nnext"));
    QCOMPARE(version, qint64(8));
    const QByteArray badAck = R"([null,["hello"],3,[{"op":[{"p":1,"d":"wrong"}]}]])";
    QVERIFY(!decodeJoinedDoc(QJsonDocument::fromJson(badAck).array(), &text, &error));
    QVERIFY(!error.isEmpty());
}

void ProjectListTests::createsTextOperations()
{
    const QStringList before = {QStringLiteral("abc"), QStringLiteral("same"),
                                QString::fromUtf8("a\xf0\x9f\x98\x80z"),
                                QString::fromUtf8("a\xf0\x9f\x98\x80z"), QString()};
    const QStringList after = {QStringLiteral("axc"), QStringLiteral("same"),
                               QString::fromUtf8("a\xf0\x9f\x98\x81z"),
                               QStringLiteral("az"), QString::fromUtf8("\xc3\xa9")};
    for (int i = 0; i < before.size(); ++i) {
        QString result = before.at(i);
        const QJsonArray operations = makeTextOperation(result, after.at(i));
        for (const QJsonValue &value : operations) {
            const QJsonObject op = value.toObject();
            const int pos = op.value(QStringLiteral("p")).toInt();
            QVERIFY(pos == 0 || pos == result.size() || !result.at(pos).isLowSurrogate());
            if (op.contains(QStringLiteral("d"))) {
                const QString removed = op.value(QStringLiteral("d")).toString();
                QCOMPARE(result.mid(pos, removed.size()), removed);
                result.remove(pos, removed.size());
            } else {
                result.insert(pos, op.value(QStringLiteral("i")).toString());
            }
        }
        QCOMPARE(result, after.at(i));
    }
}

void ProjectListTests::preservesHandshakeCookies()
{
    const QUrl handshake(QStringLiteral("https://example.org/socket.io/1/"));
    const QUrl socket(QStringLiteral("wss://example.org/socket.io/1/websocket/sid"));
    QList<QNetworkCookie> cookies = QNetworkCookie::parseCookies("GCLB=route; Path=/; Secure");
    cookies.append(QNetworkCookie::parseCookies("session=new; Path=/; Secure"));
    cookies.append(QNetworkCookie::parseCookies("other=x; Path=/private"));
    const QByteArray result = socketCookieHeader("session=old; user=alice", cookies, handshake, socket);
    QVERIFY(result.contains("user=alice"));
    QVERIFY(result.contains("session=new"));
    QVERIFY(result.contains("GCLB=route"));
    QVERIFY(!result.contains("session=old"));
    QVERIFY(!result.contains("other=x"));
    QNetworkCookieJar stored;
    stored.setCookiesFromUrl(QNetworkCookie::parseCookies("GCLB=old; Path=/; Secure"), handshake);
    stored.setCookiesFromUrl(QNetworkCookie::parseCookies("hidden=x; Path=/private; Secure"), handshake);
    const QByteArray reused = socketCookieHeader("session=old", {}, handshake, socket, &stored);
    QVERIFY(reused.contains("GCLB=old"));
    QVERIFY(!reused.contains("hidden=x"));
    const QByteArray replaced = socketCookieHeader("session=old", cookies, handshake, socket, &stored);
    QVERIFY(replaced.contains("GCLB=route"));
    QVERIFY(!replaced.contains("GCLB=old"));
    const QByteArray otherOrigin = socketCookieHeader("session=old", cookies, handshake,
                                                      QUrl(QStringLiteral("wss://other.example/socket.io/1/websocket/sid")),
                                                      &stored);
    QVERIFY(otherOrigin.isEmpty());
}

void ProjectListTests::remembersServerHistory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QSettings::Format originalFormat = QSettings::defaultFormat();
    const QString oldOrganization = QCoreApplication::organizationName();
    const QString oldApplication = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("AbedulTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ServerHistory"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    {
        OverleafClient client;
        QVERIFY(client.setServer(QStringLiteral("https://one.example")));
        QVERIFY(client.rememberedServer().isEmpty());
        QVERIFY(client.rememberedServers().isEmpty());
        QVERIFY(client.setServer(QStringLiteral("https://two.example")));
        QVERIFY(client.rememberedServers().isEmpty());
        OverleafClient restored;
        QVERIFY(restored.rememberedServer().isEmpty());
        QVERIFY(restored.rememberedServers().isEmpty());
    }
    QSettings::setDefaultFormat(originalFormat);
    QCoreApplication::setOrganizationName(oldOrganization);
    QCoreApplication::setApplicationName(oldApplication);
}

QTEST_GUILESS_MAIN(ProjectListTests)
#include "projectlist_tests.moc"
