#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QDir>
#include <QPainter>
#include <QPdfWriter>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QWheelEvent>
#include <QSettings>
#include <QTranslator>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include "draftstore.h"
#include "overleafclient.h"
#include "latexhighlighter.h"
#include "pdfgeometry.h"
#include "doubletapmonitor.h"
#include "completionkeyfilter.h"
#include "compileoutput.h"

class FakeProjectClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error CONSTANT)
    Q_PROPERTY(QString server READ server CONSTANT)
    Q_PROPERTY(bool coverShowsProjects MEMBER coverShowsProjects)
public:
    bool coverShowsProjects = true;
    QVariantList projects() const
    {
        return {
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Active")},
                        {QStringLiteral("id"), QStringLiteral("one")},
                        {QStringLiteral("archived"), false}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Archived")},
                        {QStringLiteral("id"), QStringLiteral("two")},
                        {QStringLiteral("archived"), true}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Trashed")},
                        {QStringLiteral("id"), QStringLiteral("three")},
                        {QStringLiteral("archived"), false},
                        {QStringLiteral("trashed"), true}},
            QVariantMap{{QStringLiteral("name"), QStringLiteral("Archived and trashed")},
                        {QStringLiteral("id"), QStringLiteral("four")},
                        {QStringLiteral("archived"), true},
                        {QStringLiteral("trashed"), true}}
        };
    }
    bool busy() const { return m_busy; }
    void setBusy(bool busy) { m_busy = busy; emit busyChanged(); }
    QString error() const { return QString(); }
    QString server() const { return QStringLiteral("https://example.org"); }
signals:
    void projectsChanged();
    void busyChanged();
private:
    bool m_busy = false;
};

class FakeFileClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projectFiles READ projectFiles CONSTANT)
    Q_PROPERTY(QString filesError READ filesError CONSTANT)
    Q_PROPERTY(bool filesBusy READ filesBusy CONSTANT)
    Q_PROPERTY(bool sourceBusy MEMBER sourceBusy NOTIFY sourceBusyChanged)
public:
    QVariantList projectFiles() const { return QVariantList(); }
    QString filesError() const { return QString(); }
    bool filesBusy() const { return false; }
    bool sourceBusy = false;
    QString openedPath;
    QString openedProject;
    Q_INVOKABLE void loadProjectFiles(const QString &) {}
    Q_INVOKABLE void closeProjectFiles() {}
    Q_INVOKABLE void openProjectFileExternally(const QString &project, const QString &path)
    { openedProject = project; openedPath = path; }
signals:
    void sourceBusyChanged();
    void externalFileReady(const QUrl &url);
};

class FakeFileEditor : public QObject
{
    Q_OBJECT
public:
    QString openedPath;
    Q_INVOKABLE bool openProjectFile(const QString &path) { openedPath = path; return true; }
};

class FakePageStack : public QObject
{
    Q_OBJECT
public:
    int pops = 0;
    int pushes = 0;
    int backwards = 0;
    int forwards = 0;
    QUrl lastUrl;
    QVariantMap attachedProperties;
    QVariantMap pushProperties;
    Q_PROPERTY(bool busy MEMBER busy)
    Q_PROPERTY(bool backNavigation MEMBER backNavigation)
    Q_PROPERTY(bool forwardNavigation MEMBER forwardNavigation)
    bool busy = false;
    bool backNavigation = true;
    bool forwardNavigation = true;
    Q_INVOKABLE void pop() { ++pops; }
    Q_INVOKABLE void navigateBack() { ++backwards; }
    Q_INVOKABLE void navigateForward() { ++forwards; }
    Q_INVOKABLE QObject *nextPage(QObject *) { return nullptr; }
    Q_INVOKABLE void push(const QUrl &url) { ++pushes; lastUrl = url; }
    Q_INVOKABLE void push(const QUrl &url, const QVariantMap &properties)
    { ++pushes; lastUrl = url; pushProperties = properties; }
    Q_INVOKABLE void pushAttached(const QUrl &url, const QVariantMap &properties)
    { lastUrl = url; attachedProperties = properties; }
};

class FakeLoginClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString rememberedServer READ rememberedServer CONSTANT)
    Q_PROPERTY(QStringList rememberedServers READ rememberedServers CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)
public:
    QString rememberedServer() const { return remembered; }
    QStringList rememberedServers() const { return {QStringLiteral("https://example.org"),
                                                   QStringLiteral("https://other.example")}; }
    QString error() const { return QString(); }
    QString selected;
    QString remembered = QStringLiteral("https://example.org");
    bool acceptServer = true;
    Q_INVOKABLE bool setServer(const QString &address) { selected = address; return acceptServer; }
};

class FakePdfClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(QObject *coverEditor READ coverEditor NOTIFY coverEditorChanged)
    Q_PROPERTY(bool coverShowsProjects MEMBER coverShowsProjects NOTIFY coverShowsProjectsChanged)
    Q_PROPERTY(bool archiveBusy MEMBER archiveBusy NOTIFY archiveBusyChanged)
    Q_PROPERTY(QString archiveError MEMBER archiveError NOTIFY archiveStatusChanged)
    Q_PROPERTY(QString archivePath MEMBER archivePath NOTIFY archiveStatusChanged)
    Q_PROPERTY(QString archiveProject MEMBER archiveProject NOTIFY archiveStatusChanged)
    Q_PROPERTY(QString server READ server CONSTANT)
    Q_PROPERTY(QString pdfPath READ pdfPath NOTIFY pdfPathChanged)
    Q_PROPERTY(QString pdfError READ pdfError CONSTANT)
    Q_PROPERTY(bool pdfBusy READ pdfBusy NOTIFY pdfBusyChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceTextChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY sourceTextChanged)
    Q_PROPERTY(QString sourceError READ sourceError NOTIFY sourceErrorChanged)
    Q_PROPERTY(bool sourceBusy READ sourceBusy NOTIFY sourceBusyChanged)
    Q_PROPERTY(bool pdfOnLeft MEMBER pdfOnLeft NOTIFY pdfOnLeftChanged)
    Q_PROPERTY(int editorFontScale MEMBER editorFontScale NOTIFY editorFontScaleChanged)
public:
    QVariantList projects() const
    {
        return {QVariantMap{{QStringLiteral("id"), QStringLiteral("one")},
                            {QStringLiteral("name"), QStringLiteral("First")}},
                QVariantMap{{QStringLiteral("id"), QStringLiteral("two")},
                            {QStringLiteral("name"), QStringLiteral("Deleted")},
                            {QStringLiteral("trashed"), true}},
                QVariantMap{{QStringLiteral("id"), QStringLiteral("three")},
                            {QStringLiteral("name"), QStringLiteral("Other")}}};
    }
    QObject *coverEditor() const { return m_coverEditor; }
    Q_INVOKABLE void setCoverEditor(QObject *editor) { m_coverEditor = editor; emit coverEditorChanged(); }
    Q_INVOKABLE QUrl cachedPdfForExternalOpen() { return QUrl(); }
    Q_INVOKABLE void downloadProjectArchive(const QString &project) { archiveRequested = project; }
    bool archiveBusy = false;
    bool coverShowsProjects = true;
    QString archiveError;
    QString archivePath;
    QString archiveProject;
    QString archiveRequested;
    QString server() const { return QStringLiteral("https://example.org"); }
    QString pdfPath() const { return m_path; }
    QString pdfError() const { return QString(); }
    bool pdfBusy() const { return m_pdfBusy; }
    QString sourcePath() const { return m_sourcePath; }
    QString sourceText() const { return m_sourceText; }
    QString sourceError() const { return m_sourceError; }
    bool sourceBusy() const { return m_sourceBusy; }
    int saves = 0;
    bool pdfOnLeft = false;
    int editorFontScale = 100;
    int compiles = 0;
    int sourceCloses = 0;
    int pdfCloses = 0;
    QString openedPath;
    QString root;
    void setPdfPath(const QString &path) { m_path = path; emit pdfPathChanged(); }
    void setSource(const QString &path, const QString &text) { m_sourcePath = path; m_sourceText = text; emit sourceTextChanged(); }
    void setPdfBusy(bool busy) { m_pdfBusy = busy; emit pdfBusyChanged(); }
    void setSourceBusy(bool busy) { m_sourceBusy = busy; emit sourceBusyChanged(); }
    void setSourceError(const QString &error) { m_sourceError = error; emit sourceErrorChanged(); }
    Q_INVOKABLE void closeProjectPdf() { ++pdfCloses; }
    Q_INVOKABLE void closeProjectSource() { ++sourceCloses; }
    Q_INVOKABLE void openProjectSource(const QString &, const QString &path) { openedPath = path; }
    Q_INVOKABLE void saveProjectSource(const QString &, const QString &, const QString &, bool, const QString &) { ++saves; }
    Q_INVOKABLE void compileProjectPdf(const QString &, const QString &path) { ++compiles; root = path; }
signals:
    void projectsChanged();
    void coverEditorChanged();
    void coverShowsProjectsChanged();
    void archiveBusyChanged();
    void archiveStatusChanged();
    void pdfPathChanged();
    void pdfBusyChanged();
    void sourceBusyChanged();
    void sourceErrorChanged();
    void pdfOnLeftChanged();
    void editorFontScaleChanged();
    void sourceTextChanged();
    void sourceSaved(const QString &path, const QString &text);
    void syncError(const QString &message);
    void syncPdfPositions(const QVariantList &positions);
    void syncSourcePositions(const QVariantList &positions);
private:
    QObject *m_coverEditor = nullptr;
    QString m_path;
    QString m_sourcePath;
    QString m_sourceText;
    QString m_sourceError;
    bool m_pdfBusy = false;
    bool m_sourceBusy = false;
};

