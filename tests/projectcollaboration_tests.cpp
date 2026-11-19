#include "../src/projectcollaboration.h"
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

class MockReply : public QNetworkReply
{
public:
    MockReply(const QNetworkRequest &request, int status, const QByteArray &data, QObject *parent)
        : QNetworkReply(parent), m_data(data)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(ReadOnly | Unbuffered);
        QTimer::singleShot(0, this, [this]() { emit readyRead(); emit finished(); });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_data.size() - m_pos + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 count = qMin(maxSize, qint64(m_data.size()) - m_pos);
        if (count <= 0) return -1;
        memcpy(data, m_data.constData() + m_pos, size_t(count));
        m_pos += count;
        return count;
    }
private:
    QByteArray m_data;
    qint64 m_pos = 0;
};

class MockNetwork : public QNetworkAccessManager
{
public:
    struct Response { int status; QByteArray body; };
    QList<Response> responses;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;
    QList<Operation> operations;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoing) override {
        requests.append(request);
        bodies.append(outgoing ? outgoing->readAll() : QByteArray());
        operations.append(op);
        if (responses.isEmpty()) return new MockReply(request, 500, QByteArray(), this);
        const Response response = responses.takeFirst();
        return new MockReply(request, response.status, response.body, this);
    }
};

class CollaborationTests : public QObject
{
    Q_OBJECT
private slots:
    void chatAndCsrf();
    void inviteFailureKeepsInput();
    void commentsAndHistory();
    void historyPreviews();
    void sessionChangeAndHttpError();
    void commentsReplyAndMissingCsrf();
};

