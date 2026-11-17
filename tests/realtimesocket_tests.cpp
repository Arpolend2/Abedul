#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QProcess>
#include <QSharedPointer>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTcpServer>
#include <QTemporaryDir>

#include "realtimesocket.h"
#include "socketprotocol.h"

class MockServer : public QTcpServer
{
    Q_OBJECT
public:
    QSslCertificate certificate;
    QSslKey key;
    QByteArray request;
    QByteArray clientFrame;
    QSslSocket *peer = nullptr;
    bool requireRoute = false;
    bool routeOnlyOnce = false;
    int handshakes = 0;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        peer = new QSslSocket(this);
        peer->setSocketDescriptor(descriptor);
        peer->setLocalCertificate(certificate);
        peer->setPrivateKey(key);
        QSslSocket *connection = peer;
        const QSharedPointer<QByteArray> input(new QByteArray);
        connect(peer, &QSslSocket::readyRead, this, [this, connection, input]() {
            if (!input->contains("\r\n\r\n")) {
                *input += connection->readAll();
                if (!input->contains("\r\n\r\n"))
                    return;
                request = *input;
                if (request.startsWith("GET /socket.io/1/?")) {
                    ++handshakes;
                    QByteArray response("HTTP/1.1 200 OK\r\n");
                    if (!routeOnlyOnce || handshakes == 1)
                        response += "Set-Cookie: GCLB=route; Path=/; Secure\r\n";
                    response += "Content-Length: 19\r\nConnection: close\r\n\r\nsid:15:60:websocket";
                    connection->write(response);
                    connection->disconnectFromHost();
                    return;
                }
                if (requireRoute && !request.contains("Cookie: session=test; GCLB=route\r\n")) {
                    connection->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                QByteArray wsKey;
                for (const QByteArray &line : request.split('\n')) {
                    if (line.toLower().startsWith("sec-websocket-key:"))
                        wsKey = line.mid(line.indexOf(':') + 1).trimmed();
                }
                const QByteArray accept = QCryptographicHash::hash(
                            wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                            QCryptographicHash::Sha1).toBase64();
                connection->write("HTTP/1.1 101\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
                connection->write(QByteArray::fromHex("0102686500026c6c80016f"));
            } else {
                clientFrame += connection->readAll();
            }
        });
        peer->startServerEncryption();
    }
};

class RealtimeSocketTests : public QObject
{
    Q_OBJECT
private slots:
    void authenticatedFragmentedExchange();
    void reusesRouteCookieOnLaterHandshake();
};

void RealtimeSocketTests::authenticatedFragmentedExchange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString keyPath = QDir(directory.path()).filePath(QStringLiteral("key.pem"));
    const QString certPath = QDir(directory.path()).filePath(QStringLiteral("cert.pem"));
    QStringList args;
    args << QStringLiteral("req") << QStringLiteral("-x509") << QStringLiteral("-newkey")
         << QStringLiteral("rsa:2048") << QStringLiteral("-nodes") << QStringLiteral("-keyout")
         << keyPath << QStringLiteral("-out") << certPath << QStringLiteral("-days")
         << QStringLiteral("1") << QStringLiteral("-subj") << QStringLiteral("/CN=localhost");
    QCOMPARE(QProcess::execute(QStringLiteral("openssl"), args), 0);
    QFile certificateFile(certPath);
    QFile keyFile(keyPath);
    QVERIFY(certificateFile.open(QIODevice::ReadOnly));
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    MockServer server;
    server.certificate = QSslCertificate(&certificateFile);
    server.key = QSslKey(&keyFile, QSsl::Rsa);
    QVERIFY(!server.certificate.isNull());
    QVERIFY(!server.key.isNull());
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const QSslConfiguration original = QSslConfiguration::defaultConfiguration();
    QSslConfiguration trusted = original;
    trusted.setCaCertificates(QList<QSslCertificate>() << server.certificate);
    QSslConfiguration::setDefaultConfiguration(trusted);
    server.requireRoute = true;
    const QUrl handshakeUrl(QStringLiteral("https://localhost:%1/socket.io/1/?projectId=1")
                            .arg(server.serverPort()));
    QNetworkAccessManager network;
    QNetworkReply *reply = network.get(QNetworkRequest(handshakeUrl));
    QSignalSpy handshakeFinished(reply, SIGNAL(finished()));
    QTRY_COMPARE(handshakeFinished.size(), 1);
    QCOMPARE(reply->error(), QNetworkReply::NoError);
    const QList<QNetworkCookie> received =
            qvariant_cast<QList<QNetworkCookie>>(reply->header(QNetworkRequest::SetCookieHeader));
    QCOMPARE(received.size(), 1);
    RealtimeSocket socket;
    QSignalSpy messages(&socket, SIGNAL(textMessageReceived(QString)));
    QSignalSpy failures(&socket, SIGNAL(failed(QString)));
    const QUrl socketUrl(QStringLiteral("wss://localhost:%1/socket.io/1/websocket/sid?projectId=1")
                         .arg(server.serverPort()));
    socket.open(socketUrl, socketCookieHeader("session=test", received, handshakeUrl, socketUrl),
                "https://localhost");
    QTRY_VERIFY(messages.size() == 1 || failures.size() > 0);
    QSslConfiguration::setDefaultConfiguration(original);
    QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.first().at(0).toString()));
    QCOMPARE(messages.first().at(0).toString(), QStringLiteral("hello"));
    QVERIFY(server.request.contains("Cookie: session=test; GCLB=route\r\n"));
    QVERIFY(server.request.contains("GET /socket.io/1/websocket/sid?projectId=1 HTTP/1.1"));
    socket.sendText(QStringLiteral("2::"));
    QTRY_VERIFY(server.clientFrame.size() >= 9);
    const QByteArray frame = server.clientFrame;
    QCOMPARE(static_cast<unsigned char>(frame.at(0)), static_cast<unsigned char>(0x81));
    QCOMPARE(static_cast<unsigned char>(frame.at(1)), static_cast<unsigned char>(0x83));
    QByteArray decoded;
    for (int i = 0; i < 3; ++i)
        decoded.append(frame.at(6 + i) ^ frame.at(2 + i));
    QCOMPARE(decoded, QByteArray("2::"));
    socket.abort();
}