class FakeWebView : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url NOTIFY urlChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    QUrl url() const { return m_url; }
    bool loading() const { return m_loading; }

    void start(const QUrl &url)
    {
        emit loadStart();
        m_loading = true;
        emit loadingChanged(true);
        m_url = url;
        emit urlChanged();
    }

    void finish(int status, bool loadingFirst = false)
    {
        if (loadingFirst) {
            m_loading = false;
            emit loadingChanged(false);
        }
        emit loadFinished(status);
        if (!loadingFirst) {
            m_loading = false;
            emit loadingChanged(false);
        }
    }

signals:
    void urlChanged();
    void loadingChanged(bool status);
    void loadStart();
    void loadFinished(int httpStatusCode);

private:
    QUrl m_url;
    bool m_loading = false;
};

class FakePulley : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active MEMBER active)
    Q_PROPERTY(bool enabled MEMBER enabled)
    Q_PROPERTY(qreal height MEMBER height)
public:
    bool active = false;
    bool enabled = true;
    qreal height = 100;
    Q_INVOKABLE void close() { active = false; }
};

class FakeFlickable : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *pullDownMenu MEMBER menu)
    Q_PROPERTY(qreal originY MEMBER originY)
    Q_PROPERTY(qreal contentY MEMBER contentY)
    Q_PROPERTY(qreal contentHeight MEMBER contentHeight)
    Q_PROPERTY(qreal height MEMBER height)
public:
    QObject *menu = nullptr;
    qreal originY = 0;
    qreal contentY = 0;
    qreal contentHeight = 800;
    qreal height = 800;
    Q_INVOKABLE void cancelFlick() {}
};

class AutoLoginTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void doubleTapRespectsClipping();
    void loadsPdfGeometry();
    void automaticLoginOnlyAfterSuccessfulProjectLoad();
    void interfaceComponentsLoad();
    void russianTranslations();
    void coverShowsProjectsAndRecentEdits();
    void opensProjectFilesByType();
    void editorPersistsDraft();
    void completesLatexCommands();
    void editorScrollsLongDocuments();
    void projectViewFiltersArchived();
    void pdfPaneLoadsDocument();
    void pdfPaneReplacesRenderedPage();
    void sourcePreservesLocalEdits();
    void latexHighlightingPreservesText();
    void highlightsLatexContexts();
    void highlightsBibliography();
    void compileWaitsForSave();
    void saveShortcutOnlyOnActiveEditor();
    void reopensLastFile();
    void attachedEditorDefersInitialization();
    void restoresServerAtStartup();
    void showsServerSelectionWhenRestoreUnavailable();
    void editorPulleyWheelIsLimitedToHeader();
    void mouseButtonsFollowNavigationRules();
    void editorSettingsAndZoom();
    void settingsPersistAcrossClients();
    void changesServerFromProjects();
    void restoresLastProjectNavigation();
    void attachedEditorSurvivesNavigation();
};

void AutoLoginTests::initTestCase()
{
    qmlRegisterType<LatexHighlighter>("Abedul", 1, 0, "LatexHighlighter");
    qmlRegisterType<PdfGeometry>("Abedul", 1, 0, "PdfGeometry");
    qmlRegisterType<DoubleTapMonitor>("Abedul", 1, 0, "DoubleTapMonitor");
    qmlRegisterType<CompletionKeyFilter>("Abedul", 1, 0, "CompletionKeyFilter");
}

void AutoLoginTests::doubleTapRespectsClipping()
{
    QQuickWindow window;
    QQuickItem clip(window.contentItem());
    clip.setSize(QSizeF(50, 50));
    clip.setClip(true);
    QQuickItem target(&clip);
    target.setSize(QSizeF(100, 100));
    DoubleTapMonitor monitor;
    monitor.setTarget(&target);
    QSignalSpy spy(&monitor, SIGNAL(doubleTapped(qreal,qreal)));

    QMouseEvent inside(QEvent::MouseButtonDblClick, QPointF(20, 20),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &inside);
    QCOMPARE(spy.count(), 1);
    QMouseEvent clipped(QEvent::MouseButtonDblClick, QPointF(75, 20),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &clipped);
    QCOMPARE(spy.count(), 1);
}

void AutoLoginTests::loadsPdfGeometry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("geometry.pdf"));
    {
        QPdfWriter writer(path);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.drawText(100, 100, QStringLiteral("Geometry"));
    }
    PdfGeometry geometry;
    geometry.setPath(path);
    QTRY_VERIFY(geometry.ready() || !geometry.error().isEmpty());
    QVERIFY2(geometry.ready(), qPrintable(geometry.error()));
    const QVariantMap page = geometry.page(0);
    QVERIFY(page.value(QStringLiteral("width")).toDouble() > 0);
    QVERIFY(page.value(QStringLiteral("height")).toDouble() > 0);
    QCOMPARE(page.value(QStringLiteral("rotation")).toInt(), 0);
    QVERIFY(page.value(QStringLiteral("supported")).toBool());
}