void CollaborationTests::chatAndCsrf()
{
    MockNetwork network;
    QUrl server(QStringLiteral("https://example.test"));
    QByteArray cookie("session=test");
    ProjectCollaboration client(&network, &server, &cookie);
    network.responses.append({200, "[{\"content\":\"hello\",\"user\":{\"first_name\":\"Ada\"}}]"});
    client.open(QStringLiteral("abc"), QStringLiteral("chat"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(client.entries().size(), 1);
    QVERIFY(client.entries().first().toMap().value(QStringLiteral("body")).toString().contains(QStringLiteral("Ada")));
    network.responses.append({200, "<meta name=\"ol-csrfToken\" content=\"token\">"});
    network.responses.append({204, ""});
    network.responses.append({200, "[]"});
    QSignalSpy sent(&client, SIGNAL(submitted()));
    client.submit(QStringLiteral("Hi"), QString(), false);
    QTRY_COMPARE(sent.count(), 1);
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requests.at(2).url().path(), QStringLiteral("/project/abc/messages"));
    QCOMPARE(network.requests.at(2).rawHeader("X-CSRF-Token"), QByteArray("token"));
    QCOMPARE(network.requests.at(2).rawHeader("Cookie"), cookie);
    QCOMPARE(QJsonDocument::fromJson(network.bodies.at(2)).object().value(QStringLiteral("content")).toString(), QStringLiteral("Hi"));
    QCOMPARE(network.requests.at(2).attribute(QNetworkRequest::FollowRedirectsAttribute).toBool(), false);
}

void CollaborationTests::inviteFailureKeepsInput()
{
    MockNetwork network;
    QUrl server(QStringLiteral("https://example.test"));
    QByteArray cookie("session=test");
    ProjectCollaboration client(&network, &server, &cookie);
    network.responses.append({200, "{\"members\":[{\"first_name\":\"Ada\",\"email\":\"ada@example.test\",\"privileges\":\"readOnly\"},{\"name\":\"Grace\",\"privileges\":\"readAndWrite\"},{\"email\":\"owner@example.test\",\"privilegeLevel\":\"owner\"},{\"name\":\"Unknown\",\"privileges\":\"other\"}]}"});
    client.open(QStringLiteral("abc"), QStringLiteral("share"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(client.entries().at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Ada"));
    QCOMPARE(client.entries().at(0).toMap().value(QStringLiteral("email")).toString(), QStringLiteral("ada@example.test"));
    QCOMPARE(client.entries().at(0).toMap().value(QStringLiteral("access")).toString(), QStringLiteral("Can view"));
    QCOMPARE(client.entries().at(1).toMap().value(QStringLiteral("access")).toString(), QStringLiteral("Can edit"));
    QCOMPARE(client.entries().at(2).toMap().value(QStringLiteral("access")).toString(), QStringLiteral("Owner"));
    QCOMPARE(client.entries().at(3).toMap().value(QStringLiteral("access")).toString(), QStringLiteral("Access not specified"));
    network.responses.append({200, "<meta name='ol-csrfToken' content='token'>"});
    network.responses.append({200, "{\"invite\":null,\"error\":\"cannot_invite_self\"}"});
    QSignalSpy sent(&client, SIGNAL(submitted()));
    client.submit(QStringLiteral("user@example.test"), QString(), true);
    QTRY_VERIFY(!client.busy());
    QCOMPARE(sent.count(), 0);
    QVERIFY(client.error().contains(QStringLiteral("cannot_invite_self")));
    const QJsonObject body = QJsonDocument::fromJson(network.bodies.at(2)).object();
    QCOMPARE(body.value(QStringLiteral("privileges")).toString(), QStringLiteral("readOnly"));
}

void CollaborationTests::commentsAndHistory()
{
    MockNetwork network;
    QUrl server(QStringLiteral("https://example.test"));
    QByteArray cookie("session=test");
    ProjectCollaboration client(&network, &server, &cookie);
    network.responses.append({200, "{\"t1\":{\"resolved\":false,\"messages\":[{\"content\":\"Comment\"}]}}"});
    client.open(QStringLiteral("abc"), QStringLiteral("comments"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(client.entries().first().toMap().value(QStringLiteral("target")).toString(), QStringLiteral("t1"));
    client.submit(QStringLiteral("Reply"), QStringLiteral("../escape"), false);
    QVERIFY(!client.error().isEmpty());
    QCOMPARE(network.requests.size(), 1);
    network.responses.append({200, "{\"updates\":[{\"fromV\":1,\"toV\":2,\"meta\":{\"end_ts\":1512383572877,\"users\":[{\"first_name\":\"Ada\"}]},\"pathnames\":[\"main.tex\"],\"project_ops\":[{\"add\":{\"pathname\":\"new.tex\"}},{\"remove\":{\"pathname\":\"old.tex\"}},{\"rename\":{\"pathname\":\"a.tex\",\"newPathname\":\"b.tex\"}}]}]}"});
    client.open(QStringLiteral("abc"), QStringLiteral("history"));
    QTRY_VERIFY(!client.busy());
    const QString text = client.entries().first().toMap().value(QStringLiteral("body")).toString();
    QVERIFY(text.contains(QStringLiteral("Ada")));
    const QVariantList changes = client.entries().first().toMap().value(QStringLiteral("changes")).toList();
    QCOMPARE(changes.size(), 4);
    QCOMPARE(changes.at(0).toMap().value(QStringLiteral("path")).toString(), QStringLiteral("main.tex"));
    QCOMPARE(changes.at(1).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("Created"));
    QCOMPARE(changes.at(2).toMap().value(QStringLiteral("action")).toString(), QStringLiteral("Deleted"));
    QCOMPARE(changes.at(3).toMap().value(QStringLiteral("path")).toString(), QStringLiteral("a.tex → b.tex"));
}

void CollaborationTests::sessionChangeAndHttpError()
{
    MockNetwork network;
    QUrl server(QStringLiteral("https://example.test"));
    QByteArray cookie("session=test");
    ProjectCollaboration client(&network, &server, &cookie);
    network.responses.append({403, "{\"error\":\"forbidden\"}"});
    client.open(QStringLiteral("abc"), QStringLiteral("comments"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("403")));
    cookie = "session=other";
    client.refresh();
    QVERIFY(client.error().contains(QStringLiteral("session"), Qt::CaseInsensitive));
    QCOMPARE(network.requests.size(), 1);
}

void CollaborationTests::historyPreviews()
{
    MockNetwork network;
    QUrl server(QStringLiteral("https://example.test"));
    QByteArray cookie("session=test");
    ProjectCollaboration client(&network, &server, &cookie);
    network.responses.append({200, R"({"updates":[{"fromV":1,"toV":2,"meta":{},"pathnames":["a & b.tex"],"project_ops":[]},{"fromV":2,"toV":3,"meta":{},"pathnames":["long.tex"],"project_ops":[]},{"fromV":3,"toV":4,"meta":{},"pathnames":["missing.tex"],"project_ops":[]}]})"});
    network.responses.append({200, R"({"diff":[{"u":"unchanged"},{"d":"old"},{"i":"new"}]})"});
    network.responses.append({200, QByteArray("{\"diff\":[{\"i\":\"") + QByteArray(201, 'x') + "\"}]}"});
    network.responses.append({404, "{}"});
    QAbstractItemModel *model = client.entriesModel();
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    QSignalSpy changes(model, &QAbstractItemModel::dataChanged);
    client.open(QStringLiteral("abc"), QStringLiteral("history"));
    QTRY_COMPARE(network.requests.size(), 4);
    QTRY_VERIFY(!client.entries().at(2).toMap().value(QStringLiteral("previewError")).toString().isEmpty());
    const QVariantMap shortEntry = client.entries().at(0).toMap();
    const QVariantList preview = shortEntry.value(QStringLiteral("preview")).toList();
    QCOMPARE(preview.size(), 3);
    QCOMPARE(preview.at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("old"));
    QCOMPARE(preview.at(2).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("new"));
    QCOMPARE(QUrlQuery(network.requests.at(1).url()).queryItemValue(QStringLiteral("pathname")),
             QStringLiteral("a & b.tex"));
    QVERIFY(client.entries().at(1).toMap().value(QStringLiteral("preview")).toList().isEmpty());
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(resets.count(), 2);
    QCOMPARE(changes.count(), 3);
    QCOMPARE(model->data(model->index(0, 0), Qt::UserRole + 1).toMap(), shortEntry);
    for (int i = 0; i < changes.count(); ++i)
        QCOMPARE(changes.at(i).at(0).value<QModelIndex>().row(), i);
}

void CollaborationTests::commentsReplyAndMissingCsrf()
{
    MockNetwork network;
    QUrl server(QStringLiteral("https://example.test"));
    QByteArray cookie("session=test");
    ProjectCollaboration client(&network, &server, &cookie);
    network.responses.append({200, "{\"t1\":{\"messages\":[]}}"});
    client.open(QStringLiteral("abc"), QStringLiteral("comments"));
    QTRY_VERIFY(!client.busy());
    network.responses.append({200, "<html>login</html>"});
    QSignalSpy sent(&client, SIGNAL(submitted()));
    client.submit(QStringLiteral("reply"), QStringLiteral("t1"), false);
    QTRY_VERIFY(!client.busy());
    QCOMPARE(sent.count(), 0);
    QCOMPARE(network.requests.size(), 2);
    QVERIFY(client.error().contains(QStringLiteral("CSRF")));

    network.responses.append({200, "<meta name='ol-csrfToken' content='token'>"});
    network.responses.append({204, ""});
    network.responses.append({200, "{}"});
    client.submit(QStringLiteral("reply"), QStringLiteral("t1"), false);
    QTRY_COMPARE(sent.count(), 1);
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requests.at(3).url().path(), QStringLiteral("/project/abc/thread/t1/messages"));
}

QTEST_GUILESS_MAIN(CollaborationTests)
#include "projectcollaboration_tests.moc"
