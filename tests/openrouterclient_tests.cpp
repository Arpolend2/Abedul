#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include "openrouterclient.h"

class FakeReply : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, const QByteArray &body, int status, QObject *parent,
              const QByteArray &retryAfter = QByteArray())
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!retryAfter.isEmpty())
            setRawHeader("Retry-After", retryAfter);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this]() {
            if (!m_done) {
                m_done = true;
                emit readyRead();
                emit finished();
            }
        });
    }

    void abort() override
    {
        if (m_done)
            return;
        m_done = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, qint64(m_body.size()) - m_offset);
        if (count <= 0)
            return -1;
        memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
    bool m_done = false;
};

class FakeNetwork : public QNetworkAccessManager
{
public:
    QByteArray response;
    int status = 200;
    QByteArray quotaResponse = R"({"data":{"free_model_daily_requests":{"remaining":10}}})";
    int quotaStatus = 200;
    QByteArray retryAfter;
    QByteArray payload;
    QList<QByteArray> responses;
    QList<int> statuses;
    QList<QByteArray> payloads;
    int requestCount = 0;
    QNetworkRequest lastRequest;
    QPointer<QNetworkReply> lastReply;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing) override
    {
        lastRequest = request;
        ++requestCount;
        payload = outgoing ? outgoing->readAll() : QByteArray();
        payloads.append(payload);
        const bool quota = request.url().path() == QLatin1String("/api/v1/key");
        lastReply = new FakeReply(request, quota ? quotaResponse : (responses.isEmpty() ? response : responses.takeFirst()),
                                  quota ? quotaStatus : (statuses.isEmpty() ? status : statuses.takeFirst()),
                                  this, retryAfter);
        return lastReply;
    }
};

class OpenRouterTests : public QObject
{
    Q_OBJECT
private slots:
    void signInPersistsAndUsesPkce();
    void browserCallbackRequiresMatchingState();
    void freeGenerationAndErrors();
    void handlesEmptyAndProviderResponses();
    void editRequestIncludesWholeDocument();
    void completionRequestIncludesCursorContext();
    void completionReportsErrorsAndCanBeCancelled();
    void freeQuotaIsCheckedForBothModes();
    void completionFallsBackOnProviderRateLimit();
    void retriesRateLimitsUntilSuccessAndCanCancel();
    void retriesAutomaticallyAndStopsWhenQuotaExhausted();
};

void OpenRouterTests::signInPersistsAndUsesPkce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    QVERIFY(!client.authenticated());
    client.beginSignIn();
    const QUrl url(client.authorizationUrl());
    QCOMPARE(url.host(), QStringLiteral("openrouter.ai"));
    QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    QCOMPARE(network.lastRequest.url().path(), QStringLiteral("/api/v1/auth/keys"));
    const QJsonObject sent = QJsonDocument::fromJson(network.payload).object();
    const QByteArray verifier = sent.value(QStringLiteral("code_verifier")).toString().toLatin1();
    const QByteArray challenge = QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge")).toLatin1(), challenge);
    OpenRouterClient restored(nullptr, &network);
    QVERIFY(restored.authenticated());
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("openrouter/session"));
    QVERIFY(QFile::exists(path));
    QVERIFY(!(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::ReadOther)));
    restored.signOut();
    QVERIFY(!QFile::exists(path));
    QVERIFY(!restored.authenticated());
}

void OpenRouterTests::browserCallbackRequiresMatchingState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    QUrlQuery authorization(QUrl(client.authorizationUrl()));
    QCOMPARE(authorization.queryItemValue(QStringLiteral("callback_url")),
             QStringLiteral("http://localhost:49152/abedul-openrouter"));
    const QString state = authorization.queryItemValue(QStringLiteral("state"));
    QVERIFY(!state.isEmpty());
    QVERIFY(!client.handleAuthorizationUrl(QUrl(QStringLiteral("https://openrouter.ai/"))));
    QVERIFY(client.handleAuthorizationUrl(QUrl(QStringLiteral(
        "http://localhost:49152/abedul-openrouter?state=wrong&code=test-code"))));
    QVERIFY(!client.error().isEmpty());
    QVERIFY(!client.busy());
    QUrl callback(QStringLiteral("http://localhost:49152/abedul-openrouter"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("code"), QStringLiteral("test-code"));
    callback.setQuery(query);
    QVERIFY(client.handleAuthorizationUrl(callback));
    QTRY_VERIFY(client.authenticated());
    QCOMPARE(QJsonDocument::fromJson(network.payload).object()
             .value(QStringLiteral("code")).toString(), QStringLiteral("test-code"));
    client.signOut();
}

