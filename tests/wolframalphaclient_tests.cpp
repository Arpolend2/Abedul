#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>

#include "wolframalphaclient.h"

class FakeReply : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, const QByteArray &body, int status, QObject *parent)
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

class FakeNetwork : public QNetworkAccessManager
{
public:
    QByteArray body;
    int status = 200;
    QNetworkRequest lastRequest;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        lastRequest = request;
        return new FakeReply(request, body, status, this);
    }
};

class WolframAlphaClientTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void extractsLatexMathAtCursor();
    void extractsEquationWithoutAlignmentOrLabel();
    void ignoresCommentsAndEscapedDelimiters();
    void sendsEquationAndReadsTextPods();
    void reportsApiErrors();
    void storesAppIdInPrivateAppData();
    void reloadsSettingsAndClearsPreviousResults();

private:
    QTemporaryDir m_dataDirectory;
};

void WolframAlphaClientTests::initTestCase()
{
    QVERIFY(m_dataDirectory.isValid());
    qputenv("XDG_DATA_HOME", m_dataDirectory.path().toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("AbedulTests"));
    QCoreApplication::setApplicationName(QStringLiteral("WolframAlphaClientTests"));
}

void WolframAlphaClientTests::extractsLatexMathAtCursor()
{
    WolframAlphaClient client;
    const QString inlineMath = QStringLiteral("Text $x^2 + 1$ tail");
    QCOMPARE(client.equationAtCursor(inlineMath, inlineMath.indexOf(QStringLiteral("x^2"))),
             QStringLiteral("x^2 + 1"));

    const QString displayMath = QStringLiteral("$$\\int_0^1 x^2 dx$$");
    QCOMPARE(client.equationAtCursor(displayMath, displayMath.indexOf(QStringLiteral("x^2"))),
             QStringLiteral("\\int_0^1 x^2 dx"));

    const QString parenMath = QStringLiteral("\\(a+b=c\\)");
    QCOMPARE(client.equationAtCursor(parenMath, parenMath.indexOf(QLatin1Char('+'))),
             QStringLiteral("a+b=c"));

    const QString bracketMath = QStringLiteral("\\[\\frac{x}{2}=1\\]");
    QCOMPARE(client.equationAtCursor(bracketMath, bracketMath.indexOf(QStringLiteral("x}"))),
             QStringLiteral("\\frac{x}{2}=1"));

    const QString environment = QStringLiteral(
        "\\begin{align*}\nx^2 + y^2 &= 1 \\\\\n2x &= 0\n\\end{align*}");
    QCOMPARE(client.equationAtCursor(environment, environment.indexOf(QStringLiteral("2x"))),
             QStringLiteral("x^2 + y^2 = 1 \\\\\n2x = 0"));
    QVERIFY(client.equationAtCursor(inlineMath, 0).isEmpty());
}

void WolframAlphaClientTests::extractsEquationWithoutAlignmentOrLabel()
{
    WolframAlphaClient client;
    const QString source = QStringLiteral(
        "\\begin{eqnarray}\\label{eq:newtons_law}\n"
        "    && \\dot \\omega = \\frac{\\omega_R^2}{2HP_G(\\omega + \\omega_R)}(P_m - P_l)\n"
        "\\end{eqnarray}");
    const QString expected = QStringLiteral(
        "\\dot \\omega = \\frac{\\omega_R^2}{2HP_G(\\omega + \\omega_R)}(P_m - P_l)");
    QCOMPARE(client.equationAtCursor(source, source.indexOf(QStringLiteral("\\dot"))), expected);
    QCOMPARE(client.equationAtCursor(source, 0), expected);
    QCOMPARE(client.equationAtCursor(source, source.indexOf(QStringLiteral("\\end")) + 3), expected);
    const QString nested = QStringLiteral(
        "\\begin{align}a &= \\begin{matrix}x & y\\end{matrix} % ignored\n"
        "\\label{a} + \\text{A \\& B}\\end{align}");
    QCOMPARE(client.equationAtCursor(nested, nested.indexOf(QStringLiteral("a &"))),
             QStringLiteral("a = \\begin{matrix}x & y\\end{matrix} \n + \\text{A \\& B}"));

    FakeNetwork network;
    network.body = R"({"queryresult":{"success":true,"pods":[{"title":"Result","subpods":[{"plaintext":"ok"}]}]}})";
    WolframAlphaClient sender(nullptr, &network);
    sender.setAppId(QStringLiteral("TEST-APP-ID"));
    sender.request(client.equationAtCursor(source, source.indexOf(QStringLiteral("\\dot"))));
    const QUrlQuery query(network.lastRequest.url());
    QCOMPARE(query.queryItemValue(QStringLiteral("input"), QUrl::FullyDecoded), expected);
    QVERIFY(network.lastRequest.url().toEncoded().contains("%2B"));
    QTRY_VERIFY(!sender.busy());
}