void AutoLoginTests::automaticLoginOnlyAfterSuccessfulProjectLoad()
{
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    const QSettings::Format originalFormat = QSettings::defaultFormat();
    const QString originalOrganization = QCoreApplication::organizationName();
    const QString originalApplication = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("AbedulTest"));
    QCoreApplication::setApplicationName(QStringLiteral("AutoLogin"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> controller(component.create());
    QVERIFY2(controller, qPrintable(component.errorString()));

    OverleafClient client;
    QVERIFY(client.setServer(QStringLiteral("https://www.overleaf.com")));
    FakeWebView browser;
    controller->setProperty("client", QVariant::fromValue(static_cast<QObject *>(&client)));
    controller->setProperty("webView", QVariant::fromValue(static_cast<QObject *>(&browser)));
    QSignalSpy requests(controller.data(), SIGNAL(loginRequested()));
    QSignalSpy signIn(controller.data(), SIGNAL(signInRequired()));
    QSignalSpy failures(controller.data(), SIGNAL(loadFailed(int)));
    QCoreApplication::processEvents();

    browser.start(QUrl(QStringLiteral("https://www.overleaf.com/login")));
    browser.finish(500);
    QVERIFY(client.rememberedServer().isEmpty());
    browser.start(QUrl(QStringLiteral("https://www.overleaf.com.example.org/login")));
    browser.finish(200);
    QVERIFY(client.rememberedServer().isEmpty());
    browser.start(QUrl(QStringLiteral("https://www.overleaf.com/login")));
    browser.finish(200);
    QVERIFY2(client.rememberedServer() == QStringLiteral("https://www.overleaf.com"),
             qPrintable(client.error()));
    QTest::qWait(350);
    QCOMPARE(requests.count(), 0);
    QCOMPARE(signIn.count(), 1);

    browser.start(QUrl(QStringLiteral("https://www.overleaf.com/project")));
    browser.finish(500);
    QTest::qWait(350);
    QCOMPARE(requests.count(), 0);
    QCOMPARE(failures.count(), 2);
    QCOMPARE(failures.at(1).at(0).toInt(), 500);

    browser.start(QUrl(QStringLiteral("https://www.overleaf.com.example.org/project")));
    browser.finish(200);
    QTest::qWait(350);
    QCOMPARE(requests.count(), 0);

    browser.start(QUrl(QStringLiteral("https://www.overleaf.com/project")));
    browser.finish(200);
    QTRY_COMPARE(requests.count(), 1);
    emit browser.loadFinished(200);
    QTest::qWait(350);
    QCOMPARE(requests.count(), 1);

    browser.start(QUrl(QStringLiteral("https://www.overleaf.com/login")));
    browser.finish(200);
    browser.start(QUrl(QStringLiteral("https://www.overleaf.com/project")));
    browser.finish(200, true);
    QTRY_COMPARE(requests.count(), 2);
    QSettings::setDefaultFormat(originalFormat);
    QCoreApplication::setOrganizationName(originalOrganization);
    QCoreApplication::setApplicationName(originalApplication);
}

void AutoLoginTests::interfaceComponentsLoad()
{
    QQmlEngine engine;
    const QUrl autoLoginUrl = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    const QStringList pages = {QStringLiteral("EditorPage.qml"), QStringLiteral("ProjectsPage.qml"),
                               QStringLiteral("CoverPage.qml"), QStringLiteral("OpenCachedPdf.qml"),
                               QStringLiteral("LoginPage.qml"), QStringLiteral("SettingsPage.qml"),
                               QStringLiteral("ProjectFilesPage.qml"), QStringLiteral("CompileRootPage.qml")};
    for (const QString &page : pages) {
        QQmlComponent component(&engine, autoLoginUrl.resolved(QUrl(page)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    }
    QQmlComponent window(&engine, autoLoginUrl.resolved(QUrl(QStringLiteral("../OverleafClient.qml"))));
    QVERIFY2(window.isReady(), qPrintable(window.errorString()));
}

void AutoLoginTests::russianTranslations()
{
    QTranslator translator;
    QVERIFY(translator.load(QLocale(QStringLiteral("ru_RU")), QStringLiteral("Abedul"),
                            QStringLiteral("_"), QStringLiteral(ABEDUL_TRANSLATION_DIR)));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    {
        QTemporaryDir directory;
        DraftStore drafts(directory.path());
        FakePdfClient client;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
        engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
        const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
        QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
        QScopedPointer<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        QObject *save = page->findChild<QObject *>(QStringLiteral("saveAndCompile"));
        QVERIFY(save);
        QCOMPARE(save->property("text").toString(), QString::fromUtf8("Сохранить и скомпилировать"));
        QByteArray payload;
        QString error;
        QVERIFY(!compileRequestBody(QString(), &payload, &error));
        QCOMPARE(error, QString::fromUtf8("Выберите корректный главный документ."));
        OverleafClient realClient;
        QVERIFY(!realClient.setServer(QStringLiteral("http://example.org")));
        QCOMPARE(realClient.error(), QString::fromUtf8("Введите HTTPS-адрес сервера без пути."));
        QCOMPARE(QCoreApplication::translate("OverleafClient", "Project ZIP downloaded"),
                 QString::fromUtf8("ZIP-архив проекта загружен"));
    }
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QCOMPARE(QCoreApplication::translate("EditorPage", "Save and compile"),
             QStringLiteral("Save and compile"));
}

void AutoLoginTests::coverShowsProjectsAndRecentEdits()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    QQmlComponent stackComponent(&engine);
    stackComponent.setData(R"(
        import QtQuick 2.0
        QtObject {
            property var currentPage
            property var projectsPage
            property var attachedPage
            property bool editorInStack: false
            property bool busy: false
            function find(test) {
                if (editorInStack && test(attachedPage)) return attachedPage
                return projectsPage && test(projectsPage) ? projectsPage : null
            }
            function nextPage(page) { return page === projectsPage ? attachedPage : null }
            function pop(page, operation) { currentPage = page }
            function navigateForward(operation) { currentPage = attachedPage }
        }
    )", QUrl());
    QScopedPointer<QObject> stack(stackComponent.create());
    QVERIFY2(stack, qPrintable(stackComponent.errorString()));
    QObject projectsPage;
    projectsPage.setObjectName(QStringLiteral("projectsPage"));
    stack->setProperty("projectsPage", QVariant::fromValue(&projectsPage));
    stack->setProperty("currentPage", QVariant::fromValue(&projectsPage));
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), stack.data());
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent coverComponent(&engine, base.resolved(QUrl(QStringLiteral("CoverPage.qml"))));
    QVERIFY2(coverComponent.isReady(), qPrintable(coverComponent.errorString()));
    QScopedPointer<QObject> cover(coverComponent.create());
    QVERIFY2(cover, qPrintable(coverComponent.errorString()));
    cover->setProperty("navigationStack", QVariant::fromValue(stack.data()));
    cover->setProperty("width", 240);
    cover->setProperty("height", 320);
    QCOMPARE(cover->property("visibleProjects").toList().size(), 2);
    QObject *title = cover->findChild<QObject *>(QStringLiteral("coverTitle"));
    QVERIFY(title);
    QCOMPARE(title->property("text").toString(), QString());

    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("one"));
    page->setProperty("projectName", QStringLiteral("First"));
    component.completeCreate();
    client.setProperty("coverShowsProjects", false);
    client.setCoverEditor(page.data());
    stack->setProperty("attachedPage", QVariant::fromValue(page.data()));
    QVERIFY(QMetaObject::invokeMethod(cover.data(), "openCoverPage", Q_ARG(QVariant, false)));
    QCOMPARE(stack->property("currentPage").value<QObject *>(), page.data());
    QCOMPARE(title->property("text").toString(), QStringLiteral("First"));
    client.setSource(QStringLiteral("/main.tex"), QStringLiteral("one\ntwo\nthree\nfour\nfive\nsix\nseven"));
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    editor->setProperty("text", QStringLiteral("one\ntwo\nthree\nchanged\nfive\nsix\nseven"));
    QObject *textEdit = editor->property("_editor").value<QObject *>();
    QVERIFY(textEdit);
    textEdit->setProperty("cursorPosition", 21);
    QTRY_VERIFY(page->property("coverSnippet").toString().contains(QStringLiteral("changed")));
    QCOMPARE(page->property("coverSnippetFile").toString(), QStringLiteral("/main.tex"));
    QObject *snippet = cover->findChild<QObject *>(QStringLiteral("coverSnippet"));
    QVERIFY(snippet);
    QCOMPARE(snippet->property("text").toString(), page->property("coverSnippet").toString());
    QQmlComponent snapshotComponent(&engine);
    snapshotComponent.setData("import QtQuick 2.0\nQtObject { property url url: \"\" }", QUrl());
    QScopedPointer<QObject> snapshot(snapshotComponent.create());
    QVERIFY(snapshot);
    page->setProperty("coverPdfSnapshot", QVariant::fromValue(snapshot.data()));
    QObject *pdfPeek = cover->findChild<QObject *>(QStringLiteral("coverPdfPeek"));
    QVERIFY(pdfPeek);
    QVERIFY(pdfPeek->property("visible").toBool());
    QTest::qWait(100);
    QVERIFY2(snippet->property("x").toReal() < pdfPeek->property("x").toReal(),
             qPrintable(QStringLiteral("cover width=%1 text width=%2 x=%3 preview width=%4 x=%5")
                        .arg(cover->property("width").toReal()).arg(snippet->property("width").toReal())
                        .arg(snippet->property("x").toReal()).arg(pdfPeek->property("width").toReal())
                        .arg(pdfPeek->property("x").toReal())));
    client.setProperty("pdfOnLeft", true);
    QTRY_VERIFY(pdfPeek->property("x").toReal() < snippet->property("x").toReal());
    client.setProperty("pdfOnLeft", false);
    QObject *zip = cover->findChild<QObject *>(QStringLiteral("coverDownloadZipWithoutPdf"));
    QVERIFY(zip);
    QVERIFY(QMetaObject::invokeMethod(zip, "triggered"));
    QCOMPARE(client.archiveRequested, QStringLiteral("one"));
    QObject *back = cover->findChild<QObject *>(QStringLiteral("coverShowProjectsWithoutPdf"));
    QVERIFY(back);
    stack->setProperty("editorInStack", true);
    stack->setProperty("busy", true);
    QVERIFY(QMetaObject::invokeMethod(back, "triggered"));
    QCOMPARE(stack->property("currentPage").value<QObject *>(), page.data());
    QCOMPARE(title->property("text").toString(), QStringLiteral("First"));
    QVERIFY(!cover->property("navigationError").toString().isEmpty());
    stack->setProperty("busy", false);
    QVERIFY(QMetaObject::invokeMethod(back, "triggered"));
    QCOMPARE(title->property("text").toString(), QString());
    QVERIFY(!pdfPeek->property("visible").toBool());
    QCOMPARE(stack->property("currentPage").value<QObject *>(), &projectsPage);
    QObject *repeater = cover->findChild<QObject *>(QStringLiteral("coverProjects"));
    QVERIFY(repeater);
    QQuickItem *lastProject = nullptr;
    QQuickItem *otherProject = nullptr;
    QVERIFY(QMetaObject::invokeMethod(repeater, "itemAt", Q_RETURN_ARG(QQuickItem *, lastProject), Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(repeater, "itemAt", Q_RETURN_ARG(QQuickItem *, otherProject), Q_ARG(int, 1)));
    QVERIFY(lastProject);
    QVERIFY(otherProject);
    QVERIFY(lastProject->property("color") != otherProject->property("color"));
    QObject *forward = cover->findChild<QObject *>(QStringLiteral("coverShowEditor"));
    QVERIFY(forward);
    stack->setProperty("editorInStack", false);
    QVERIFY(QMetaObject::invokeMethod(forward, "triggered"));
    QCOMPARE(title->property("text").toString(), QStringLiteral("First"));
    QCOMPARE(stack->property("currentPage").value<QObject *>(), page.data());
    client.setCoverEditor(nullptr);
    QCOMPARE(title->property("text").toString(), QString());
}

void AutoLoginTests::attachedEditorSurvivesNavigation()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        import "." as Pages
        ApplicationWindow {
            property var projectsPage
            property var editorPage
            property bool editorRetained: false
            property var activePage: pageStack.currentPage
            initialPage: Component { Page { objectName: "projectsPage" } }
            Component {
                id: editor
                Page {
                    property string projectId: "one"
                    property string projectName: "First"
                    property string coverSnippet: ""
                    property string coverSnippetFile: ""
                    property bool dirty: false
                }
            }
            Pages.CoverPage { objectName: "testCover"; navigationStack: pageStack }
            function openEditor() {
                projectsPage = pageStack.currentPage
                editorPage = pageStack.pushAttached(editor)
                pageStack.navigateForward(PageStackAction.Immediate)
                overleafClient.setCoverEditor(editorPage)
                overleafClient.coverShowsProjects = false
            }
            function returnToProjects() {
                pageStack.pop(projectsPage, PageStackAction.Immediate)
                editorRetained = pageStack.nextPage(projectsPage) === editorPage
            }
        }
    )", QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH)));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "openEditor"));
    QVERIFY(window->property("editorPage").value<QObject *>());
    QVERIFY(QMetaObject::invokeMethod(window.data(), "returnToProjects"));
    QVERIFY(window->property("editorRetained").toBool());
    QObject *cover = window->findChild<QObject *>(QStringLiteral("testCover"));
    QVERIFY(cover);
    client.setProperty("coverShowsProjects", true);
    QObject *forward = cover->findChild<QObject *>(QStringLiteral("coverShowEditor"));
    QVERIFY(forward);
    QVERIFY(QMetaObject::invokeMethod(forward, "triggered"));
    QCOMPARE(window->property("activePage").value<QObject *>(), window->property("editorPage").value<QObject *>());
    QObject *back = cover->findChild<QObject *>(QStringLiteral("coverShowProjectsWithoutPdf"));
    QVERIFY(back);
    QVERIFY(QMetaObject::invokeMethod(back, "triggered"));
    QCOMPARE(window->property("activePage").value<QObject *>(), window->property("projectsPage").value<QObject *>());
}