void OpenRouterTests::freeGenerationAndErrors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));
    network.response = R"({"choices":[{"message":{"content":"\\alpha"}}]})";
    const int beforeSuccess = network.requestCount;
    client.request(QStringLiteral("Generate LaTeX"), QStringLiteral("context"));
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(network.requestCount, beforeSuccess + 1);
    QCOMPARE(results.takeFirst().at(0).toString(), QStringLiteral("\\alpha"));
    const QJsonObject sent = QJsonDocument::fromJson(network.payload).object();
    QCOMPARE(sent.value(QStringLiteral("model")).toString(), QStringLiteral("particle-ai/particle-1:free"));
    QCOMPARE(sent.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("enabled")).toBool(true), false);
    QCOMPARE(sent.value(QStringLiteral("max_tokens")).toInt(), 4096);
    QCOMPARE(network.lastRequest.url().path(), QStringLiteral("/api/v1/chat/completions"));
    QVERIFY(sent.value(QStringLiteral("messages")).toArray().at(1).toObject()
            .value(QStringLiteral("content")).toString().contains(QStringLiteral("context")));
    network.status = 429;
    network.response = R"({"error":{"message":"Provider returned error"}})";
    network.quotaResponse = R"({"data":{"free_model_daily_requests":{"remaining":0}}})";
    const int beforeRateLimit = network.requestCount;
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, beforeRateLimit + 2);
    QVERIFY(client.error().contains(QStringLiteral("429")));
    QCOMPARE(results.count(), 0);
    network.status = 200;
    client.request(QStringLiteral("Cancel"), QString());
    QVERIFY(client.busy());
    client.cancel();
    QVERIFY(!client.busy());
    QVERIFY(client.error().isEmpty());
    QCOMPARE(results.count(), 0);

    client.request(QStringLiteral("Timeout"), QString());
    QVERIFY(client.busy());
    QTimer *timeout = network.lastReply->findChild<QTimer *>();
    QVERIFY(timeout);
    QVERIFY(QMetaObject::invokeMethod(timeout, "timeout"));
    QVERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("timed out")));
    QCOMPARE(results.count(), 0);
    client.signOut();
}

void OpenRouterTests::handlesEmptyAndProviderResponses()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));

    network.responses << R"({"choices":[{"finish_reason":"length","message":{"content":null,"reasoning":"not output"}}]})"
                      << R"({"choices":[{"finish_reason":"stop","message":{"content":"\\alpha"}}]})";
    int before = network.requestCount;
    client.request(QStringLiteral("Generate LaTeX"), QStringLiteral("context"));
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(results.takeFirst().at(0).toString(), QStringLiteral("\\alpha"));
    QCOMPARE(network.requestCount, before + 2);
    QVERIFY(QJsonDocument::fromJson(network.payload).object()
            .value(QStringLiteral("messages")).toArray().at(1).toObject()
            .value(QStringLiteral("content")).toString().contains(QStringLiteral("context")));

    network.response = R"({"choices":[{"finish_reason":"length","message":{"content":""}}]})";
    before = network.requestCount;
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 2);
    QVERIFY(client.error().contains(QStringLiteral("response limit")));
    QCOMPARE(results.count(), 0);

    network.response = R"({"choices":[{"finish_reason":"length","message":{"content":"partial"}}]})";
    before = network.requestCount;
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(client.error().contains(QStringLiteral("response limit")));
    QCOMPARE(results.count(), 0);

    network.response = R"({"error":{"message":"provider unavailable"},"choices":[{"message":{"content":"partial"}}]})";
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("provider unavailable")));
    QCOMPARE(results.count(), 0);

    network.response = R"({"choices":[{"error":{"message":"generation failed"},"message":{"content":"partial"}}]})";
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("generation failed")));
    QCOMPARE(results.count(), 0);

    network.response = R"({"choices":[{"error":{},"message":{"content":"partial"}}]})";
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("model error")));
    QCOMPARE(results.count(), 0);

    network.response = R"({"choices":[{"finish_reason":"content_filter","message":{"content":null}}]})";
    before = network.requestCount;
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(client.error().contains(QStringLiteral("blocked")));
    QCOMPARE(results.count(), 0);

    network.response = R"({"choices":[{"finish_reason":"stop","message":{"content":""}}]})";
    before = network.requestCount;
    client.request(QStringLiteral("Again"), QString());
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 2);
    QVERIFY(client.error().contains(QStringLiteral("no text after retry")));
    QCOMPARE(results.count(), 0);

    network.responses << R"({"choices":[{"finish_reason":"stop","message":{"content":""}}]})"
                      << R"({"choices":[{"finish_reason":"stop","message":{"content":"late"}}]})";
    before = network.requestCount;
    const QMetaObject::Connection cancelRetry = connect(&client, &OpenRouterClient::busyChanged,
                                                        &client, [&]() {
        if (client.busy() && network.requestCount == before + 2)
            client.cancel();
    });
    client.request(QStringLiteral("Cancel retry"), QString());
    QTRY_COMPARE(network.requestCount, before + 2);
    QVERIFY(!client.busy());
    QVERIFY(client.error().isEmpty());
    QCOMPARE(results.count(), 0);
    disconnect(cancelRetry);
    client.signOut();
}