void WolframAlphaClientTests::ignoresCommentsAndEscapedDelimiters()
{
    WolframAlphaClient client;
    const QString source = QStringLiteral(
        "\\$ not math % $comment math$\n"
        "Text $x+1$");
    QCOMPARE(client.equationAtCursor(source, source.indexOf(QStringLiteral("comment"))).isEmpty(), true);
    QCOMPARE(client.equationAtCursor(source, source.indexOf(QStringLiteral("x+1"))),
             QStringLiteral("x+1"));

    const QString escapedPercent = QStringLiteral("$x\\%y$");
    QCOMPARE(client.equationAtCursor(escapedPercent, escapedPercent.indexOf(QLatin1Char('%'))),
             QStringLiteral("x\\%y"));
}

void WolframAlphaClientTests::sendsEquationAndReadsTextPods()
{
    FakeNetwork network;
    network.body = R"({
        "queryresult": {
            "success": true,
            "error": false,
            "pods": [
                {"id": "Input", "title": "Input interpretation",
                 "subpods": [{"plaintext": "integral x^2"}]},
                {"id": "Result", "title": "Indefinite integral",
                 "subpods": [{"plaintext": "x^3/3 + constant"}]}
            ]
        }
    })";
    WolframAlphaClient client(nullptr, &network);
    client.setAppId(QStringLiteral("TEST-APP-ID"));
    QVERIFY(client.configured());
    QSignalSpy results(&client, SIGNAL(resultReady(QString)));
    client.request(QStringLiteral("\\int x^2 dx"));
    QVERIFY(client.busy());
    QTRY_COMPARE(results.count(), 1);
    QVERIFY(!client.busy());
    QCOMPARE(results.first().first().toString(),
             QStringLiteral("Indefinite integral\nx^3/3 + constant"));
    QVERIFY(!client.result().contains(QStringLiteral("Input interpretation")));
    QCOMPARE(network.lastRequest.url().scheme(), QStringLiteral("https"));
    QCOMPARE(network.lastRequest.url().host(), QStringLiteral("api.wolframalpha.com"));
    QCOMPARE(network.lastRequest.url().path(), QStringLiteral("/v2/query"));
    const QUrlQuery query(network.lastRequest.url());
    QCOMPARE(query.queryItemValue(QStringLiteral("appid")), QStringLiteral("TEST-APP-ID"));
    QCOMPARE(query.queryItemValue(QStringLiteral("input")), QStringLiteral("\\int x^2 dx"));
    QCOMPARE(query.queryItemValue(QStringLiteral("output")), QStringLiteral("json"));
    QCOMPARE(query.queryItemValue(QStringLiteral("format")), QStringLiteral("plaintext"));
}

void WolframAlphaClientTests::reportsApiErrors()
{
    FakeNetwork network;
    network.status = 401;
    WolframAlphaClient client(nullptr, &network);
    client.setAppId(QStringLiteral("TEST-APP-ID"));
    client.request(QStringLiteral("x=1"));
    QTRY_VERIFY(!client.busy());
    QVERIFY(client.error().contains(QStringLiteral("401")));
    QVERIFY(client.result().isEmpty());

    network.status = 200;
    network.body = R"({"queryresult":{"success":false,"error":{"code":1,"msg":"Invalid AppID"}}})";
    client.request(QStringLiteral("x=1"));
    QTRY_VERIFY(!client.busy());
    QCOMPARE(client.error(), QStringLiteral("Invalid AppID"));
}

void WolframAlphaClientTests::storesAppIdInPrivateAppData()
{
    WolframAlphaClient client;
    client.setAppId(QStringLiteral("TEST-APP-ID"));
    QVERIFY(client.error().isEmpty());
    WolframAlphaClient restored;
    QCOMPARE(restored.appId(), QStringLiteral("TEST-APP-ID"));

    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("wolframalpha/appid"));
    QVERIFY(QFile::exists(path));
    QVERIFY(!(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::WriteGroup
                                               | QFile::ReadOther | QFile::WriteOther)));
    client.setAppId(QString());
    QVERIFY(!QFile::exists(path));
}

void WolframAlphaClientTests::reloadsSettingsAndClearsPreviousResults()
{
    FakeNetwork network;
    network.body = R"({"queryresult":{"success":true,"pods":[{"title":"Result","subpods":[{"plaintext":"x=1"}]}]}})";
    WolframAlphaClient editor(nullptr, &network);
    WolframAlphaClient settings;
    settings.setAppId(QStringLiteral("TEST-APP-ID"));
    QVERIFY(settings.configured());
    editor.reload();
    QCOMPARE(editor.appId(), QStringLiteral("TEST-APP-ID"));
    editor.request(QStringLiteral("x=1"));
    QTRY_VERIFY(!editor.busy());
    QVERIFY(!editor.result().isEmpty());
    editor.request(QString());
    QVERIFY(!editor.error().isEmpty());
    QVERIFY(editor.result().isEmpty());
    settings.setAppId(QString());
    editor.reload();
    QVERIFY(!editor.configured());
    editor.request(QStringLiteral("x=1"));
    QVERIFY(!editor.error().isEmpty());
    QVERIFY(!editor.busy());
}

QTEST_GUILESS_MAIN(WolframAlphaClientTests)
#include "wolframalphaclient_tests.moc"