void AutoLoginTests::editorPersistsDraft()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    OverleafClient client;
    QVERIFY(client.setServer(QStringLiteral("https://www.overleaf.com")));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("projectId", QStringLiteral("test"));
    page->setProperty("projectName", QStringLiteral("Test"));
    component.completeCreate();
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    QVERIFY(pageItem);
    pageItem->setSize(QSizeF(600, 800));
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    QObject *header = page->findChild<QObject *>(QStringLiteral("editorHeader"));
    QObject *codePane = page->findChild<QObject *>(QStringLiteral("editorCodePane"));
    QObject *pdfLoader = page->findChild<QObject *>(QStringLiteral("pdfLoader"));
    QObject *headerBackground = page->findChild<QObject *>(QStringLiteral("editorHeaderBackground"));
    QObject *projectName = page->findChild<QObject *>(QStringLiteral("editorProjectName"));
    QQuickItem *settings = page->findChild<QQuickItem *>(QStringLiteral("editorSettings"));
    QQuickItem *openPdf = page->findChild<QQuickItem *>(QStringLiteral("openCachedPdf"));
    QQuickItem *downloadZip = page->findChild<QQuickItem *>(QStringLiteral("downloadProjectArchive"));
    QQuickItem *projectFiles = page->findChild<QQuickItem *>(QStringLiteral("editorProjectFiles"));
    QObject *saveMenuItem = page->findChild<QObject *>(QStringLiteral("saveAndCompile"));
    QVERIFY(header);
    QVERIFY(codePane);
    QVERIFY(pdfLoader);
    QVERIFY(headerBackground);
    QVERIFY(projectName);
    QVERIFY(settings);
    QVERIFY(openPdf);
    QVERIFY(downloadZip);
    QVERIFY(projectFiles);
    QVERIFY(saveMenuItem);
    QCOMPARE(qobject_cast<QQuickItem *>(projectName)->parentItem(),
             qobject_cast<QQuickItem *>(saveMenuItem)->parentItem());
    const QList<QQuickItem *> menuItems = settings->parentItem()->childItems();
    const QList<QQuickItem *> expectedOrder = {
        qobject_cast<QQuickItem *>(projectName), settings, openPdf, downloadZip,
        projectFiles, qobject_cast<QQuickItem *>(saveMenuItem)
    };
    for (int i = 0; i + 1 < expectedOrder.size(); ++i) {
        const int position = menuItems.indexOf(expectedOrder.at(i));
        QVERIFY(position >= 0);
        QVERIFY(position < menuItems.indexOf(expectedOrder.at(i + 1)));
    }
    QCOMPARE(projectName->property("text").toString(), QStringLiteral("Test"));
    page->setProperty("projectName", QStringLiteral("Renamed"));
    QCOMPARE(projectName->property("text").toString(), QStringLiteral("Renamed"));
    QCOMPARE(headerBackground->property("color").value<QColor>().alpha(), 255);
    const QColor headerColor = headerBackground->property("color").value<QColor>();
    QVERIFY(headerColor == QColor(Qt::black) || headerColor == QColor(Qt::white));
    QVERIFY(codePane->property("clip").toBool());
    QVERIFY(pdfLoader->property("clip").toBool());
    QVERIFY(header->property("height").toReal() > 0);
    QVERIFY(header->property("pullDownMenu").value<QObject *>());
    QVERIFY(!codePane->property("pullDownMenu").value<QObject *>());
    QQuickItem *panes = page->findChild<QQuickItem *>(QStringLiteral("editorPanes"));
    QQuickItem *pdfPane = page->findChild<QQuickItem *>(QStringLiteral("editorPdfPane"));
    QQuickItem *compileButton = page->findChild<QQuickItem *>(QStringLiteral("pdfCompileButton"));
    QVERIFY(panes);
    QVERIFY(pdfPane);
    QVERIFY(compileButton);
    QCOMPARE(pdfPane->parentItem(), panes);
    QCOMPARE(compileButton->parentItem(), pdfPane);
    QCOMPARE(compileButton->x() + compileButton->width() / 2, pdfPane->width() / 2);
    QCOMPARE(compileButton->y() + compileButton->height() / 2, pdfPane->height() / 2);
    QCOMPARE(qobject_cast<QQuickItem *>(codePane)->parentItem(), panes);
    const qreal originalY = panes->mapToItem(pageItem, QPointF()).y();
    header->setProperty("contentY", -80);
    QCOMPARE(panes->mapToItem(pageItem, QPointF()).y(), originalY + 80);
    header->setProperty("contentY", 0);
    QVERIFY(page->property("initialized").toBool());
    editor->setProperty("text", QStringLiteral("\\section{Test}"));
    QTRY_VERIFY(!page->property("dirty").toBool());
    QVERIFY(drafts.load(client.server(), QStringLiteral("test")));
    QCOMPARE(drafts.text(), QStringLiteral("\\section{Test}"));
}

void AutoLoginTests::completesLatexCommands()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    view.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(600, 800);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    view.setSource(base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty()
             ? QString() : view.errors().first().toString()));
    QQuickItem *page = view.rootObject();
    page->setVisible(true);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    page->setProperty("currentFile", QStringLiteral("/main.tex"));
    page->setProperty("initialized", true);
    editor->setProperty("text", QStringLiteral("\\beg"));
    QQuickItem *input = qobject_cast<QQuickItem *>(editor->property("_editor").value<QObject *>());
    QVERIFY(input);
    input->forceActiveFocus();
    input->setProperty("cursorPosition", 4);
    QCOMPARE(page->property("latexCompletion").toString(), QStringLiteral("in{}"));
    QQuickItem *hint = page->findChild<QQuickItem *>(QStringLiteral("latexCompletionHint"));
    QVERIFY(hint);
    QTRY_VERIFY(hint->isVisible());
    QTRY_VERIFY(hint->width() > 0);
    QVERIFY(hint->height() > 0);
    QObject *shortcut = page->findChild<QObject *>(QStringLiteral("latexCompletionKeyFilter"));
    QVERIFY(shortcut);
    QVERIFY(shortcut->property("enabled").toBool());
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &tab);
    QCOMPARE(input->property("text").toString(), QStringLiteral("\\begin{}"));
    QCOMPARE(input->property("cursorPosition").toInt(), 7);
    QVERIFY(page->property("dirty").toBool());

    editor->setProperty("text", QStringLiteral("  \\eqn"));
    input->setProperty("cursorPosition", 6);
    QCOMPARE(hint->property("text").toString(), QStringLiteral(" -> \\begin{eqnarray}..."));
    QKeyEvent snippetTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(input, &snippetTab);
    QCOMPARE(input->property("text").toString(),
             QStringLiteral("  \\begin{eqnarray}\n      \n  \\end{eqnarray}"));
    QCOMPARE(input->property("cursorPosition").toInt(),
             QStringLiteral("  \\begin{eqnarray}\n      ").length());
    QVERIFY(QMetaObject::invokeMethod(input, "undo"));
    QCOMPARE(input->property("text").toString(), QStringLiteral("  \\eqn"));

    const QList<QPair<QString, QString>> snippets = {
        {QStringLiteral("\\equ"), QStringLiteral("equation")},
        {QStringLiteral("\\equation"), QStringLiteral("equation")},
        {QStringLiteral("\\tab"), QStringLiteral("tabular")},
        {QStringLiteral("\\tabular"), QStringLiteral("tabular")},
        {QStringLiteral("\\fig"), QStringLiteral("figure")},
        {QStringLiteral("\\figure"), QStringLiteral("figure")},
        {QStringLiteral("\\align"), QStringLiteral("align")},
        {QStringLiteral("\\itemize"), QStringLiteral("itemize")},
        {QStringLiteral("\\enumerate"), QStringLiteral("enumerate")}
    };
    for (const auto &snippet : snippets) {
        editor->setProperty("text", snippet.first);
        input->setProperty("cursorPosition", snippet.first.length());
        QVERIFY2(shortcut->property("enabled").toBool(), qPrintable(snippet.first));
        QVERIFY(hint->property("text").toString().contains(snippet.second));
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &key);
        const QString extra = snippet.second == QStringLiteral("tabular") ? QStringLiteral("{c}") : QString();
        const QString opening = QStringLiteral("\\begin{%1}%2\n    ").arg(snippet.second, extra);
        QCOMPARE(input->property("text").toString(),
                 opening + QStringLiteral("\n\\end{%1}").arg(snippet.second));
        QCOMPARE(input->property("cursorPosition").toInt(), opening.length());
        QVERIFY(QMetaObject::invokeMethod(input, "undo"));
        QCOMPARE(input->property("text").toString(), snippet.first);
    }

    editor->setProperty("text", QStringLiteral("\\begin{eqn}"));
    input->setProperty("cursorPosition", 10);
    QCOMPARE(page->property("latexCompletion").toString(), QStringLiteral("array"));
    QKeyEvent environmentTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(input, &environmentTab);
    QCOMPARE(input->property("text").toString(), QStringLiteral("\\begin{eqnarray}"));
    QCOMPARE(input->property("cursorPosition").toInt(), 16);

    editor->setProperty("text", QStringLiteral("% \\beg"));
    input->setProperty("cursorPosition", 6);
    QCOMPARE(page->property("latexCompletion").toString(), QString());
    editor->setProperty("text", QStringLiteral("\\\\beg"));
    input->setProperty("cursorPosition", 5);
    QCOMPARE(page->property("latexCompletion").toString(), QString());
    editor->setProperty("text", QStringLiteral("\\beg trailing"));
    input->setProperty("cursorPosition", 4);
    QCOMPARE(page->property("latexCompletion").toString(), QStringLiteral("in{}"));
    QQuickItem *popup = page->findChild<QQuickItem *>(QStringLiteral("latexCompletionPopup"));
    QVERIFY(popup);
    QVERIFY(popup->isVisible());
    QKeyEvent inlineTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &inlineTab);
    QCOMPARE(input->property("text").toString(), QStringLiteral("\\begin{} trailing"));
    editor->setProperty("text", QStringLiteral("\\sec{Title}"));
    input->setProperty("cursorPosition", 4);
    QCOMPARE(page->property("latexCompletion").toString(), QStringLiteral("tion"));
    QKeyEvent braceTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &braceTab);
    QCOMPARE(input->property("text").toString(), QStringLiteral("\\section{Title}"));
    editor->setProperty("text", QStringLiteral("\\"));
    input->setProperty("cursorPosition", 1);
    QVERIFY(!page->property("latexCompletion").toString().isEmpty());
    editor->setProperty("text", QStringLiteral("\\a"));
    input->setProperty("cursorPosition", 2);
    QVERIFY(!page->property("latexCompletion").toString().isEmpty());
    editor->setProperty("text", QStringLiteral("\\in"));
    input->setProperty("cursorPosition", 3);
    QCOMPARE(page->property("latexCompletion").toString(), QString());
    editor->setProperty("text", QStringLiteral("\\unknowncommand"));
    input->setProperty("cursorPosition", 15);
    QCOMPARE(page->property("latexCompletion").toString(), QString());
    QVERIFY(!hint->isVisible());
    QVERIFY(!shortcut->property("enabled").toBool());
    editor->setProperty("text", QStringLiteral("\\begin{unknown}"));
    input->setProperty("cursorPosition", 14);
    QVERIFY(!hint->isVisible());
    QVERIFY(!popup->isVisible());
    QVERIFY(!shortcut->property("enabled").toBool());
    editor->setProperty("text", QStringLiteral("\\beg"));
    QVERIFY(QMetaObject::invokeMethod(input, "select", Q_ARG(int, 1), Q_ARG(int, 4)));
    QCOMPARE(page->property("latexCompletion").toString(), QString());
    QVERIFY(QMetaObject::invokeMethod(input, "deselect"));
    page->setProperty("currentFile", QStringLiteral("/refs.bib"));
    input->setProperty("cursorPosition", 4);
    QCOMPARE(page->property("latexCompletion").toString(), QStringLiteral("in{}"));
    page->setProperty("currentFile", QStringLiteral("/notes.txt"));
    QCOMPARE(page->property("latexCompletion").toString(), QString());
    page->setProperty("currentFile", QStringLiteral("/main.tex"));
    editor->setProperty("text", QStringLiteral("ordinary"));
    input->setProperty("cursorPosition", 8);
    QVERIFY(!shortcut->property("enabled").toBool());
}