void OpenRouterTests::editRequestIncludesWholeDocument()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    network.response = R"({"choices":[{"message":{"content":"{\"action\":\"replace\",\"old_text\":\"old\",\"new_text\":\"new\"}"}}]})";
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));
    const QString document = QStringLiteral("start\n") + QStringLiteral("x").repeated(15000)
            + QStringLiteral("\n\\section{old}\n");
    client.requestEdit(QStringLiteral("Change the section"), document);
    QTRY_COMPARE(results.count(), 1);
    const QJsonObject request = QJsonDocument::fromJson(network.payload).object();
    QCOMPARE(request.value(QStringLiteral("model")).toString(), QStringLiteral("particle-ai/particle-1:free"));
    QCOMPARE(request.value(QStringLiteral("max_tokens")).toInt(), 4096);
    QCOMPARE(request.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("enabled")).toBool(true), false);
    const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
    const QJsonObject data = QJsonDocument::fromJson(messages.at(1).toObject()
            .value(QStringLiteral("content")).toString().toUtf8()).object();
    QCOMPARE(data.value(QStringLiteral("request")).toString(), QStringLiteral("Change the section"));
    QCOMPARE(data.value(QStringLiteral("document")).toString(), document);
    QVERIFY(results.at(0).at(0).toString().contains(QStringLiteral("\"old_text\":\"old\"")));
    client.signOut();
}

void OpenRouterTests::completionRequestIncludesCursorContext()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    network.response = R"({"choices":[{"message":{"content":"{\"action\":\"replace\",\"old_text\":\"\\\\beg\",\"new_text\":\"\\\\begin{}\"}"}}]})";
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));
    const int beforeSuccess = network.requestCount;
    client.requestCompletion(QStringLiteral("\\beg"), QStringLiteral(" after"));
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(network.requestCount, beforeSuccess + 1);
    const QJsonObject request = QJsonDocument::fromJson(network.payload).object();
    QCOMPARE(request.value(QStringLiteral("model")).toString(), QStringLiteral("particle-ai/particle-1:free"));
    QCOMPARE(request.value(QStringLiteral("provider")).toObject().value(QStringLiteral("sort")).toString(),
             QStringLiteral("throughput"));
    QVERIFY(request.value(QStringLiteral("provider")).toObject().value(QStringLiteral("require_parameters")).toBool());
    QCOMPARE(request.value(QStringLiteral("max_tokens")).toInt(), 512);
    QCOMPARE(request.value(QStringLiteral("response_format")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("json_object"));
    QCOMPARE(request.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("enabled")).toBool(true), false);
    const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
    const QJsonObject data = QJsonDocument::fromJson(messages.at(1).toObject()
            .value(QStringLiteral("content")).toString().toUtf8()).object();
    QCOMPARE(data.value(QStringLiteral("before")).toString(), QStringLiteral("\\beg"));
    QCOMPARE(data.value(QStringLiteral("after")).toString(), QStringLiteral(" after"));
    QVERIFY(results.at(0).at(0).toString().contains(QStringLiteral("\"action\":\"replace\"")));
    client.signOut();
}

