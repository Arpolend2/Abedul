#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QNetworkCookieJar>
#include <QTemporaryDir>
#include "overleafclient.h"
#include "overleafhtml.h"
#include "projectlist.h"
#include "compileoutput.h"
#include "socketprotocol.h"

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
    void createsTextOperations();
    void preservesHandshakeCookies();
    void remembersServerHistory();
};

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
    QVERIFY(!compileRequestBody(QString(), &body, &error));
    QVERIFY(!compileRequestBody(QStringLiteral("/../main.tex"), &body, &error));
    QVERIFY(!compileRequestBody(QStringLiteral("//main.tex"), &body, &error));
    const QByteArray serverError = R"({"error":"rootResourcePath is missing in request body"})";
    QCOMPARE(compileServerError(serverError), QStringLiteral("rootResourcePath is missing in request body"));
    QVERIFY(compileServerError("<html>error</html>").isEmpty());
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
        QVERIFY(client.setServer(QStringLiteral("https://two.example")));
        QVERIFY(client.setServer(QStringLiteral("https://one.example")));
        OverleafClient restored;
        QCOMPARE(restored.rememberedServer(), QStringLiteral("https://one.example"));
        QCOMPARE(restored.rememberedServers(), QStringList({QStringLiteral("https://one.example"),
                                                          QStringLiteral("https://two.example")}));
    }
    QSettings::setDefaultFormat(originalFormat);
    QCoreApplication::setOrganizationName(oldOrganization);
    QCoreApplication::setApplicationName(oldApplication);
}

QTEST_APPLESS_MAIN(ProjectListTests)
#include "projectlist_tests.moc"
