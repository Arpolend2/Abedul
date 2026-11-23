#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>

#include "reversoclient.h"

class ReversoFakeReply : public QNetworkReply
{
public:
    ReversoFakeReply(const QNetworkRequest &request, const QByteArray &body, int status, QObject *parent)
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
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

class ReversoFakeNetwork : public QNetworkAccessManager
{
public:
    QByteArray response;
    QList<QByteArray> responses;
    QByteArray payload;
    QNetworkRequest lastRequest;
    Operation operation = UnknownOperation;
    int status = 200;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *body) override
    {
        operation = op;
        lastRequest = request;
        payload = body ? body->readAll() : QByteArray();
        const QByteArray result = responses.isEmpty() ? response : responses.takeFirst();
        return new ReversoFakeReply(request, result, status, this);
    }
};

class ReversoClientTests : public QObject
{
    Q_OBJECT
private slots:
    void extractsWord();
    void acceptsOnlyPlainText();
    void requestsTranslationsAndContext();
    void reportsFailuresAndClearsResults();
    void translatesSentencesAndLoadsContext();
    void liveTranslation();
};

void ReversoClientTests::extractsWord()
{
    ReversoClient client;
    const QString text = QString::fromUtf8("Привет, well-being! don't");
    QCOMPARE(client.wordAtCursor(text, 3), QString::fromUtf8("Привет"));
    QCOMPARE(client.wordAtCursor(text, text.indexOf(QStringLiteral("being"))), QStringLiteral("well-being"));
    QCOMPARE(client.wordAtCursor(text, text.size()), QStringLiteral("don't"));
    QVERIFY(client.wordAtCursor(text, text.indexOf(QLatin1Char('!')) + 1).isEmpty());
    QVERIFY(client.wordAtCursor(text, -1).isEmpty());
}

void ReversoClientTests::acceptsOnlyPlainText()
{
    ReversoClient client;
    const auto at = [&client](const QString &text, const QString &word) {
        const int position = text.indexOf(word);
        Q_ASSERT(position >= 0);
        return client.plainTextAtCursor(text, position + 1, position, position);
    };
    QCOMPARE(at(QStringLiteral("Hello, friend."), QStringLiteral("friend")), QStringLiteral("friend"));
    QCOMPARE(at(QStringLiteral("\\textbf{Hello} there"), QStringLiteral("Hello")), QStringLiteral("Hello"));
    QCOMPARE(at(QStringLiteral("\\section[short]{Long title}"), QStringLiteral("Long")), QStringLiteral("Long"));
    for (const QString &source : {
             QStringLiteral("$hello$"), QStringLiteral("$$hello$$"), QStringLiteral("\\(hello\\)"),
             QStringLiteral("\\[hello\\]"), QStringLiteral("\\begin{align*}\nhello\n\\end{align*}"),
             QStringLiteral("\\begin{alignat}\nhello\n\\end{alignat}"),
             QStringLiteral("$unfinished hello"), QStringLiteral("% hello\nplain"),
             QStringLiteral("\\ref{hello}"), QStringLiteral("\\verb|hello|"),
             QStringLiteral("\\newcommand{\\foo}[2]{hello}"),
             QStringLiteral("\\begin{verbatim}\nhello\n\\end{verbatim}"),
             QStringLiteral("hello = 5"), QStringLiteral("hello^2")
         }) {
        QVERIFY2(at(source, QStringLiteral("hello")).isEmpty(), qPrintable(source));
    }
    QVERIFY(at(QStringLiteral("\\section{Hello}"), QStringLiteral("section")).isEmpty());
    QCOMPARE(at(QStringLiteral("\\$ money and hello"), QStringLiteral("hello")), QStringLiteral("hello"));
    QCOMPARE(at(QStringLiteral("before $x$ hello"), QStringLiteral("hello")), QStringLiteral("hello"));
    const QString text = QStringLiteral("Hello\nworld $x$ end");
    QCOMPARE(client.plainTextAtCursor(text, 11, 0, 11), QStringLiteral("Hello\nworld"));
    QVERIFY(client.plainTextAtCursor(text, 15, 0, 15).isEmpty());
    QVERIFY(client.plainTextAtCursor(text, 0, 5, 6).isEmpty());
}