void RealtimeSocketTests::reusesRouteCookieOnLaterHandshake()
{
    QTemporaryDir directory;
    const QString keyPath = QDir(directory.path()).filePath(QStringLiteral("key.pem"));
    const QString certPath = QDir(directory.path()).filePath(QStringLiteral("cert.pem"));
    const QStringList args{QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
                           QStringLiteral("rsa:2048"), QStringLiteral("-nodes"), QStringLiteral("-keyout"),
                           keyPath, QStringLiteral("-out"), certPath, QStringLiteral("-days"),
                           QStringLiteral("1"), QStringLiteral("-subj"), QStringLiteral("/CN=localhost")};
    QCOMPARE(QProcess::execute(QStringLiteral("openssl"), args), 0);
    QFile certificateFile(certPath);
    QFile keyFile(keyPath);
    QVERIFY(certificateFile.open(QIODevice::ReadOnly));
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    MockServer server;
    server.certificate = QSslCertificate(&certificateFile);
    server.key = QSslKey(&keyFile, QSsl::Rsa);
    server.requireRoute = true;
    server.routeOnlyOnce = true;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const QSslConfiguration original = QSslConfiguration::defaultConfiguration();
    QSslConfiguration trusted = original;
    trusted.setCaCertificates(QList<QSslCertificate>() << server.certificate);
    QSslConfiguration::setDefaultConfiguration(trusted);
    const QUrl handshakeUrl(QStringLiteral("https://localhost:%1/socket.io/1/?projectId=1")
                            .arg(server.serverPort()));
    const QUrl socketUrl(QStringLiteral("wss://localhost:%1/socket.io/1/websocket/sid?projectId=1")
                         .arg(server.serverPort()));
    QNetworkAccessManager network;
    for (int i = 0; i < 2; ++i) {
        QNetworkRequest request(handshakeUrl);
        request.setRawHeader("Cookie", "session=test");
        QNetworkReply *reply = network.get(request);
        QSignalSpy finished(reply, SIGNAL(finished()));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        if (i == 1) {
            QVERIFY(server.request.contains("GCLB=route"));
            const auto responseCookies = qvariant_cast<QList<QNetworkCookie>>(reply->header(QNetworkRequest::SetCookieHeader));
            QVERIFY(responseCookies.isEmpty());
            QUrl cookieUrl = socketUrl;
            cookieUrl.setScheme(QStringLiteral("https"));
            QVERIFY(!network.cookieJar()->cookiesForUrl(cookieUrl).isEmpty());
            RealtimeSocket socket;
            QSignalSpy messages(&socket, SIGNAL(textMessageReceived(QString)));
            QSignalSpy failures(&socket, SIGNAL(failed(QString)));
            socket.open(socketUrl, socketCookieHeader("session=test", responseCookies, handshakeUrl, socketUrl,
                                                      network.cookieJar()),
                        "https://localhost");
            QTRY_VERIFY(!messages.isEmpty() || !failures.isEmpty());
            QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.first().at(0).toString()));
            QVERIFY(server.request.contains("Cookie: session=test; GCLB=route\r\n"));
            socket.abort();
        }
        reply->deleteLater();
    }
    QSslConfiguration::setDefaultConfiguration(original);
}

QTEST_GUILESS_MAIN(RealtimeSocketTests)
#include "realtimesocket_tests.moc"