void AutoLoginTests::editorScrollsLongDocuments()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QQmlComponent containerComponent(&engine);
    containerComponent.setData(
        "import QtQuick 2.5\n"
        "import Sailfish.Silica 1.0\n"
        "Item { property int status: PageStatus.Active; property int navigation: 0; "
        "property int navigationPending: 0; property int direction: 0 }", QUrl());
    QVERIFY2(containerComponent.isReady(), qPrintable(containerComponent.errorString()));
    QScopedPointer<QObject> container(containerComponent.create());
    QVERIFY(container);
    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.resize(600, 800);
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("__stack_container", QVariant::fromValue(qobject_cast<QQuickItem *>(container.data())));
    component.completeCreate();
    page->setProperty("visible", true);
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    pageItem->setParentItem(window.contentItem());
    pageItem->setSize(QSizeF(600, 800));
    QQuickItem *editor = page->findChild<QQuickItem *>(QStringLiteral("draftEditor"));
    QQuickItem *pane = page->findChild<QQuickItem *>(QStringLiteral("editorCodePane"));
    QVERIFY(editor && pane);
    QObject *loading = page->findChild<QObject *>(QStringLiteral("sourceLoadingIndicator"));
    QVERIFY(loading);
    QVERIFY(!loading->property("visible").toBool());
    client.setSourceBusy(true);
    QVERIFY(loading->property("visible").toBool());
    client.setSourceBusy(false);
    QVERIFY(!loading->property("visible").toBool());
    page->setProperty("pendingCompileRoot", QStringLiteral("main.tex"));
    client.setSourceBusy(true);
    QVERIFY(!loading->property("visible").toBool());
    client.setSourceBusy(false);
    client.setSourceError(QStringLiteral("Save failed"));
    QVERIFY(page->property("pendingCompileRoot").toString().isEmpty());
    client.setSourceError(QString());
    client.setSourceBusy(true);
    QVERIFY(loading->property("visible").toBool());
    client.setSourceBusy(false);
    QQuickItem *column = editor->parentItem();
    QVERIFY(column);
    for (QQuickItem *item : column->childItems())
        QVERIFY2(item == editor || !item->inherits("QQuickMouseArea") || item->z() <= editor->z(),
                 "An overlay must not intercept editor scrolling");
    editor->setProperty("text", QStringLiteral("Line of text\n").repeated(100));
    renderControl.polishItems();
    QTRY_VERIFY(pane->property("contentHeight").toReal() > pane->height());
    const qreal bottom = pane->property("contentHeight").toReal() - pane->height();
    QVERIFY(pane->setProperty("contentY", bottom));
    QCOMPARE(pane->property("contentY").toReal(), bottom);
    pane->setProperty("contentY", 0);
    window.show();
    const QPointF point = pane->mapToScene(QPointF(pane->width() / 2, pane->height() / 2));
    QWheelEvent wheel(point, -120, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &wheel);
    QVERIFY(pane->property("contentY").toReal() > 0);
    QObject *header = page->findChild<QObject *>(QStringLiteral("editorHeader"));
    QVERIFY(header);
    QObject *menu = header->property("pullDownMenu").value<QObject *>();
    QVERIFY(menu);
    QVERIFY(!menu->property("active").toBool());
    QWheelEvent menuOpen(QPointF(300, 5), 120, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &menuOpen);
    QVERIFY(menu->property("active").toBool());
    QVERIFY(header->property("contentY").toReal() < header->property("originY").toReal());
    QWheelEvent menuClose(QPointF(300, 5), -120, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &menuClose);
    QTRY_VERIFY(!menu->property("active").toBool());
    QWheelEvent wheelUp(point, 120, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &wheelUp);
    QCOMPARE(pane->property("contentY").toReal(), 0.0);
    page->setProperty("zoomTarget", QStringLiteral("pdf"));
    QWheelEvent editorZoomIn(point, 120, Qt::NoButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(&window, &editorZoomIn);
    QCOMPARE(client.editorFontScale, 110);
    QCOMPARE(page->property("zoomTarget").toString(), QStringLiteral("editor"));
    QCOMPARE(pane->property("contentY").toReal(), 0.0);
    QWheelEvent editorZoomOut(point, -120, Qt::NoButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(&window, &editorZoomOut);
    QCOMPARE(client.editorFontScale, 100);

    const QString pdfPath = QDir(directory.path()).filePath(QStringLiteral("scroll.pdf"));
    {
        QPdfWriter writer(pdfPath);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.drawText(100, 100, QStringLiteral("First page"));
        QVERIFY(writer.newPage());
        painter.drawText(100, 100, QStringLiteral("Second page"));
    }
    client.setPdfPath(pdfPath);
    QObject *loader = page->findChild<QObject *>(QStringLiteral("pdfLoader"));
    QVERIFY(loader);
    QObject *preview = loader->property("item").value<QObject *>();
    QVERIFY(preview);
    QTRY_COMPARE(preview->property("status").toInt(), 2);
    renderControl.polishItems();
    QQuickItem *pdfPane = page->findChild<QQuickItem *>(QStringLiteral("editorPdfPane"));
    QVERIFY(pdfPane);
    const QPointF pdfPoint = pdfPane->mapToScene(QPointF(pdfPane->width() / 2, pdfPane->height() / 2));
    page->setProperty("zoomTarget", QStringLiteral("pdf"));
    QTest::mouseMove(&window, point.toPoint());
    QTRY_COMPARE(page->property("zoomTarget").toString(), QStringLiteral("editor"));
    page->setProperty("zoomTarget", QStringLiteral("editor"));
    QTest::mouseMove(&window, pdfPoint.toPoint());
    QTRY_COMPARE(page->property("zoomTarget").toString(), QStringLiteral("pdf"));
    QSignalSpy pdfClicks(preview, SIGNAL(clicked()));
    QVERIFY(pdfClicks.isValid());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, pdfPoint.toPoint());
    QTRY_COMPARE(pdfClicks.count(), 1);
    const qreal originalZoom = preview->property("zoom").toReal();
    window.requestActivate();
    QTest::keyClick(&window, Qt::Key_Plus, Qt::ControlModifier);
    QVERIFY(preview->property("zoom").toReal() > originalZoom);
    QTest::keyClick(&window, Qt::Key_Minus, Qt::ControlModifier);
    QVERIFY(qAbs(preview->property("zoom").toReal() - originalZoom) < 0.001);
    QTest::mouseMove(&window, point.toPoint());
    QTRY_COMPARE(page->property("zoomTarget").toString(), QStringLiteral("editor"));
    QTest::keyClick(&window, Qt::Key_Plus, Qt::ControlModifier);
    QCOMPARE(client.editorFontScale, 110);
    QTest::keyClick(&window, Qt::Key_Minus, Qt::ControlModifier);
    QCOMPARE(client.editorFontScale, 100);
    QWheelEvent pdfZoomIn(pdfPoint, 120, Qt::NoButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(&window, &pdfZoomIn);
    QVERIFY(preview->property("zoom").toReal() > originalZoom);
    QCOMPARE(page->property("zoomTarget").toString(), QStringLiteral("pdf"));
    QWheelEvent pdfZoomOut(pdfPoint, -120, Qt::NoButton, Qt::ControlModifier);
    QCoreApplication::sendEvent(&window, &pdfZoomOut);
    QVERIFY(qAbs(preview->property("zoom").toReal() - originalZoom) < 0.001);
    QTest::mouseMove(&window, pdfPoint.toPoint());
    QTest::qWait(250);
    QWheelEvent pdfScroll(pdfPoint, -120, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &pdfScroll);
    QTRY_VERIFY(preview->property("contentY").toReal() > 0);
    QTest::qWait(500);
    client.setPdfPath(QString());
    QTest::qWait(100);
}

void AutoLoginTests::projectViewFiltersArchived()
{
    FakeProjectClient client;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectsPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QVERIFY(!page->property("backNavigation").toBool());
    QObject *list = page->findChild<QObject *>(QStringLiteral("projectList"));
    QObject *loading = page->findChild<QObject *>(QStringLiteral("projectsLoading"));
    QVERIFY(list);
    QVERIFY(loading);
    QVERIFY(!loading->property("enabled").toBool());
    client.setBusy(true);
    QVERIFY(loading->property("enabled").toBool());
    QCOMPARE(loading->property("text").toString(), QStringLiteral("Loading projects…"));
    client.setBusy(false);
    QVERIFY(!loading->property("enabled").toBool());
    QCOMPARE(list->property("count").toInt(), 1);
    const QVariantList active = list->property("model").toList();
    QCOMPARE(active.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Active"));
    page->setProperty("showArchived", true);
    QCOMPARE(list->property("count").toInt(), 1);
    const QVariantList archived = list->property("model").toList();
    QCOMPARE(archived.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Archived"));
}

void AutoLoginTests::opensProjectFilesByType()
{
    FakeFileClient client;
    FakeFileEditor editor;
    FakePageStack stack;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectFilesPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("projectId", QStringLiteral("project"));
    page->setProperty("editorPage", QVariant::fromValue(static_cast<QObject *>(&editor)));
    const QVariant image = QVariantMap{{QStringLiteral("type"), QStringLiteral("file")},
                                       {QStringLiteral("path"), QStringLiteral("/figures/image.png")}};
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openFile", Q_ARG(QVariant, image)));
    QCOMPARE(client.openedProject, QStringLiteral("project"));
    QCOMPARE(client.openedPath, QStringLiteral("/figures/image.png"));
    QCOMPARE(stack.pops, 0);
    const QVariant doc = QVariantMap{{QStringLiteral("type"), QStringLiteral("doc")},
                                     {QStringLiteral("path"), QStringLiteral("/main.tex")}};
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openFile", Q_ARG(QVariant, doc)));
    QCOMPARE(editor.openedPath, QStringLiteral("/main.tex"));
    QCOMPARE(stack.pops, 1);
    client.openedPath.clear();
    client.sourceBusy = true;
    emit client.sourceBusyChanged();
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openFile", Q_ARG(QVariant, image)));
    QVERIFY(client.openedPath.isEmpty());
    QCOMPARE(stack.pops, 1);
}

void AutoLoginTests::pdfPaneLoadsDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdfPath = QDir(directory.path()).filePath(QStringLiteral("preview.pdf"));
    const QString secondPath = QDir(directory.path()).filePath(QStringLiteral("preview2.pdf"));
    {
        QPdfWriter writer(pdfPath);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.drawText(100, 100, QStringLiteral("Abedul"));
    }
    {
        QPdfWriter writer(secondPath);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.drawText(100, 100, QStringLiteral("Updated"));
        QVERIFY(writer.newPage());
        painter.drawText(100, 100, QStringLiteral("Second page"));
    }
    DraftStore drafts(QDir(directory.path()).filePath(QStringLiteral("drafts")));
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("projectId", QStringLiteral("test"));
    component.completeCreate();
    page->setProperty("visible", true);
    page->setProperty("width", 600);
    page->setProperty("height", 800);
    QObject *loader = page->findChild<QObject *>(QStringLiteral("pdfLoader"));
    QObject *compileButton = page->findChild<QObject *>(QStringLiteral("pdfCompileButton"));
    QObject *progress = page->findChild<QObject *>(QStringLiteral("compileProgress"));
    QObject *progressLabel = page->findChild<QObject *>(QStringLiteral("compileProgressLabel"));
    QObject *hintTimer = page->findChild<QObject *>(QStringLiteral("compileHintTimer"));
    QVERIFY(loader);
    QVERIFY(compileButton);
    QVERIFY(progress);
    QVERIFY(progressLabel);
    QVERIFY(hintTimer);
    QVERIFY(compileButton->property("visible").toBool());
    QVERIFY(!progress->property("visible").toBool());
    page->setProperty("pendingCompileRoot", QStringLiteral("main.tex"));
    client.setSourceBusy(true);
    QVERIFY(progress->property("visible").toBool());
    QVERIFY(!compileButton->property("visible").toBool());
    QCOMPARE(progressLabel->property("text").toString(), QStringLiteral("Saving…"));
    client.setPdfBusy(true);
    client.setSourceBusy(false);
    QCOMPARE(progressLabel->property("text").toString(), QStringLiteral("Compiling…"));
    client.setPdfBusy(false);
    QVERIFY(!progress->property("visible").toBool());
    QVERIFY(compileButton->property("visible").toBool());
    QVERIFY(!hintTimer->property("running").toBool());
    client.setPdfPath(pdfPath);
    QVERIFY(!compileButton->property("visible").toBool());
    QVERIFY(hintTimer->property("running").toBool());
    client.setPdfBusy(true);
    QVERIFY(progress->property("visible").toBool());
    client.setPdfBusy(false);
    QVERIFY(!progress->property("visible").toBool());
    QObject *preview = loader->property("item").value<QObject *>();
    QVERIFY(preview);
    QTRY_COMPARE(preview->property("status").toInt(), 2);
    QCOMPARE(preview->property("count").toInt(), 1);
    QObject *geometry = page->findChild<QObject *>(QStringLiteral("pdfGeometry"));
    QVERIFY(geometry);
    QTRY_VERIFY(geometry->property("ready").toBool());
    QSignalSpy syncFailures(preview, SIGNAL(syncFailure(QString)));
    QVERIFY(syncFailures.isValid());
    QVariantMap position{{QStringLiteral("page"), 1}, {QStringLiteral("h"), 20},
                         {QStringLiteral("v"), 30}};
    QVERIFY(QMetaObject::invokeMethod(preview, "showSyncPosition",
                                     Q_ARG(QVariant, QVariant(position))));
    QCOMPARE(syncFailures.count(), 0);
    QCOMPARE(preview->property("syncHighlightPage").toInt(), 0);
    position[QStringLiteral("h")] = -1;
    QVERIFY(QMetaObject::invokeMethod(preview, "showSyncPosition",
                                     Q_ARG(QVariant, QVariant(position))));
    QCOMPARE(syncFailures.count(), 1);
    page->setProperty("zoomTarget", QStringLiteral("editor"));
    QVERIFY(QMetaObject::invokeMethod(preview, "clicked"));
    QCOMPARE(page->property("zoomTarget").toString(), QStringLiteral("pdf"));
    QPointer<QObject> oldPreview(preview);
    client.setPdfPath(QString());
    QVERIFY(compileButton->property("visible").toBool());
    QVERIFY(!hintTimer->property("running").toBool());
    client.setPdfPath(secondPath);
    QVERIFY(!hintTimer->property("running").toBool());
    preview = loader->property("item").value<QObject *>();
    QVERIFY(preview);
    QVERIFY(preview != oldPreview.data());
    QTRY_COMPARE(preview->property("count").toInt(), 2);
    QCOMPARE(preview->property("status").toInt(), 2);
}

void AutoLoginTests::pdfPaneReplacesRenderedPage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = QDir(directory.path()).filePath(QStringLiteral("first.pdf"));
    const QString secondPath = QDir(directory.path()).filePath(QStringLiteral("second.pdf"));
    for (int i = 0; i < 2; ++i) {
        QPdfWriter writer(i == 0 ? firstPath : secondPath);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.drawText(100, 100, i == 0 ? QStringLiteral("Original") : QStringLiteral("Updated"));
    }

    DraftStore drafts(QDir(directory.path()).filePath(QStringLiteral("drafts")));
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("test"));
    component.completeCreate();
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    QVERIFY(pageItem);
    pageItem->setSize(QSizeF(600, 800));
    QObject *loader = page->findChild<QObject *>(QStringLiteral("pdfLoader"));
    QVERIFY(loader);
    auto pageContainer = [](QObject *preview) -> QObject * {
        for (QObject *child : preview->findChildren<QObject *>()) {
            if (child->inherits("PdfPageContainer"))
                return child;
        }
        return nullptr;
    };
    client.setPdfPath(firstPath);
    QObject *preview = loader->property("item").value<QObject *>();
    QVERIFY(preview);
    QTRY_COMPARE(preview->property("status").toInt(), 2);
    QTRY_VERIFY(pageContainer(preview));
    QPointer<QObject> original(pageContainer(preview));
    QPointer<QObject> oldPreview(preview);
    client.setPdfPath(QString());
    client.setPdfPath(secondPath);
    preview = loader->property("item").value<QObject *>();
    QVERIFY(preview);
    QVERIFY(preview != oldPreview.data());
    QTRY_COMPARE(preview->property("pdfPath").toString(), secondPath);
    QTRY_COMPARE(preview->property("status").toInt(), 2);
    QTRY_VERIFY(original.isNull());
    QTRY_VERIFY(pageContainer(preview));
    QPointer<QObject> second(pageContainer(preview));
    client.setPdfPath(QString());
    client.setPdfPath(firstPath);
    preview = loader->property("item").value<QObject *>();
    QVERIFY(preview);
    QTRY_COMPARE(preview->property("status").toInt(), 2);
    QTRY_COMPARE(preview->property("pdfPath").toString(), firstPath);
    QTRY_VERIFY(second.isNull());
    QTRY_VERIFY(pageContainer(preview));
}