void OpenRouterTests::completionReportsErrorsAndCanBeCancelled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));

    network.response = R"({"choices":[{"finish_reason":"length","message":{"content":null,"reasoning":"not output"}}]})";
    int before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(client.error().contains(QStringLiteral("response limit")));
    QCOMPARE(results.count(), 0);

    network.response = R"({"choices":[{"finish_reason":"stop","message":{"content":""}}]})";
    before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(client.error().contains(QStringLiteral("returned no text")));
    QCOMPARE(results.count(), 0);

    network.status = 404;
    before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(client.error().contains(QStringLiteral("404")));

    network.status = 429;
    network.response = R"({"error":{"message":"Rate limit exceeded, try again later."}})";
    network.retryAfter = "42";
    network.quotaResponse = R"({"data":{"free_model_daily_requests":{"remaining":0}}})";
    before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 2);
    QVERIFY(client.error().contains(QStringLiteral("429")));
    QVERIFY(client.error().contains(QStringLiteral("Rate limit exceeded")));
    QVERIFY(client.error().contains(QStringLiteral("Autocomplete")));
    QVERIFY(client.error().contains(QStringLiteral("42 seconds")));
    QCOMPARE(results.count(), 0);

    network.status = 200;
    network.retryAfter.clear();
    before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QVERIFY(client.busy());
    client.cancel();
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(!client.busy());
    QVERIFY(client.error().isEmpty());
    QCOMPARE(results.count(), 0);

    before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTimer *timeout = network.lastReply->findChild<QTimer *>();
    QVERIFY(timeout);
    QVERIFY(timeout->interval() > 0 && timeout->interval() <= 8000);
    QVERIFY(QMetaObject::invokeMethod(timeout, "timeout"));
    QVERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(client.error().contains(QStringLiteral("timed out")));
    QCOMPARE(results.count(), 0);

    network.response = "not json";
    before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 1);
    QVERIFY(client.error().contains(QStringLiteral("invalid response")));
    QCOMPARE(results.count(), 0);
    client.signOut();
}

void OpenRouterTests::freeQuotaIsCheckedForBothModes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));

    network.status = 429;
    network.response = R"({"error":{"message":"Rate limit exceeded"}})";
    network.quotaResponse = R"({"data":{"free_model_daily_requests":{"remaining":0}}})";
    int before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 2);
    QCOMPARE(network.lastRequest.url().path(), QStringLiteral("/api/v1/key"));
    QCOMPARE(network.lastRequest.rawHeader("Authorization"), QByteArray("Bearer sk-or-test-key"));
    QVERIFY(client.error().contains(QStringLiteral("daily free model limit")));
    QCOMPARE(results.count(), 0);

    before = network.requestCount;
    client.requestEdit(QStringLiteral("Edit"), QStringLiteral("document"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.requestCount, before + 2);
    QVERIFY(client.error().contains(QStringLiteral("daily free model limit")));

    network.quotaResponse = R"({"data":{}})";
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("Could not check")));

    network.quotaStatus = 429;
    network.quotaResponse = R"({"error":{"message":"Rate limited"}})";
    client.requestEdit(QStringLiteral("Edit"), QStringLiteral("document"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("429")));
    QVERIFY(client.error().contains(QStringLiteral("Rate limited")));
    QVERIFY(client.error().contains(QStringLiteral("Limit check")));
    client.signOut();
}