void ReversoClientTests::requestsTranslationsAndContext()
{
    ReversoFakeNetwork network;
    network.response = R"({"success":true,"error":false,"sources":[{"translations":[
        {"translation":"happy","contexts":[{"source":"<em>happy</em> &amp; well","target":"<em>glad</em>"}]},
        {"translation":"glad"},{"translation":"happy"}]}]})";
    ReversoClient client(nullptr, &network);
    client.request(QStringLiteral("word"), QStringLiteral("ru"), QStringLiteral("en"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(network.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(network.lastRequest.url().host(), QStringLiteral("cps.reverso.net"));
    QCOMPARE(network.lastRequest.url().path(), QStringLiteral("/api2/TranslateWord"));
    const QJsonObject payload = QJsonDocument::fromJson(network.payload).object();
    QCOMPARE(payload.value(QStringLiteral("Source")).toString(), QStringLiteral("word"));
    QCOMPARE(payload.value(QStringLiteral("Direction")).toString(), QStringLiteral("ru-en"));
    QCOMPARE(client.translations(), QStringList({QStringLiteral("happy"), QStringLiteral("glad")}));
    QCOMPARE(client.examples().size(), 1);
    const QVariantMap example = client.examples().first().toMap();
    QCOMPARE(example.value(QStringLiteral("source")).toString(), QStringLiteral("happy & well"));
    QCOMPARE(example.value(QStringLiteral("target")).toString(), QStringLiteral("glad"));
    QVERIFY(client.error().isEmpty());
}

void ReversoClientTests::translatesSentencesAndLoadsContext()
{
    ReversoFakeNetwork network;
    network.responses << R"({"success":true,"error":false,"translation":"translated sentence"})"
                      << R"({"success":true,"error":false,"sources":[{"translations":[
                           {"translation":"...","contexts":[{"source":"Example","target":"Translation"}]}]}]})";
    ReversoClient client(nullptr, &network);
    client.request(QStringLiteral("this is a long sentence"), QStringLiteral("en"), QStringLiteral("ru"));
    QTRY_COMPARE(client.examples().size(), 1);
    QVERIFY(!client.busy());
    QCOMPARE(client.translations(), QStringList({QStringLiteral("translated sentence")}));
    QCOMPARE(network.lastRequest.url().path(), QStringLiteral("/api2/TranslateSimple"));
    QVERIFY(client.error().isEmpty());
}

void ReversoClientTests::reportsFailuresAndClearsResults()
{
    ReversoFakeNetwork network;
    ReversoClient client(nullptr, &network);
    client.request(QStringLiteral("test"), QStringLiteral("en"), QStringLiteral("en"));
    QVERIFY(!client.error().isEmpty());
    network.status = 403;
    client.request(QStringLiteral("test"), QStringLiteral("en"), QStringLiteral("ru"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("403")));
    network.status = 200;
    network.response = "not json";
    client.request(QStringLiteral("test"), QStringLiteral("en"), QStringLiteral("ru"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(!client.error().isEmpty());
    QVERIFY(client.translations().isEmpty());
    QVERIFY(client.examples().isEmpty());
}

void ReversoClientTests::liveTranslation()
{
    if (!qEnvironmentVariableIsSet("ABEDUL_TEST_REVERSO_LIVE"))
        QSKIP("Set ABEDUL_TEST_REVERSO_LIVE to test the external service.");
    ReversoClient client;
    client.request(QStringLiteral("happy"), QStringLiteral("en"), QStringLiteral("ru"));
    QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 20000);
    QVERIFY2(client.error().isEmpty(), qPrintable(client.error()));
    QVERIFY(!client.translations().isEmpty());
    QVERIFY(!client.examples().isEmpty());
    client.request(QStringLiteral("I am happy to see you today"), QStringLiteral("en"), QStringLiteral("ru"));
    QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 20000);
    QVERIFY2(client.error().isEmpty(), qPrintable(client.error()));
    QVERIFY(!client.translations().isEmpty());
    QVERIFY(!client.examples().isEmpty());
}

QTEST_MAIN(ReversoClientTests)
#include "reversoclient_tests.moc"