void AutoLoginTests::sourcePreservesLocalEdits()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("test"));
    component.completeCreate();
    page->setProperty("visible", true);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QObject *status = page->findChild<QObject *>(QStringLiteral("sourceStatus"));
    QVERIFY(editor);
    QVERIFY(status);
    QVERIFY(!status->property("visible").toBool());
    const QString serverText = QStringLiteral("\\ref{section} $x + \\alpha$ \\newcommand \\foobar \\% % comment");
    client.setSource(QStringLiteral("/main.tex"), serverText);
    QCOMPARE(editor->property("text").toString(), serverText);
    QVERIFY(!status->property("visible").toBool());
    QObject *highlighter = page->property("highlighter").value<QObject *>();
    QVERIFY(highlighter);
    QQuickTextDocument *quickDocument = highlighter->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(quickDocument);
    QTRY_VERIFY(!quickDocument->textDocument()->firstBlock().layout()->additionalFormats().isEmpty());
    const auto ranges = quickDocument->textDocument()->firstBlock().layout()->additionalFormats();
    auto colorAt = [&ranges](int pos) {
        for (const auto &range : ranges) {
            if (pos >= range.start && pos < range.start + range.length)
                return range.format.foreground().color();
        }
        return QColor();
    };
    const QColor commandColor = colorAt(0);
    const QColor argumentColor = colorAt(serverText.indexOf(QStringLiteral("section")));
    const QColor mathColor = colorAt(serverText.indexOf(QLatin1Char('x')));
    const QColor commentColor = colorAt(serverText.lastIndexOf(QLatin1Char('%')));
    QVERIFY(commandColor.isValid());
    QVERIFY(argumentColor.isValid());
    QVERIFY(mathColor.isValid());
    QVERIFY(commentColor.isValid());
    QVERIFY(commandColor != argumentColor);
    QVERIFY(argumentColor != mathColor);
    QVERIFY(mathColor != commentColor);
    QCOMPARE(colorAt(serverText.indexOf(QStringLiteral("\\alpha"))), commandColor);
    QCOMPARE(colorAt(serverText.indexOf(QStringLiteral("\\newcommand"))), commandColor);
    QCOMPARE(colorAt(serverText.indexOf(QStringLiteral("\\foobar"))), commandColor);
    QCOMPARE(colorAt(serverText.indexOf(QStringLiteral("\\%"))), commandColor);
    editor->setProperty("text", QStringLiteral("local edits"));
    QVERIFY(status->property("visible").toBool());
    QTRY_VERIFY(!page->property("dirty").toBool());
    client.setSource(QStringLiteral("/other.tex"), QStringLiteral("other"));
    QCOMPARE(editor->property("text").toString(), QStringLiteral("other"));
    client.setSource(QStringLiteral("/main.tex"), QStringLiteral("changed server text"));
    QCOMPARE(editor->property("text").toString(), QStringLiteral("local edits"));
}

