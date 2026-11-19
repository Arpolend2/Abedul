#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "draftstore.h"

class DraftStoreTests : public QObject
{
    Q_OBJECT
private slots:
    void persistsAndSeparatesDrafts();
    void rejectsDamagedDraft();
    void reportsSaveFailure();
    void persistsFileBaseAndCompileRoot();
    void separatesAccounts();
};

void DraftStoreTests::separatesAccounts()
{
    QTemporaryDir directory;
    DraftStore store(directory.path());
    const QString server = QStringLiteral("https://example.org");
    QVERIFY(store.save(server, QStringLiteral("project"), QStringLiteral("legacy")));
    store.setAccount(server, QStringLiteral("alice"));
    QVERIFY(store.load(server, QStringLiteral("project")));
    QVERIFY(!store.exists());
    QVERIFY(store.save(server, QStringLiteral("project"), QStringLiteral("alice draft")));
    store.setAccount(server, QStringLiteral("bob"));
    QVERIFY(store.load(server, QStringLiteral("project")));
    QVERIFY(!store.exists());
    QVERIFY(store.save(server, QStringLiteral("project"), QStringLiteral("bob draft")));
    store.setAccount(server, QStringLiteral("alice"));
    QVERIFY(store.load(server, QStringLiteral("project")));
    QCOMPARE(store.text(), QStringLiteral("alice draft"));
    QVERIFY(!store.load(QStringLiteral("https://other.example"), QStringLiteral("project")));
    QVERIFY(!store.error().isEmpty());
    store.setAccount(server, QStringLiteral("bob"));
    QVERIFY(store.error().isEmpty());
    QVERIFY(store.text().isEmpty());
    QVERIFY(!store.exists());
    QVERIFY(!store.baseKnown());
    store.setAccount(QString(), QString());
    QVERIFY(!store.save(server, QStringLiteral("project"), QStringLiteral("unsafe")));
    DraftStore legacy(directory.path());
    QVERIFY(legacy.load(server, QStringLiteral("project")));
    QCOMPARE(legacy.text(), QStringLiteral("legacy"));
}

void DraftStoreTests::persistsFileBaseAndCompileRoot()
{
    QTemporaryDir directory;
    DraftStore store(directory.path());
    const QString server = QStringLiteral("https://example.org");
    const QString project = QStringLiteral("project");
    const QString path = QStringLiteral("/chapter.tex");
    QVERIFY(store.saveFileWithBase(server, project, path, QStringLiteral("local"),
                                   QStringLiteral("original"), true));
    DraftStore restored(directory.path());
    QVERIFY(restored.loadFile(server, project, path));
    QVERIFY(restored.exists());
    QVERIFY(restored.baseKnown());
    QCOMPARE(restored.baseText(), QStringLiteral("original"));
    QCOMPARE(restored.text(), QStringLiteral("local"));
    QVERIFY(restored.saveCompileRoot(server, project, path));
    QCOMPARE(store.loadCompileRoot(server, project), path);
    QVERIFY(store.loadCompileRoot(server, QStringLiteral("other")).isEmpty());
    QVERIFY(restored.saveLastFile(server, project, QStringLiteral("/references.bib")));
    QCOMPARE(store.loadLastFile(server, project), QStringLiteral("/references.bib"));
    QVERIFY(store.loadLastFile(server, QStringLiteral("other")).isEmpty());
    QVERIFY(store.loadLastFile(QStringLiteral("https://other.example"), project).isEmpty());
    QVERIFY(restored.saveLastProject(server, project));
    QCOMPARE(store.loadLastProject(server), project);
    QVERIFY(store.saveLastProject(server, QString()));
    QVERIFY(restored.loadLastProject(server).isEmpty());
    QVERIFY(store.loadLastProject(QStringLiteral("https://other.example")).isEmpty());
    QVERIFY(store.saveFileWithBase(server, project, path, QStringLiteral("old draft"), QString(), false));
    QVERIFY(restored.loadFile(server, project, path));
    QVERIFY(!restored.baseKnown());
    QCOMPARE(restored.text(), QStringLiteral("old draft"));
}

void DraftStoreTests::persistsAndSeparatesDrafts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DraftStore store(directory.path());
    QVERIFY(store.load(QStringLiteral("https://one.example"), QStringLiteral("project")));
    QCOMPARE(store.text(), QString());
    const QString content = QString::fromUtf8("\\section{Hello}\nUnicode: \xc3\xa9\n");
    QVERIFY(store.save(QStringLiteral("https://one.example"), QStringLiteral("project"), content));
    QVERIFY(store.load(QStringLiteral("https://one.example"), QStringLiteral("other")));
    QCOMPARE(store.text(), QString());
    QVERIFY(store.load(QStringLiteral("https://two.example"), QStringLiteral("project")));
    QCOMPARE(store.text(), QString());
    QVERIFY(store.load(QStringLiteral("https://one.example"), QStringLiteral("project")));
    QCOMPARE(store.text(), content);
    QVERIFY(!store.save(QString(), QStringLiteral("project"), content));
    QVERIFY(!store.error().isEmpty());
}

void DraftStoreTests::rejectsDamagedDraft()
{
    QTemporaryDir directory;
    DraftStore store(directory.path());
    QVERIFY(store.save(QStringLiteral("https://example.org"), QStringLiteral("id"), QStringLiteral("text")));
    const QStringList files = QDir(directory.path()).entryList(QStringList() << QStringLiteral("*.json"));
    QCOMPARE(files.size(), 1);
    QFile file(QDir(directory.path()).filePath(files.first()));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("{broken"), qint64(7));
    file.close();
    QVERIFY(!store.load(QStringLiteral("https://example.org"), QStringLiteral("id")));
    QVERIFY(!store.error().isEmpty());
}

void DraftStoreTests::reportsSaveFailure()
{
    QTemporaryDir directory;
    const QString path = QDir(directory.path()).filePath(QStringLiteral("not-a-directory"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    DraftStore store(path);
    QVERIFY(!store.save(QStringLiteral("https://example.org"), QStringLiteral("id"), QStringLiteral("text")));
    QVERIFY(!store.error().isEmpty());
}

QTEST_APPLESS_MAIN(DraftStoreTests)
#include "draftstore_tests.moc"