void OpenRouterTests::completionFallsBackOnProviderRateLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));

    network.payloads.clear();
    network.statuses << 429 << 429 << 200;
    network.responses << R"({"error":{"message":"Provider returned error"}})"
                      << R"({"error":{"message":"Provider returned error"}})"
                      << R"({"choices":[{"message":{"content":"{\"action\":\"insert\",\"new_text\":\"x\"}"}}]})";
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_COMPARE(network.requestCount, 3);
    QTimer *timer = client.findChild<QTimer *>(QStringLiteral("openRouterRateLimitRetry"));
    QVERIFY(timer && timer->isActive());
    timer->stop();
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
    QTRY_COMPARE(network.requestCount, 5);
    timer = client.findChild<QTimer *>(QStringLiteral("openRouterRateLimitRetry"));
    QVERIFY(timer && timer->isActive());
    timer->stop();
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(network.payloads.size(), 5);
    const QStringList expected{QStringLiteral("particle-ai/particle-1:free"),
                               QStringLiteral("google/gemma-4-26b-a4b-it:free"),
                               QStringLiteral("inclusionai/ling-3.2-flash:free")};
    for (int i = 0; i < expected.size(); ++i)
        QCOMPARE(QJsonDocument::fromJson(network.payloads.at(i * 2)).object()
                 .value(QStringLiteral("model")).toString(), expected.at(i));
    QVERIFY(client.error().isEmpty());

    network.payloads.clear();
    network.status = 429;
    network.response = R"({"error":{"message":"Rate limit exceeded"}})";
    network.quotaResponse = R"({"data":{"free_model_daily_requests":{"remaining":0}}})";
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.payloads.size(), 2);
    QVERIFY(client.error().contains(QStringLiteral("429")));
    client.signOut();
}

void OpenRouterTests::retriesRateLimitsUntilSuccessAndCanCancel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));

    network.statuses << 429 << 429 << 429 << 429 << 200;
    for (int i = 0; i < 4; ++i)
        network.responses << R"({"error":{"message":"Rate limit exceeded"}})";
    network.responses << R"({"choices":[{"message":{"content":"success"}}]})";
    network.retryAfter = "3";
    int before = network.requestCount;
    client.requestEdit(QStringLiteral("Edit"), QStringLiteral("document"));
    for (int i = 0; i < 4; ++i) {
        QTRY_COMPARE(network.requestCount, before + i * 2 + 2);
        QVERIFY(client.busy());
        QTimer *timer = client.findChild<QTimer *>(QStringLiteral("openRouterRateLimitRetry"));
        QVERIFY(timer && timer->isActive());
        QVERIFY(timer->interval() >= 3000);
        timer->stop();
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
    }
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(results.takeFirst().at(0).toString(), QStringLiteral("success"));
    QVERIFY(!client.busy());
    QVERIFY(client.error().isEmpty());

    network.status = 429;
    network.response = R"({"error":{"message":"Rate limit exceeded"}})";
    before = network.requestCount;
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_COMPARE(network.requestCount, before + 2);
    QVERIFY(client.busy());
    client.cancel();
    QVERIFY(!client.busy());
    QTest::qWait(50);
    QCOMPARE(network.requestCount, before + 2);
    QVERIFY(client.error().isEmpty());
    client.signOut();
}

void OpenRouterTests::retriesAutomaticallyAndStopsWhenQuotaExhausted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    FakeNetwork network;
    network.response = R"({"key":"sk-or-test-key"})";
    OpenRouterClient client(nullptr, &network);
    client.beginSignIn();
    client.finishSignIn(QStringLiteral("test-code"));
    QTRY_VERIFY(client.authenticated());

    network.status = 429;
    network.response = R"({"error":{"message":"Rate limit exceeded"}})";
    const int before = network.requestCount;
    int idleSignals = 0;
    connect(&client, &OpenRouterClient::busyChanged, &client, [&]() {
        if (!client.busy())
            ++idleSignals;
    });
    client.requestCompletion(QStringLiteral("before"), QStringLiteral("after"));
    QTRY_COMPARE(network.requestCount, before + 2);
    QTRY_VERIFY(client.findChild<QTimer *>(QStringLiteral("openRouterRateLimitRetry")));
    QVERIFY(client.busy());
    QCOMPARE(idleSignals, 0);
    network.quotaResponse = R"({"data":{"free_model_daily_requests":{"remaining":0}}})";
    QTRY_COMPARE(network.requestCount, before + 4);
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("daily free model limit")));
    QCOMPARE(idleSignals, 1);
    client.signOut();
}

QTEST_MAIN(OpenRouterTests)
#include "openrouterclient_tests.moc"