void AutoLoginTests::latexHighlightingPreservesText()
{
    QTextDocument document;
    LatexSyntaxHighlighter highlighter(&document);
    highlighter.commandColor = Qt::red;
    highlighter.commentColor = Qt::gray;
    highlighter.mathColor = Qt::blue;
    const QString text = QStringLiteral("\\section{A} $x$ \\% not a comment % comment\n$$\ny\n$$");
    document.setPlainText(text);
    highlighter.rehighlight();
    QCOMPARE(document.toPlainText(), text);
    const QTextBlock first = document.firstBlock();
    const auto ranges = first.layout()->additionalFormats();
    QVERIFY(!ranges.isEmpty());
    bool command = false, math = false, comment = false;
    for (const auto &range : ranges) {
        if (range.start == 0 && range.format.foreground().color() == QColor(Qt::red))
            command = true;
        if (range.format.foreground().color() == QColor(Qt::blue))
            math = true;
        if (range.start == text.indexOf(QStringLiteral("% comment"))
                && range.format.foreground().color() == QColor(Qt::gray))
            comment = true;
    }
    QVERIFY(command);
    QVERIFY(math);
    QVERIFY(comment);
    QCOMPARE(first.next().userState(), 2);
    QCOMPARE(first.next().next().userState(), 2);
    QCOMPARE(first.next().next().next().userState(), 0);
    document.setPlainText(QStringLiteral("\\alpha"));
    highlighter.rehighlight();
    QCOMPARE(document.toPlainText(), QStringLiteral("\\alpha"));
    QVERIFY(!document.firstBlock().layout()->additionalFormats().isEmpty());
}

void AutoLoginTests::highlightsLatexContexts()
{
    QTextDocument document;
    LatexSyntaxHighlighter highlighter(&document);
    highlighter.commandColor = Qt::red;
    highlighter.commentColor = Qt::gray;
    highlighter.mathColor = Qt::blue;
    highlighter.argumentColor = Qt::green;
    const QString text = QStringLiteral(
                "\\ref{sec:intro} \\verb|% $| % comment\n"
                "\\[\nx + y\n\\]\n"
                "\\begin{align*}\na + b\n\\end{align*}\n"
                "\\begin{verbatim}\n% \\section{$}\n\\end{verbatim}\n"
                "\\% text % real comment");
    document.setPlainText(text);
    highlighter.rehighlight();
    auto colorAt = [](const QTextBlock &block, int pos) {
        const auto ranges = block.layout()->additionalFormats();
        for (const auto &range : ranges) {
            if (pos >= range.start && pos < range.start + range.length)
                return range.format.foreground().color();
        }
        return QColor();
    };
    QTextBlock block = document.firstBlock();
    QCOMPARE(colorAt(block, 0), QColor(Qt::red));
    QCOMPARE(colorAt(block, 6), QColor(Qt::green));
    QCOMPARE(colorAt(block, block.text().indexOf(QLatin1Char('%'))), QColor(Qt::green));
    QCOMPARE(colorAt(block, block.text().lastIndexOf(QLatin1Char('%'))), QColor(Qt::gray));
    block = block.next();
    QCOMPARE(block.userState(), 4);
    block = block.next();
    QCOMPARE(colorAt(block, 0), QColor(Qt::blue));
    block = block.next();
    QCOMPARE(block.userState(), 0);
    block = block.next();
    QVERIFY(block.userState() >= 10);
    block = block.next();
    QCOMPARE(colorAt(block, 0), QColor(Qt::blue));
    block = block.next();
    QCOMPARE(block.userState(), 0);
    block = block.next();
    QVERIFY(block.userState() >= 100);
    block = block.next();
    QCOMPARE(colorAt(block, 0), QColor(Qt::green));
    QCOMPARE(colorAt(block, 2), QColor(Qt::green));
    block = block.next();
    QCOMPARE(block.userState(), 0);
    block = block.next();
    QCOMPARE(colorAt(block, 0), QColor(Qt::red));
    QCOMPARE(colorAt(block, block.text().lastIndexOf(QLatin1Char('%'))), QColor(Qt::gray));
    QCOMPARE(document.toPlainText(), text);
}

void AutoLoginTests::highlightsBibliography()
{
    QTextDocument document;
    LatexSyntaxHighlighter highlighter(&document);
    highlighter.bibMode = true;
    highlighter.commandColor = Qt::red;
    highlighter.commentColor = Qt::gray;
    highlighter.mathColor = Qt::blue;
    highlighter.argumentColor = Qt::green;
    document.setPlainText(QStringLiteral("@article{smith,\n  title = \"100% useful\",\n  year = 2024 % comment\n}"));
    highlighter.rehighlight();
    auto colorAt = [](const QTextBlock &block, int pos) {
        for (const auto &range : block.layout()->additionalFormats()) {
            if (pos >= range.start && pos < range.start + range.length)
                return range.format.foreground().color();
        }
        return QColor();
    };
    const QTextBlock first = document.firstBlock();
    QCOMPARE(colorAt(first, 0), QColor(Qt::red));
    QCOMPARE(colorAt(first, 9), QColor(Qt::green));
    const QTextBlock second = first.next();
    QCOMPARE(colorAt(second, second.text().indexOf(QStringLiteral("title"))), QColor(Qt::red));
    QCOMPARE(colorAt(second, second.text().indexOf(QLatin1Char('%'))), QColor(Qt::blue));
    const QTextBlock third = second.next();
    QCOMPARE(colorAt(third, third.text().indexOf(QLatin1Char('%'))), QColor(Qt::gray));
}

void AutoLoginTests::compileWaitsForSave()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("test"));
    component.completeCreate();
    client.setSource(QStringLiteral("/main.tex"), QStringLiteral("old"));
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    editor->setProperty("text", QStringLiteral("new"));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "saveAndCompile"));
    QCOMPARE(client.saves, 1);
    QCOMPARE(client.compiles, 0);
    emit client.sourceSaved(QStringLiteral("/main.tex"), QStringLiteral("new"));
    QCOMPARE(client.compiles, 1);
    QCOMPARE(client.root, QStringLiteral("/main.tex"));
}

void AutoLoginTests::saveShortcutOnlyOnActiveEditor()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent editorComponent(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(editorComponent.isReady(), qPrintable(editorComponent.errorString()));
    QQmlComponent containerComponent(&engine);
    containerComponent.setData(
        "import QtQuick 2.5\n"
        "import Sailfish.Silica 1.0\n"
        "Item { property int status: PageStatus.Inactive; property int activeStatus: PageStatus.Active; "
        "property int inactiveStatus: PageStatus.Inactive; property int navigation: 0; "
        "property int navigationPending: 0; property int direction: 0 }", QUrl());
    QVERIFY2(containerComponent.isReady(), qPrintable(containerComponent.errorString()));
    QScopedPointer<QObject> container(containerComponent.create());
    QScopedPointer<QObject> attachedContainer(containerComponent.create());
    QVERIFY(container);
    QVERIFY(attachedContainer);
    QScopedPointer<QObject> page(editorComponent.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("test"));
    page->setProperty("__stack_container", QVariant::fromValue(qobject_cast<QQuickItem *>(container.data())));
    editorComponent.completeCreate();
    QScopedPointer<QObject> attached(editorComponent.beginCreate(engine.rootContext()));
    QVERIFY(attached);
    attached->setProperty("projectId", QStringLiteral("test"));
    attached->setProperty("deferOpen", true);
    attached->setProperty("__stack_container", QVariant::fromValue(qobject_cast<QQuickItem *>(attachedContainer.data())));
    editorComponent.completeCreate();

    QObject *shortcut = page->findChild<QObject *>(QStringLiteral("saveAndCompileShortcut"));
    QObject *attachedShortcut = attached->findChild<QObject *>(QStringLiteral("saveAndCompileShortcut"));
    QVERIFY(shortcut);
    QVERIFY(attachedShortcut);
    QCOMPARE(shortcut->property("portableText").toString(), QStringLiteral("Ctrl+S"));
    QVERIFY(!shortcut->property("enabled").toBool());
    QVERIFY(!attachedShortcut->property("enabled").toBool());
    container->setProperty("status", container->property("activeStatus"));
    QVERIFY(shortcut->property("enabled").toBool());
    QVERIFY(!attachedShortcut->property("enabled").toBool());
    client.setSource(QStringLiteral("/main.tex"), QStringLiteral("content"));
    QVERIFY(QMetaObject::invokeMethod(shortcut, "activated"));
    QCOMPARE(client.saves, 1);
    container->setProperty("status", container->property("inactiveStatus"));
    attachedContainer->setProperty("status", attachedContainer->property("activeStatus"));
    QVERIFY(!shortcut->property("enabled").toBool());
    QVERIFY(attachedShortcut->property("enabled").toBool());
}

void AutoLoginTests::reopensLastFile()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QVERIFY(drafts.saveLastFile(client.server(), QStringLiteral("test"), QStringLiteral("/chapter.tex")));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("test"));
    component.completeCreate();
    QCOMPARE(client.openedPath, QStringLiteral("/chapter.tex"));
    client.setSource(QStringLiteral("/other.bib"), QStringLiteral("content"));
    QCOMPARE(drafts.loadLastFile(client.server(), QStringLiteral("test")), QStringLiteral("/other.bib"));
}

void AutoLoginTests::attachedEditorDefersInitialization()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QVERIFY(drafts.saveLastFile(client.server(), QStringLiteral("test"), QStringLiteral("/chapter.tex")));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("test"));
    page->setProperty("deferOpen", true);
    component.completeCreate();
    QVERIFY(!page->property("started").toBool());
    QVERIFY(client.openedPath.isEmpty());
    page.reset();
    QCOMPARE(client.sourceCloses, 0);
    QCOMPARE(client.pdfCloses, 0);
}

void AutoLoginTests::restoresServerAtStartup()
{
    FakeLoginClient client;
    FakePageStack stack;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("LoginPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QVERIFY(page->property("restoringServer").toBool());
    QTRY_COMPARE(stack.pushes, 1);
    QCOMPARE(client.selected, QStringLiteral("https://example.org"));
    QVERIFY(stack.lastUrl.toString().endsWith(QStringLiteral("WebLoginPage.qml")));
    QVERIFY(stack.pushProperties.value(QStringLiteral("restoringSession")).toBool());
    QObject *repeater = page->findChild<QObject *>(QStringLiteral("previousServers"));
    QVERIFY(repeater);
    QQuickItem *previous = nullptr;
    QVERIFY(QMetaObject::invokeMethod(repeater, "itemAt", Q_RETURN_ARG(QQuickItem *, previous),
                                      Q_ARG(int, 1)));
    QVERIFY(previous);
    QCOMPARE(previous->property("text").toString(), QStringLiteral("https://other.example"));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openServer",
                                     Q_ARG(QVariant, previous->property("text"))));
    QCOMPARE(client.selected, QStringLiteral("https://other.example"));
    QCOMPARE(stack.pushes, 2);
}

void AutoLoginTests::showsServerSelectionWhenRestoreUnavailable()
{
    FakeLoginClient client;
    FakePageStack stack;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("LoginPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    client.remembered.clear();
    QScopedPointer<QObject> firstPage(component.create());
    QVERIFY2(firstPage, qPrintable(component.errorString()));
    QVERIFY(!firstPage->property("restoringServer").toBool());
    QTest::qWait(50);
    QCOMPARE(stack.pushes, 0);

    client.remembered = QStringLiteral("https://example.org");
    client.acceptServer = false;
    QScopedPointer<QObject> failedPage(component.create());
    QVERIFY2(failedPage, qPrintable(component.errorString()));
    QTRY_VERIFY(!failedPage->property("restoringServer").toBool());
    QCOMPARE(stack.pushes, 0);
}

void AutoLoginTests::editorPulleyWheelIsLimitedToHeader()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    view.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(600, 800);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    view.setSource(base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty()
             ? QString() : view.errors().first().toString()));
    QQuickItem *page = view.rootObject();
    QQuickItem *wheelArea = page->findChild<QQuickItem *>(QStringLiteral("editorPulleyWheel"));
    QVERIFY(wheelArea);
    FakePulley menu;
    FakeFlickable flickable;
    flickable.menu = &menu;
    wheelArea->setProperty("flickable", QVariant::fromValue(static_cast<QObject *>(&flickable)));
    const auto wheel = [wheelArea](qreal delta) {
        return QMetaObject::invokeMethod(wheelArea, "moveWheel", Q_ARG(QVariant, delta));
    };
    QVERIFY(wheelArea->height() < 700);
    QVERIFY(!menu.active);
    QVERIFY(wheel(30));
    QVERIFY(menu.active);
    const qreal first = flickable.contentY;
    QVERIFY(first < flickable.originY);
    QVERIFY(wheel(30));
    QVERIFY(flickable.contentY < first);
    QVERIFY(wheel(-30));
    QVERIFY(wheel(-30));
    QVERIFY(!menu.active);
}

void AutoLoginTests::mouseButtonsFollowNavigationRules()
{
    FakePageStack stack;
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("navigationStack"), &stack);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(300, 300);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    view.setSource(base.resolved(QUrl(QStringLiteral("MouseNavigation.qml"))));
    QVERIFY(view.status() == QQuickView::Ready);
    view.rootObject()->setProperty("stack", QVariant::fromValue(static_cast<QObject *>(&stack)));
    const auto click = [&view](int button) {
        return QMetaObject::invokeMethod(view.rootObject(), "navigate", Q_ARG(QVariant, button));
    };
    QVERIFY(click(Qt::BackButton));
    QVERIFY(click(Qt::ForwardButton));
    QCOMPARE(stack.backwards, 1);
    QCOMPARE(stack.forwards, 1);
    stack.backNavigation = false;
    stack.forwardNavigation = false;
    QVERIFY(click(Qt::BackButton));
    QVERIFY(click(Qt::ForwardButton));
    QCOMPARE(stack.backwards, 1);
    QCOMPARE(stack.forwards, 1);
    stack.backNavigation = true;
    stack.busy = true;
    QVERIFY(click(Qt::BackButton));
    QCOMPARE(stack.backwards, 1);
}

void AutoLoginTests::editorSettingsAndZoom()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    pageItem->setSize(QSizeF(800, 600));
    QQuickItem *code = page->findChild<QQuickItem *>(QStringLiteral("editorCodePane"));
    QQuickItem *pdf = page->findChild<QQuickItem *>(QStringLiteral("editorPdfPane"));
    QVERIFY(code && pdf);
    QVERIFY(code->x() < pdf->x());
    client.pdfOnLeft = true;
    emit client.pdfOnLeftChanged();
    QVERIFY(pdf->x() < code->x());
    pageItem->setSize(QSizeF(600, 800));
    QCOMPARE(code->x(), 0.0);
    QCOMPARE(pdf->x(), 0.0);
    QVERIFY(code->y() < pdf->y());
    QVERIFY(QMetaObject::invokeMethod(page.data(), "adjustZoom", Q_ARG(QVariant, 1)));
    QCOMPARE(client.editorFontScale, 110);
    QVERIFY(QMetaObject::invokeMethod(page.data(), "adjustZoom", Q_ARG(QVariant, -1)));
    QCOMPARE(client.editorFontScale, 100);

    QQmlComponent settings(&engine, base.resolved(QUrl(QStringLiteral("SettingsPage.qml"))));
    QVERIFY2(settings.isReady(), qPrintable(settings.errorString()));
    client.editorFontScale = 130;
    QScopedPointer<QObject> settingsPage(settings.create());
    QVERIFY2(settingsPage, qPrintable(settings.errorString()));
    QCOMPARE(client.editorFontScale, 130);
    QObject *slider = settingsPage->findChild<QObject *>(QStringLiteral("editorFontScaleSetting"));
    QVERIFY(slider);
    slider->setProperty("value", 140);
    QCOMPARE(client.editorFontScale, 140);
}

void AutoLoginTests::settingsPersistAcrossClients()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString oldOrg = QCoreApplication::organizationName();
    const QString oldApp = QCoreApplication::applicationName();
    const QSettings::Format oldFormat = QSettings::defaultFormat();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("AbedulTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SettingsTest"));
    {
        OverleafClient first;
        first.setPdfOnLeft(true);
        first.setEditorFontScale(130);
        OverleafClient second;
        QCOMPARE(second.pdfOnLeft(), true);
        QCOMPARE(second.editorFontScale(), 130);
    }
    QCoreApplication::setOrganizationName(oldOrg);
    QCoreApplication::setApplicationName(oldApp);
    QSettings::setDefaultFormat(oldFormat);
}

void AutoLoginTests::changesServerFromProjects()
{
    FakeProjectClient client;
    FakePageStack stack;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectsPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QObject *menu = page->findChild<QObject *>(QStringLiteral("changeServer"));
    QVERIFY(menu);
    QVERIFY(QMetaObject::invokeMethod(menu, "clicked"));
    QCOMPARE(stack.pops, 1);
}

void AutoLoginTests::restoresLastProjectNavigation()
{
    FakeProjectClient client;
    FakePageStack stack;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QVERIFY(drafts.saveLastProject(client.server(), QStringLiteral("one")));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectsPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QCOMPARE(page->property("lastProjectId").toString(), QStringLiteral("one"));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "attachLastProject"));
    QCOMPARE(stack.attachedProperties.value(QStringLiteral("projectId")).toString(), QStringLiteral("one"));
    QVERIFY(stack.attachedProperties.value(QStringLiteral("deferOpen")).toBool());
    const QVariantMap other{{QStringLiteral("id"), QStringLiteral("two")},
                            {QStringLiteral("name"), QStringLiteral("Archived")}};
    QVERIFY(QMetaObject::invokeMethod(page.data(), "rememberProject", Q_ARG(QVariant, other)));
    QCOMPARE(drafts.loadLastProject(client.server()), QStringLiteral("two"));
    QCOMPARE(page->property("lastProjectId").toString(), QStringLiteral("two"));
    page->setProperty("lastProjectId", QStringLiteral("three"));
    stack.attachedProperties.clear();
    QVERIFY(QMetaObject::invokeMethod(page.data(), "attachLastProject"));
    QVERIFY(stack.attachedProperties.isEmpty());
}

QTEST_MAIN(AutoLoginTests)
#include "autologin_tests.moc"
