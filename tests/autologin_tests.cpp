#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QJSValue>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QDir>
#include <QImage>
#include <QFontMetricsF>
#include <QPainter>
#include <QPdfWriter>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QWheelEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QNetworkProxy>
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
#include "openrouterclient.h"
#include "wolframalphaclient.h"
#include "reversoclient.h"
#include "compileoutput.h"
#include "projectcollaboration.h"

class SingleImageProvider : public QQuickImageProvider
{
public:
    SingleImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        if (++requests > 1)
            return QImage();
        QImage image(40, 20, QImage::Format_ARGB32);
        image.fill(Qt::black);
        if (size)
            *size = image.size();
        return image;
    }
    int requests = 0;
};

class FakeCollaboration : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* entriesModel READ entriesModel CONSTANT)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    QAbstractItemModel *entriesModel() { return &model; }
    QVariantList entries() const { return model.entries(); }
    bool busy() const { return busyValue; }
    QString error() const { return errorValue; }
    QString errorValue;
    bool busyValue = false;
    int refreshCount = 0;
    Q_INVOKABLE void refresh() { ++refreshCount; busyValue = true; emit changed(); }
    void finishRefresh() { busyValue = false; emit changed(); }
    Q_INVOKABLE void open(const QString &, const QString &) {}
    Q_INVOKABLE void close() {}
    CollaborationEntriesModel model;
signals:
    void changed();
    void submitted();
};

class FakeCollaborationHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* collaboration READ collaboration CONSTANT)
public:
    QObject *collaboration() { return &client; }
    FakeCollaboration client;
};

class FakeProjectClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error CONSTANT)
    Q_PROPERTY(QString server READ server CONSTANT)
    Q_PROPERTY(bool coverShowsProjects MEMBER coverShowsProjects)
    Q_PROPERTY(bool authenticated MEMBER authenticated)
    Q_PROPERTY(bool projectCreationBusy MEMBER projectCreationBusy NOTIFY projectCreationChanged)
    Q_PROPERTY(QString projectCreationError MEMBER projectCreationError NOTIFY projectCreationChanged)
    Q_PROPERTY(QString projectActionError MEMBER projectActionError NOTIFY projectActionChanged)
    Q_PROPERTY(QObject *coverEditor MEMBER coverEditor)
    Q_PROPERTY(bool projectsCached MEMBER projectsCached)
    Q_PROPERTY(bool offline MEMBER offline)
public:
    bool coverShowsProjects = true;
    bool authenticated = true;
    bool projectCreationBusy = false;
    QString projectCreationError;
    QString projectActionError;
    QObject *coverEditor = nullptr;
    bool projectsCached = false;
    bool offline = false;
    QString createdName;
    QString actionProject;
    QString actionName;
    int refreshCount = 0;
    Q_INVOKABLE void createProject(const QString &name) { createdName = name; }
    Q_INVOKABLE void changeProjectTrash(const QString &project, const QString &action)
    { actionProject = project; actionName = action; }
    Q_INVOKABLE void refreshProjects() { ++refreshCount; }
    Q_INVOKABLE void openMainProjectSource(const QString &) {}
    Q_INVOKABLE void setCoverEditor(QObject *editor) { coverEditor = editor; }
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
    void projectCreationChanged();
    void projectActionChanged();
    void projectTrashChanged(const QString &projectId, const QString &action);
private:
    bool m_busy = false;
};

class FakeFileClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projectFiles READ projectFiles CONSTANT)
    Q_PROPERTY(QString server READ server CONSTANT)
    Q_PROPERTY(QString filesError READ filesError NOTIFY filesErrorChanged)
    Q_PROPERTY(bool filesBusy READ filesBusy NOTIFY filesBusyChanged)
    Q_PROPERTY(bool sourceBusy MEMBER sourceBusy NOTIFY sourceBusyChanged)
    Q_PROPERTY(bool offline MEMBER offline NOTIFY offlineChanged)
    Q_PROPERTY(QString sourceError MEMBER sourceError NOTIFY sourceErrorChanged)
public:
    QVariantList projectFiles() const { return files; }
    QString server() const { return QStringLiteral("https://example.org"); }
    QVariantList files;
    QString filesError() const { return filesErrorValue; }
    QString filesErrorValue;
    bool filesBusy() const { return filesBusyValue; }
    bool filesBusyValue = false;
    bool asyncLoad = false;
    bool offline = false;
    int loadCount = 0;
    bool sourceBusy = false;
    QString sourceError;
    QString openedPath;
    QString openedProject;
    QString operation;
    QString operationPath;
    QList<QUrl> uploads;
    QString overwriteId;
    QString overwriteType;
    Q_INVOKABLE void loadProjectFiles(const QString &)
    {
        ++loadCount;
        if (asyncLoad) {
            filesBusyValue = false;
            emit filesBusyChanged();
            filesBusyValue = true;
            emit filesBusyChanged();
        }
    }
    void finishFileLoad() { filesBusyValue = false; emit filesBusyChanged(); }
    Q_INVOKABLE void closeProjectFiles() {}
    Q_INVOKABLE void loadProjectFileTree(const QString &) {}
    Q_INVOKABLE void openProjectFileExternally(const QString &project, const QString &path)
    { openedProject = project; openedPath = path; }
    Q_INVOKABLE void renameProjectEntity(const QString &, const QString &path, const QString &, const QString &)
    { operation = QStringLiteral("rename"); operationPath = path; }
    Q_INVOKABLE void deleteProjectEntity(const QString &, const QString &path, const QString &)
    { operation = QStringLiteral("delete"); operationPath = path; }
    Q_INVOKABLE void uploadProjectFile(const QString &, const QString &path, const QUrl &url)
    { operation = QStringLiteral("upload"); operationPath = path; uploads.append(url); }
    Q_INVOKABLE void overwriteProjectFile(const QString &, const QString &path, const QUrl &url,
                                           const QString &id, const QString &type)
    { operation = QStringLiteral("overwrite"); operationPath = path; uploads.append(url); overwriteId = id; overwriteType = type; }
signals:
    void offlineChanged();
    void sourceBusyChanged();
    void sourceErrorChanged();
    void sourceSaved(const QString &path, const QString &text);
    void projectFileOperationFinished(const QString &action, const QString &path,
                                      const QString &type, const QString &newPath);
    void filesBusyChanged();
    void filesErrorChanged();
    void projectFileOverwriteRequested(const QString &path, const QString &type, const QString &entityId);
    void externalFileReady(const QUrl &url);
};

class FakeFileEditor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dirty MEMBER dirty)
    Q_PROPERTY(QString currentFile MEMBER currentFile)
    Q_PROPERTY(QString projectId MEMBER projectId)
public:
    bool dirty = false;
    QString currentFile;
    QString projectId;
    QString openedPath;
    int saves = 0;
    bool saveSuccess = true;
    bool finishSuccess = true;
    QString changedTo;
    Q_INVOKABLE void saveDraft() {}
    Q_INVOKABLE bool openProjectFile(const QString &path) { openedPath = path; return true; }
    Q_INVOKABLE bool saveBeforeFileOperation() { ++saves; return saveSuccess; }
    Q_INVOKABLE bool finishFileOperationSave(const QString &, const QString &) { return finishSuccess; }
    Q_INVOKABLE void fileOperationFinished(const QString &, const QString &, const QString &, const QString &newPath)
    { changedTo = newPath; }
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
    QObject *pushedComponent = nullptr;
    Q_PROPERTY(bool busy MEMBER busy)
    Q_PROPERTY(bool backNavigation MEMBER backNavigation)
    Q_PROPERTY(bool forwardNavigation MEMBER forwardNavigation)
    bool busy = false;
    bool backNavigation = true;
    bool forwardNavigation = true;
    Q_INVOKABLE void pop() { ++pops; }
    Q_INVOKABLE void pop(QObject *) { ++pops; }
    Q_INVOKABLE void navigateBack() { ++backwards; }
    Q_INVOKABLE void navigateForward() { ++forwards; }
    Q_INVOKABLE QObject *nextPage(QObject *) { return nullptr; }
    Q_INVOKABLE void push(const QUrl &url) { ++pushes; lastUrl = url; }
    Q_INVOKABLE void push(QObject *component) { ++pushes; pushedComponent = component; }
    Q_INVOKABLE void push(QObject *component, const QVariantMap &properties)
    { ++pushes; pushedComponent = component; pushProperties = properties; }
    Q_INVOKABLE void push(const QUrl &url, const QVariantMap &properties)
    { ++pushes; lastUrl = url; pushProperties = properties; }
    Q_INVOKABLE void pushAttached(const QUrl &url, const QVariantMap &properties)
    { lastUrl = url; attachedProperties = properties; }
};

class FakeAiProvider : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool authenticated MEMBER authenticated NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(QString error MEMBER error NOTIFY changed)
    Q_PROPERTY(QString authorizationUrl MEMBER authorizationUrl NOTIFY changed)
public:
    bool authenticated = true;
    bool busy = false;
    QString error;
    QString prompt;
    QString context;
    QString authorizationUrl = QStringLiteral("https://openrouter.ai/auth");
    QUrl callback;
    Q_INVOKABLE void requestEdit(const QString &instruction, const QString &source)
    { prompt = instruction; context = source; }
    int cancellations = 0;
    Q_INVOKABLE void cancel() { ++cancellations; busy = false; emit changed(); }
    Q_INVOKABLE bool handleAuthorizationUrl(const QUrl &url)
    { callback = url; return url.path() == QStringLiteral("/abedul-openrouter"); }
signals:
    void changed();
    void resultReady(const QString &text);
};

class IsolatedAiSettings
{
public:
    IsolatedAiSettings()
        : oldOrganization(QCoreApplication::organizationName()),
          oldApplication(QCoreApplication::applicationName()),
          oldFormat(QSettings::defaultFormat())
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName(QStringLiteral("AbedulAiTest"));
        QCoreApplication::setApplicationName(QStringLiteral("AiSettings"));
    }
    ~IsolatedAiSettings()
    {
        QCoreApplication::setOrganizationName(oldOrganization);
        QCoreApplication::setApplicationName(oldApplication);
        QSettings::setDefaultFormat(oldFormat);
    }
private:
    QTemporaryDir directory;
    QString oldOrganization;
    QString oldApplication;
    QSettings::Format oldFormat;
};

class IsolatedAiData
{
public:
    IsolatedAiData() : previous(qgetenv("XDG_DATA_HOME"))
    {
        qputenv("XDG_DATA_HOME", directory.path().toUtf8());
    }
    ~IsolatedAiData()
    {
        if (previous.isNull())
            qunsetenv("XDG_DATA_HOME");
        else
            qputenv("XDG_DATA_HOME", previous);
    }
    bool saveKey()
    {
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("openrouter"));
        if (!QDir().mkpath(path)
                || !QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
            return false;
        QFile file(QDir(path).filePath(QStringLiteral("session")));
        if (!file.open(QIODevice::WriteOnly)
                || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner))
            return false;
        return file.write("sk-or-test-key") == 14;
    }
private:
    QTemporaryDir directory;
    QByteArray previous;
};

class LocalOnlyProxy
{
public:
    LocalOnlyProxy() : previous(QNetworkProxy::applicationProxy())
    {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 9));
    }
    ~LocalOnlyProxy() { QNetworkProxy::setApplicationProxy(previous); }
private:
    QNetworkProxy previous;
};

class FakeAiEditor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentFile MEMBER currentFile)
public:
    QString currentFile = QStringLiteral("main.tex");
    QString text = QStringLiteral("Before selection after");
    QString inserted;
    int start = -1;
    int end = -1;
    int applications = 0;
    Q_INVOKABLE QString aiDocumentText() const { return text; }
    Q_INVOKABLE int aiCursorPosition() const { return 2; }
    Q_INVOKABLE bool applyAiGeneration(const QString &file, const QString &original,
                                       int from, int to, const QString &result)
    {
        if (file != currentFile || original != text || from < 0 || to < from || to > text.length())
            return false;
        start = from;
        end = to;
        ++applications;
        inserted = result;
        return true;
    }
};

class FakeLoginClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString rememberedServer READ rememberedServer CONSTANT)
    Q_PROPERTY(QStringList rememberedServers READ rememberedServers CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)
    Q_PROPERTY(QVariantList cachedAccounts MEMBER cachedAccounts)
    Q_PROPERTY(bool autoRestoreServer MEMBER autoRestoreServer)
public:
    QString rememberedServer() const { return remembered; }
    QStringList rememberedServers() const { return {QStringLiteral("https://example.org"),
                                                   QStringLiteral("https://other.example")}; }
    QString error() const { return QString(); }
    QString selected;
    QString remembered = QStringLiteral("https://example.org");
    QVariantList cachedAccounts;
    bool autoRestoreServer = true;
    QString cachedUser;
    QString removedAccount;
    QString requestedServer;
    bool acceptServer = true;
    Q_INVOKABLE bool setServer(const QString &address) { selected = address; return acceptServer; }
    Q_INVOKABLE bool openOfflineAccount(const QString &server, const QString &user)
    { selected = server; cachedUser = user; return true; }
    Q_INVOKABLE bool removeCachedAccount(const QString &server, const QString &user)
    { selected = server; removedAccount = user; return true; }
    Q_INVOKABLE void requestServerRemoval(const QString &server) { requestedServer = server; }
};

class FakeBackgroundClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool offline MEMBER offline NOTIFY offlineChanged)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY busyChanged)
    Q_PROPERTY(bool interactiveCookiePending MEMBER interactiveCookiePending NOTIFY interactiveCookiePendingChanged)
    Q_PROPERTY(bool serverRemovalPending MEMBER serverRemovalPending NOTIFY serverRemovalPendingChanged)
public:
    bool offline = false;
    bool busy = false;
    bool interactiveCookiePending = false;
    bool serverRemovalPending = false;
    int attempts = 0;
    Q_INVOKABLE void resumeSavedSession() { ++attempts; }
signals:
    void offlineChanged();
    void busyChanged();
    void interactiveCookiePendingChanged();
    void serverRemovalPendingChanged();
    void reconnectRequested();
};

class FakeRemovalClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool interactiveCookiePending MEMBER pending NOTIFY interactiveCookiePendingChanged)
public:
    bool pending = false;
    QString error;
    QString removed;
    Q_INVOKABLE void setInteractiveCookiePending(bool value)
    { pending = value; emit interactiveCookiePendingChanged(); }
    Q_INVOKABLE void reportAccountRemovalError(const QString &value) { error = value; }
    Q_INVOKABLE void setServerRemovalPending(bool) {}
    Q_INVOKABLE QString cookieRemovalUrl(const QString &server, const QString &domain, const QString &path)
    { return domain == QStringLiteral("example.org") ? server + path : QString(); }
    Q_INVOKABLE bool removeSavedServer(const QString &server) { removed = server; return true; }
signals:
    void interactiveCookiePendingChanged();
};

class FakeRemovalCookies : public QObject
{
    Q_OBJECT
public:
    bool allowRead = true;
    bool allowDelete = true;
    int reads = 0;
    QStringList deleted;
    Q_INVOKABLE bool retrieveAllCookies() { ++reads; return allowRead; }
    Q_INVOKABLE bool deleteCookies(const QString &url, const QString &name)
    { deleted.append(url + QLatin1Char(' ') + name); return allowDelete; }
signals:
    void allCookiesAvailable(const QVariantList &cookies);
    void cookiesDeleted(int count);
};

class FakePdfClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool offline MEMBER offline NOTIFY offlineChanged)
    Q_PROPERTY(QString error MEMBER error NOTIFY errorChanged)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(QObject *coverEditor READ coverEditor NOTIFY coverEditorChanged)
    Q_PROPERTY(bool coverShowsProjects MEMBER coverShowsProjects NOTIFY coverShowsProjectsChanged)
    Q_PROPERTY(bool archiveBusy MEMBER archiveBusy NOTIFY archiveBusyChanged)
    Q_PROPERTY(bool busy MEMBER busy)
    Q_PROPERTY(bool commentsAvailable MEMBER commentsAvailable)
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
    Q_PROPERTY(int completionHintCount MEMBER completionHintCount NOTIFY completionHintCountChanged)
public:
    bool offline = false;
    QString error;
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
    bool busy = false;
    bool commentsAvailable = true;
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
    int completionHintCount = 0;
    Q_INVOKABLE void recordCompletionHintShown() { ++completionHintCount; emit completionHintCountChanged(); }
    int compiles = 0;
    int sourceCloses = 0;
    int mainOpens = 0;
    int pdfCloses = 0;
    int syncLine = 0;
    int syncColumn = 0;
    QString syncFile;
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
    Q_INVOKABLE void openMainProjectSource(const QString &) { ++mainOpens; }
    Q_INVOKABLE void saveProjectSource(const QString &, const QString &, const QString &, bool, const QString &) { ++saves; }
    Q_INVOKABLE void compileProjectPdf(const QString &, const QString &path) { ++compiles; root = path; }
    Q_INVOKABLE void syncSourceToPdf(const QString &, const QString &path, int line, int column)
    { syncFile = path; syncLine = line; syncColumn = column; }
signals:
    void offlineChanged();
    void errorChanged();
    void reconnected();
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
    void completionHintCountChanged();
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

class AutoLoginTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void doubleTapRespectsClipping();
    void loadsPdfGeometry();
    void automaticLoginOnlyAfterSuccessfulProjectLoad();
    void interfaceComponentsLoad();
    void aiGenerationSupportsLandscapeAndPreview();
    void sciHubAddressAndSearch();
    void aiLoginLandscapeAndCallback();
    void aiModeControlsSettingsAndEditor();
    void pagesAndPickersAllowLandscape();
    void russianTranslations();
    void coverShowsProjectsAndRecentEdits();
    void coverDoesNotCaptureHiddenPdf();
    void opensProjectFilesByType();
    void folderBackNavigation();
    void protectsDraftsDuringFileChanges();
    void savesOpenFileBeforeFileOperations();
    void uploadsMultipleFiles();
    void updatesEditorAfterFileOperations();
    void editorPersistsDraft();
    void editorRedoShortcut();
    void editorAndPdfWheelAfterEditing();
    void completesLatexCommands();
    void completionTabHint();
    void findsAndReplacesText();
    void editorScrollsLongDocuments();
    void projectViewFiltersArchived();
    void offlineEditorDoesNotUseLegacyDraft();
    void createsProjectFromToolbar();
    void managesProjectTrash();
    void opensCollaborationPage();
    void pullRefreshPages();
    void historyNavigatesToCurrentFile();
    void historyListUpdatesVisibleEntries();
    void pdfPaneLoadsDocument();
    void pdfPaneReplacesRenderedPage();
    void sourcePreservesLocalEdits();
    void latexHighlightingPreservesText();
    void highlightsLatexContexts();
    void highlightsBibliography();
    void compileWaitsForSave();
    void saveShortcutOnlyOnActiveEditor();
    void reopensLastFile();
    void opensMainForNewProject();
    void attachedEditorDefersInitialization();
    void restoresServerAtStartup();
    void opensCachedAccountAtStartup();
    void reconnectsInBackground();
    void removesServerCookies();
    void showsServerSelectionWhenRestoreUnavailable();
    void editorAppBarActions();
    void editorAppBarMouseClicks();
    void editorSettingsMouseClick();
    void portraitAppBarNavigation();
    void pdfAppBarMouseClick();
    void mouseButtonsFollowNavigationRules();
    void editorSettingsAndZoom();
    void portraitPdfNavigation();
    void settingsPersistAcrossClients();
    void changesServerFromProjects();
    void restoresLastProjectNavigation();
    void projectForwardNavigationRequiresAttachedEditor();
    void attachedEditorSurvivesNavigation();
};

void AutoLoginTests::initTestCase()
{
    qmlRegisterType<LatexHighlighter>("Abedul", 1, 0, "LatexHighlighter");
    qmlRegisterType<PdfGeometry>("Abedul", 1, 0, "PdfGeometry");
    qmlRegisterType<DoubleTapMonitor>("Abedul", 1, 0, "DoubleTapMonitor");
    qmlRegisterType<CompletionKeyFilter>("Abedul", 1, 0, "CompletionKeyFilter");
    qmlRegisterType<OpenRouterClient>("Abedul", 1, 0, "OpenRouterClient");
    qmlRegisterType<WolframAlphaClient>("Abedul", 1, 0, "WolframAlphaClient");
    qmlRegisterType<ReversoClient>("Abedul", 1, 0, "ReversoClient");
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
    QVERIFY(client.rememberedServer().isEmpty());
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
                               QStringLiteral("ProjectFilesPage.qml"), QStringLiteral("ProjectCollaborationPage.qml"),
                               QStringLiteral("CompileRootPage.qml"), QStringLiteral("AiAssistantPopup.qml"),
                               QStringLiteral("OpenRouterLoginPage.qml")};
    for (const QString &page : pages) {
        QQmlComponent component(&engine, autoLoginUrl.resolved(QUrl(page)));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    }
    QQmlComponent window(&engine, autoLoginUrl.resolved(QUrl(QStringLiteral("../OverleafClient.qml"))));
    QVERIFY2(window.isReady(), qPrintable(window.errorString()));
}

void AutoLoginTests::aiGenerationSupportsLandscapeAndPreview()
{
    FakeAiProvider provider;
    FakeAiEditor editor;
    FakePageStack stack;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("fakeAi"), &provider);
    engine.rootContext()->setContextProperty(QStringLiteral("fakeEditor"), &editor);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import "." as Pages
        Item {
            width: 1024; height: 600
            Pages.AiAssistantPopup { client: fakeAi; editorPage: fakeEditor }
        }
    )", base.resolved(QUrl(QStringLiteral("AiPageTest.qml"))));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QQuickItem *host = qobject_cast<QQuickItem *>(object.data());
    QQuickItem *page = object->findChild<QQuickItem *>(QStringLiteral("aiGeneratorPopup"));
    QVERIFY(host && page);
    QVERIFY(QMetaObject::invokeMethod(page, "open"));
    auto *prompt = page->findChild<QQuickItem *>(QStringLiteral("aiPrompt"));
    auto *generate = page->findChild<QQuickItem *>(QStringLiteral("aiGenerate"));
    auto *preview = page->findChild<QQuickItem *>(QStringLiteral("aiGeneratedText"));
    auto *original = page->findChild<QQuickItem *>(QStringLiteral("aiOriginalText"));
    auto *insert = page->findChild<QQuickItem *>(QStringLiteral("aiInsert"));
    auto *scroll = page->findChild<QQuickItem *>(QStringLiteral("aiGeneratorFlickable"));
    QVERIFY(prompt && generate && original && preview && insert && scroll);
    for (const QSizeF size : {QSizeF(1024, 600), QSizeF(600, 1024)}) {
        host->setSize(size);
        QCoreApplication::processEvents();
        QVERIFY(page->width() <= size.width() && page->height() <= size.height());
        QVERIFY(scroll->width() <= page->width());
        QTRY_COMPARE(prompt->width(), scroll->width());
        QVERIFY(generate->width() <= scroll->width());
    }
    prompt->setProperty("text", QStringLiteral("Make a table"));
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(generate, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(provider.prompt, QStringLiteral("Make a table"));
    QCOMPARE(provider.context, editor.text);
    emit provider.resultReady(QStringLiteral(
        "{\"action\":\"replace\",\"old_text\":\"selection\",\"new_text\":\"\\\\begin{tabular}{c}\"}"));
    QTRY_COMPARE(original->property("text").toString(), QStringLiteral("selection"));
    QTRY_COMPARE(preview->property("text").toString(), QStringLiteral("\\begin{tabular}{c}"));
    for (const QSizeF size : {QSizeF(1024, 600), QSizeF(600, 1024)}) {
        host->setSize(size);
        QCoreApplication::processEvents();
        QVERIFY(page->width() <= size.width() && page->height() <= size.height());
        QVERIFY(original->x() >= 0 && original->x() + original->width() <= scroll->width());
        QVERIFY(preview->x() >= 0 && preview->x() + preview->width() <= scroll->width());
        QVERIFY(insert->width() <= scroll->width());
    }
    QVERIFY(editor.inserted.isEmpty());
    editor.text.append(QStringLiteral(" changed"));
    QVERIFY(QMetaObject::invokeMethod(insert, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY(editor.inserted.isEmpty());
    QCOMPARE(stack.pops, 0);
    QVERIFY(!page->property("insertError").toString().isEmpty());
    editor.text = QStringLiteral("Before selection after");
    QVERIFY(QMetaObject::invokeMethod(insert, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(editor.inserted, QStringLiteral("\\begin{tabular}{c}"));
    QCOMPARE(editor.start, 7);
    QCOMPARE(editor.end, 16);
    QCOMPARE(stack.pops, 0);
    QVERIFY(!page->isVisible());

    editor.text = QStringLiteral("same text and same text");
    QVERIFY(QMetaObject::invokeMethod(page, "open"));
    prompt->setProperty("text", QStringLiteral("Make a table"));
    QVERIFY(QMetaObject::invokeMethod(generate, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    emit provider.resultReady(QStringLiteral("{\"action\":\"replace\",\"old_text\":\"same\",\"new_text\":\"other\"}"));
    QVERIFY(!page->property("proposalReady").toBool());
    QVERIFY(!page->property("insertError").toString().isEmpty());
    emit provider.resultReady(QStringLiteral("{\"action\":\"replace\",\"old_text\":\"missing\",\"new_text\":\"other\"}"));
    QVERIFY(!page->property("proposalReady").toBool());
    emit provider.resultReady(QStringLiteral("not JSON"));
    QVERIFY(!page->property("proposalReady").toBool());

    editor.text = QStringLiteral("Before selection after");
    QVERIFY(QMetaObject::invokeMethod(generate, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    emit provider.resultReady(QStringLiteral("```json\n{\"action\":\"replace\",\"old_text\":\"selection\",\"new_text\":\"\"}\n```"));
    QVERIFY(page->property("proposalReady").toBool());
    QVERIFY(QMetaObject::invokeMethod(insert, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(editor.applications, 2);
    QCOMPARE(editor.start, 7);
    QCOMPARE(editor.end, 16);
    QVERIFY(editor.inserted.isEmpty());

    QVERIFY(QMetaObject::invokeMethod(page, "open"));
    prompt->setProperty("text", QStringLiteral("Make a table"));
    QVERIFY(QMetaObject::invokeMethod(generate, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    emit provider.resultReady(QStringLiteral("{\"action\":\"insert\",\"new_text\":\"new code\"}"));
    QVERIFY(page->property("proposalReady").toBool());
    QVERIFY(QMetaObject::invokeMethod(insert, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(editor.start, 2);
    QCOMPARE(editor.end, 2);
    QCOMPARE(editor.inserted, QStringLiteral("new code"));
    QVERIFY(QMetaObject::invokeMethod(page, "open"));
    prompt->setProperty("text", QStringLiteral("Another request"));
    provider.busy = true;
    emit provider.changed();
    QVERIFY(QMetaObject::invokeMethod(page, "close"));
    QCOMPARE(provider.cancellations, 1);
    emit provider.resultReady(QStringLiteral("{\"action\":\"insert\",\"new_text\":\"late\"}"));
    QVERIFY(QMetaObject::invokeMethod(page, "open"));
    QVERIFY(prompt->property("text").toString().isEmpty());
    QVERIFY(!page->property("proposalReady").toBool());
}

void AutoLoginTests::sciHubAddressAndSearch()
{
    IsolatedAiSettings settings;
    OverleafClient client;
    QCOMPARE(client.sciHubAddress(), QStringLiteral("https://sci-hub.ru"));
    QVERIFY(client.setSciHubAddress(QStringLiteral("https://example.test/mirror/")));
    QCOMPARE(client.sciHubAddress(), QStringLiteral("https://example.test/mirror"));
    QCOMPARE(client.sciHubSearchUrl(QStringLiteral("10.1234/abc?x=1")).toEncoded(),
             QByteArray("https://example.test/mirror/10.1234%2Fabc%3Fx%3D1"));
    QVERIFY(!client.setSciHubAddress(QStringLiteral("http://example.test")));
    QVERIFY(!client.setSciHubAddress(QStringLiteral("https://user@example.test")));
    QVERIFY(!client.setSciHubAddress(QStringLiteral("https://example.test/?x=1")));
    QCOMPARE(client.sciHubAddress(), QStringLiteral("https://example.test/mirror"));
    QVERIFY(client.sciHubSearchUrl(QStringLiteral("  ")).isEmpty());
    OverleafClient restored;
    QCOMPARE(restored.sciHubAddress(), client.sciHubAddress());
}

void AutoLoginTests::aiModeControlsSettingsAndEditor()
{
    IsolatedAiSettings settings;
    IsolatedAiData isolatedData;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(ABEDUL_VERSION));
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent containerComponent(&engine);
    containerComponent.setData(
        "import QtQuick 2.5\nimport Sailfish.Silica 1.0\n"
        "Item { property int status: PageStatus.Active; property int activeStatus: PageStatus.Active; "
        "property int inactiveStatus: PageStatus.Inactive; property int navigation: 0; "
        "property int navigationPending: 0; property int direction: 0 }", QUrl());
    QScopedPointer<QObject> container(containerComponent.create());
    QVERIFY2(container, qPrintable(containerComponent.errorString()));
    QQmlComponent editorComponent(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QScopedPointer<QObject> editorPage(editorComponent.beginCreate(engine.rootContext()));
    QVERIFY2(editorPage, qPrintable(editorComponent.errorString()));
    editorPage->setProperty("__stack_container", QVariant::fromValue(qobject_cast<QQuickItem *>(container.data())));
    editorComponent.completeCreate();
    editorPage->setProperty("visible", true);
    QObject *editor = editorPage->findChild<QObject *>(QStringLiteral("draftEditor"));
    QObject *shortcut = editorPage->findChild<QObject *>(QStringLiteral("aiGeneratorShortcut"));
    QObject *aiButton = editorPage->findChild<QObject *>(QStringLiteral("editorAiAssistant"));
    QObject *appBar = editorPage->findChild<QObject *>(QStringLiteral("editorAppBar"));
    QVERIFY(editor && shortcut && aiButton && appBar);
    QObject *aiIcon = aiButton->property("icon").value<QObject *>();
    QVERIFY(aiIcon);
    QTRY_COMPARE(aiIcon->property("status").toInt(), 1);
    editorPage->setProperty("currentFile", QStringLiteral("/main.tex"));
    editorPage->setProperty("initialized", true);
    editor->setProperty("text", QStringLiteral("\\beg"));
    QObject *input = editor->property("_editor").value<QObject *>();
    QVERIFY(input);
    input->setProperty("cursorPosition", 4);
    QCOMPARE(editorPage->property("latexCompletion").toString(), QStringLiteral("in{}"));
    QVERIFY(!editorPage->property("aiEnabled").toBool());
    QVERIFY(!shortcut->property("enabled").toBool());
    QVERIFY(!aiButton->property("visible").toBool());

    QQmlComponent settingsComponent(&engine, base.resolved(QUrl(QStringLiteral("SettingsPage.qml"))));
    QScopedPointer<QObject> settingsPage(settingsComponent.create());
    QVERIFY2(settingsPage, qPrintable(settingsComponent.errorString()));
    settingsPage->setProperty("visible", true);
    QObject *appIdField = settingsPage->findChild<QObject *>(QStringLiteral("wolframAlphaAppIdSetting"));
    WolframAlphaClient *wolfram = editorPage->findChild<WolframAlphaClient *>(QStringLiteral("editorWolframAlphaClient"));
    QVERIFY(appIdField && wolfram);
    QObject *appIdInput = appIdField->property("_editor").value<QObject *>();
    QVERIFY(appIdInput);
    appIdField->setProperty("text", QStringLiteral("TEST-APP-ID"));
    QVERIFY(QMetaObject::invokeMethod(appIdInput, "editingFinished"));
    wolfram->reload();
    QCOMPARE(wolfram->appId(), QStringLiteral("TEST-APP-ID"));
    QObject *toggle = settingsPage->findChild<QObject *>(QStringLiteral("aiEnabledSetting"));
    QObject *signIn = settingsPage->findChild<QObject *>(QStringLiteral("aiSignIn"));
    OpenRouterClient *settingsClient = settingsPage->findChild<OpenRouterClient *>(QStringLiteral("settingsAiClient"));
    QVERIFY(toggle && signIn && settingsClient);
    QQuickItem *settingsItem = qobject_cast<QQuickItem *>(settingsPage.data());
    QQuickItem *toggleItem = qobject_cast<QQuickItem *>(toggle);
    QQuickItem *signInItem = qobject_cast<QQuickItem *>(signIn);
    QVERIFY(settingsItem && toggleItem && signInItem);
    settingsItem->setSize(QSizeF(1024, 600));
    QTRY_COMPARE(toggleItem->width(), settingsItem->width());
    QVERIFY(signInItem->width() <= settingsItem->width());
    QVERIFY(!toggle->property("checked").toBool());
    QVERIFY(!signIn->property("visible").toBool());

    container->setProperty("status", container->property("inactiveStatus"));
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY2(settingsClient->aiEnabled(), qPrintable(settingsClient->error()));
    QVERIFY(!settingsClient->authenticated());
    QVERIFY(signIn->property("visible").toBool());
    OpenRouterClient restored;
    QVERIFY(restored.aiEnabled());
    container->setProperty("status", container->property("activeStatus"));
    QVERIFY(editorPage->property("aiEnabled").toBool());
    QVERIFY(shortcut->property("enabled").toBool());
    QVERIFY(editorPage->property("aiEnabled").toBool());
    QCOMPARE(editorPage->property("latexCompletion").toString(), QStringLiteral("in{}"));

    container->setProperty("status", container->property("inactiveStatus"));
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY(!signIn->property("visible").toBool());
    restored.reload();
    QVERIFY(!restored.aiEnabled());
    container->setProperty("status", container->property("activeStatus"));
    QVERIFY(!editorPage->property("aiEnabled").toBool());
    QVERIFY(!shortcut->property("enabled").toBool());
    QVERIFY(!editorPage->property("aiEnabled").toBool());
    QCOMPARE(editorPage->property("latexCompletion").toString(), QStringLiteral("in{}"));
}

void AutoLoginTests::aiLoginLandscapeAndCallback()
{
    QTemporaryDir modules;
    QVERIFY(modules.isValid());
    const QString path = modules.path() + QStringLiteral("/ru/auroraos/WebView");
    QVERIFY(QDir().mkpath(path));
    const QList<QPair<QString, QByteArray>> files = {
        {QStringLiteral("qmldir"), QByteArray("module ru.auroraos.WebView\nWebView 1.0 WebView.qml\n"
                                             "TouchInput 1.0 TouchInput.qml\nKeyboardInput 1.0 KeyboardInput.qml\n"
                                             "LoadRequestExtension 1.0 LoadRequestExtension.qml\n")},
        {QStringLiteral("WebView.qml"), QByteArray("import QtQuick 2.0\nItem { property url url }\n")},
        {QStringLiteral("TouchInput.qml"), QByteArray("import QtQuick 2.0\nItem {}\n")},
        {QStringLiteral("KeyboardInput.qml"), QByteArray("import QtQuick 2.0\nItem {}\n")},
        {QStringLiteral("LoadRequestExtension.qml"), QByteArray("import QtQuick 2.0\nItem {}\n")}
    };
    for (const auto &entry : files) {
        QFile file(path + QLatin1Char('/') + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(entry.second), qint64(entry.second.size()));
    }
    FakeAiProvider provider;
    FakePageStack stack;
    QQmlEngine engine;
    engine.addImportPath(modules.path());
    engine.rootContext()->setContextProperty(QStringLiteral("fakeAi"), &provider);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import "." as Pages
        Pages.OpenRouterLoginPage { client: fakeAi }
    )", base.resolved(QUrl(QStringLiteral("AiLoginTest.qml"))));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QQuickItem *page = qobject_cast<QQuickItem *>(object.data());
    QQuickItem *browser = page ? page->findChild<QQuickItem *>(QStringLiteral("openRouterBrowser")) : nullptr;
    QObject *navigation = page ? page->findChild<QObject *>(QStringLiteral("openRouterNavigation")) : nullptr;
    QVERIFY(page && browser && navigation);
    for (const QSizeF size : {QSizeF(1024, 600), QSizeF(600, 1024)}) {
        page->setSize(size);
        QTRY_COMPARE(browser->width(), size.width());
        QVERIFY(browser->height() > size.height() / 2);
        QVERIFY(browser->y() >= 0);
        QVERIFY(browser->y() + browser->height() <= size.height());
    }
    QVariant allowed;
    QVariantMap request;
    request.insert(QStringLiteral("url"), QStringLiteral("http://localhost:49152/abedul-openrouter?code=test"));
    QVERIFY(QMetaObject::invokeMethod(navigation, "beforeUrlLoad", Q_RETURN_ARG(QVariant, allowed),
                                     Q_ARG(QVariant, QVariant(request)), Q_ARG(QVariant, QVariant(false)),
                                     Q_ARG(QVariant, QVariant(true))));
    QVERIFY(!allowed.toBool());
    QCOMPARE(provider.callback.path(), QStringLiteral("/abedul-openrouter"));
    request.insert(QStringLiteral("url"), QStringLiteral("https://openrouter.ai/auth"));
    QVERIFY(QMetaObject::invokeMethod(navigation, "beforeUrlLoad", Q_RETURN_ARG(QVariant, allowed),
                                     Q_ARG(QVariant, QVariant(request)), Q_ARG(QVariant, QVariant(false)),
                                     Q_ARG(QVariant, QVariant(false))));
    QVERIFY(allowed.toBool());
}

void AutoLoginTests::pagesAndPickersAllowLandscape()
{
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick 2.0\n"
        "import Sailfish.Silica 1.0\n"
        "import Sailfish.Pickers 1.0\n"
        "import \"..\" as App\n"
        "App.OverleafClient {\n"
        "  initialPage: Page {}\n"
        "  property var pageProbe: Page {}\n"
        "  property var dialogProbe: Dialog {}\n"
        "  property var pickerProbe: ContentPickerPage {}\n"
        "  property var pickerChildProbe: FilePickerPage {}\n"
        "}\n", base.resolved(QUrl(QStringLiteral("OrientationTest.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    const int orientations = window->property("allowedOrientations").toInt();
    QCOMPARE(window->property("_defaultPageOrientations").toInt(), orientations);
    for (const char *name : {"pageProbe", "dialogProbe", "pickerProbe", "pickerChildProbe"}) {
        QObject *probe = window->property(name).value<QObject *>();
        QVERIFY(probe);
        QCOMPARE(probe->property("allowedOrientations").toInt(), orientations);
    }
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
        QObject *compile = page->findChild<QObject *>(QStringLiteral("saveAndCompile"));
        QVERIFY(compile);
        QCOMPARE(compile->property("text").toString(), QString::fromUtf8("Компилировать"));
        QObject *files = page->findChild<QObject *>(QStringLiteral("editorProjectFiles"));
        QObject *access = page->findChild<QObject *>(QStringLiteral("projectSharing"));
        QObject *ai = page->findChild<QObject *>(QStringLiteral("editorAiAssistant"));
        QVERIFY(files && access && ai);
        QCOMPARE(files->property("text").toString(), QString::fromUtf8("Файлы"));
        QCOMPARE(access->property("text").toString(), QString::fromUtf8("Доступ"));
        QCOMPARE(ai->property("text").toString(), QString());
        QByteArray payload;
        QString error;
        QVERIFY(!compileRequestBody(QString(), &payload, &error));
        QCOMPARE(error, QString::fromUtf8("Выберите корректный главный документ."));
        OverleafClient realClient;
        QVERIFY(!realClient.setServer(QStringLiteral("http://example.org")));
        QCOMPARE(realClient.error(), QString::fromUtf8("Введите HTTPS-адрес сервера без пути."));
        QCOMPARE(QCoreApplication::translate("OverleafClient", "Project ZIP downloaded"),
                 QString::fromUtf8("ZIP-архив проекта загружен"));
        QCOMPARE(QCoreApplication::translate("EditorPage", "Press Tab to accept"),
                 QString::fromUtf8("Нажмите Tab, чтобы принять подсказку"));
        QCOMPARE(QCoreApplication::translate("SettingsPage", "Press Ctrl+S to save and compile."),
                 QString::fromUtf8("Нажмите Ctrl+S, чтобы сохранить и скомпилировать."));
        QVERIFY(QCoreApplication::translate("SettingsPage",
                "To zoom the editor or PDF, point at the desired area and use Ctrl++ / Ctrl+- or Ctrl + mouse wheel.")
                .contains(QString::fromUtf8("колесо мыши")));
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
    SingleImageProvider *provider = new SingleImageProvider;
    engine.addImageProvider(QStringLiteral("cover-test"), provider);
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
    QVERIFY(cover->property("showProjectList").toBool());

    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("one"));
    page->setProperty("projectName", QStringLiteral("First"));
    component.completeCreate();
    client.setProperty("coverShowsProjects", false);
    client.setCoverEditor(page.data());
    QQuickItem *coverItem = qobject_cast<QQuickItem *>(cover.data());
    QQuickItem *content = cover->findChild<QQuickItem *>(QStringLiteral("coverContent"));
    QQuickItem *actionArea = cover->property("coverActionArea").value<QQuickItem *>();
    QVERIFY(coverItem && content && actionArea);
    const qreal bottomMargin = QQmlProperty(content, QStringLiteral("anchors.bottomMargin")).read().toReal();
    QTRY_VERIFY(qAbs(content->y() + content->height() + bottomMargin
                     - actionArea->mapToItem(coverItem, QPointF(0, 0)).y()) < 0.1);
    stack->setProperty("attachedPage", QVariant::fromValue(page.data()));
    QVERIFY(QMetaObject::invokeMethod(cover.data(), "openCoverPage", Q_ARG(QVariant, false)));
    QCOMPARE(stack->property("currentPage").value<QObject *>(), page.data());
    QVERIFY(!cover->property("showProjectList").toBool());
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
    QStringList longLines;
    for (int i = 0; i < 40; ++i)
        longLines.append(QStringLiteral("line %1").arg(i));
    const QString longText = longLines.join(QLatin1Char('\n'));
    editor->setProperty("text", longText);
    textEdit->setProperty("cursorPosition", longText.indexOf(QStringLiteral("line 10")));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "updateCoverSnippet"));
    QStringList coverLines = page->property("coverSnippet").toString().split(QLatin1Char('\n'));
    QCOMPARE(coverLines.first(), QStringLiteral("line 8"));
    QCOMPARE(coverLines.size(), 30);
    QVERIFY(coverLines.contains(QStringLiteral("line 10")));
    textEdit->setProperty("cursorPosition", longText.indexOf(QStringLiteral("line 39")));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "updateCoverSnippet"));
    coverLines = page->property("coverSnippet").toString().split(QLatin1Char('\n'));
    QCOMPARE(coverLines.first(), QStringLiteral("line 37"));
    QCOMPARE(coverLines.last(), QStringLiteral("line 39"));
    QQuickItem *snippetItem = qobject_cast<QQuickItem *>(snippet);
    QVERIFY(snippetItem);
    QTRY_VERIFY(qAbs(snippetItem->mapToItem(content, QPointF(0, 0)).y()) < 0.1);
    QQmlComponent cachedImageComponent(&engine);
    cachedImageComponent.setData("import QtQuick 2.0\nImage { source: \"image://cover-test/preview\" }", QUrl());
    QScopedPointer<QObject> cachedImage(cachedImageComponent.create());
    QVERIFY2(cachedImage, qPrintable(cachedImageComponent.errorString()));
    QTRY_COMPARE(cachedImage->property("status").toInt(), 1);
    QQmlComponent snapshotComponent(&engine);
    snapshotComponent.setData("import QtQuick 2.0\nQtObject { property url url: \"image://cover-test/preview\" }", QUrl());
    QScopedPointer<QObject> snapshot(snapshotComponent.create());
    QVERIFY(snapshot);
    QObject *panes = page->findChild<QObject *>(QStringLiteral("editorPanes"));
    QVERIFY(panes);
    panes->setProperty("sideBySide", true);
    page->setProperty("coverPdfSnapshot", QVariant::fromValue(snapshot.data()));
    QObject *pdfPeek = cover->findChild<QObject *>(QStringLiteral("coverPdfPeek"));
    QVERIFY(pdfPeek);
    QVERIFY(pdfPeek->property("visible").toBool());
    QTRY_COMPARE(pdfPeek->property("status").toInt(), 1);
    QCOMPARE(provider->requests, 1);
    QQuickItem *pdfItem = qobject_cast<QQuickItem *>(pdfPeek);
    QVERIFY(pdfItem);
    QTRY_VERIFY(qAbs(pdfItem->mapToItem(content, QPointF(0, pdfItem->height())).y()
                     - content->height()) < 0.1);
    QVERIFY(pdfItem->clip());
    QVERIFY(pdfPeek->property("paintedWidth").toReal() >= pdfItem->width());
    QVERIFY(pdfPeek->property("paintedHeight").toReal() >= pdfItem->height());
    QTest::qWait(100);
    QVERIFY2(snippet->property("x").toReal() < pdfPeek->property("x").toReal(),
             qPrintable(QStringLiteral("cover width=%1 text width=%2 x=%3 preview width=%4 x=%5")
                        .arg(cover->property("width").toReal()).arg(snippet->property("width").toReal())
                        .arg(snippet->property("x").toReal()).arg(pdfPeek->property("width").toReal())
                        .arg(pdfPeek->property("x").toReal())));
    client.setProperty("pdfOnLeft", true);
    QTRY_VERIFY(pdfPeek->property("x").toReal() < snippet->property("x").toReal());
    client.setProperty("pdfOnLeft", false);
    panes->setProperty("sideBySide", false);
    QTRY_VERIFY(snippet->property("visible").toBool());
    QTRY_VERIFY(!pdfPeek->property("visible").toBool());
    QCOMPARE(snippet->property("width").toReal(), content->width());
    QQmlComponent pdfPageComponent(&engine);
    pdfPageComponent.setData("import QtQuick 2.0\nItem { property Item contentHost: Item {} }", QUrl());
    QScopedPointer<QObject> pdfPage(pdfPageComponent.create());
    QVERIFY2(pdfPage, qPrintable(pdfPageComponent.errorString()));
    page->setProperty("attachedPdfPage", QVariant::fromValue(pdfPage.data()));
    stack->setProperty("currentPage", QVariant::fromValue(pdfPage.data()));
    QTRY_VERIFY(pdfPeek->property("visible").toBool());
    QVERIFY(!snippet->property("visible").toBool());
    QCOMPARE(pdfPeek->property("width").toReal(), content->width());
    QTRY_VERIFY(qAbs(pdfItem->mapToItem(content, QPointF(0, pdfItem->height())).y()
                     - content->height()) < 0.1);
    QVERIFY(pdfPeek->property("paintedWidth").toReal() >= pdfItem->width());
    QVERIFY(pdfPeek->property("paintedHeight").toReal() >= pdfItem->height());
    QVERIFY(QMetaObject::invokeMethod(cover.data(), "openCoverPage", Q_ARG(QVariant, false)));
    QCOMPARE(stack->property("currentPage").value<QObject *>(), pdfPage.data());
    stack->setProperty("currentPage", QVariant::fromValue(page.data()));
    page->setProperty("attachedPdfPage", QVariant::fromValue(static_cast<QObject *>(nullptr)));
    panes->setProperty("sideBySide", true);
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
    QVERIFY(!cover->property("showProjectList").toBool());
    QVERIFY(!cover->property("navigationError").toString().isEmpty());
    stack->setProperty("busy", false);
    QVERIFY(QMetaObject::invokeMethod(back, "triggered"));
    QVERIFY(cover->property("showProjectList").toBool());
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
    QVERIFY(!cover->property("showProjectList").toBool());
    QCOMPARE(stack->property("currentPage").value<QObject *>(), page.data());
    client.setCoverEditor(nullptr);
    QVERIFY(cover->property("showProjectList").toBool());
}

void AutoLoginTests::coverDoesNotCaptureHiddenPdf()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    const QString pdfPath = QDir(directory.path()).filePath(QStringLiteral("cover.pdf"));
    {
        QPdfWriter writer(pdfPath);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.fillRect(QRect(100, 100, 3000, 4000), Qt::black);
    }
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.resize(800, 600);
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    QVERIFY(pageItem);
    pageItem->setParentItem(window.contentItem());
    pageItem->setSize(QSizeF(800, 600));
    page->setProperty("largeScreen", true);
    page->setProperty("visible", true);
    client.setPdfPath(pdfPath);
    QObject *loader = page->findChild<QObject *>(QStringLiteral("pdfLoader"));
    QVERIFY(loader);
    QTRY_VERIFY(loader->property("item").value<QObject *>());
    QObject *preview = loader->property("item").value<QObject *>();
    QTRY_COMPARE(preview->property("status").toInt(), 2);
    pageItem->setVisible(false);
    QVERIFY(!preview->property("visible").toBool());
    QObject snapshot;
    page->setProperty("coverPdfSnapshot", QVariant::fromValue(&snapshot));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "captureCoverPdf"));
    QCOMPARE(page->property("coverPdfSnapshot").value<QObject *>(), &snapshot);
    QVERIFY(page->property("coverPdfError").toString().isEmpty());
    QWindow foreground;
    foreground.show();
    foreground.requestActivate();
    QTRY_COMPARE(QGuiApplication::applicationState(), Qt::ApplicationActive);
    QObject *timer = page->findChild<QObject *>(QStringLiteral("coverPdfTimer"));
    QVERIFY(timer);
    pageItem->setVisible(true);
    QTRY_VERIFY(preview->property("visible").toBool());
    QTRY_VERIFY(timer->property("running").toBool());
    pageItem->setVisible(false);
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
    QObject *content = page->findChild<QObject *>(QStringLiteral("editorContent"));
    QQuickItem *appBar = page->findChild<QQuickItem *>(QStringLiteral("editorAppBar"));
    QObject *codePane = page->findChild<QObject *>(QStringLiteral("editorCodePane"));
    QObject *pdfLoader = page->findChild<QObject *>(QStringLiteral("pdfLoader"));
    QQuickItem *settings = page->findChild<QQuickItem *>(QStringLiteral("editorSettings"));
    QQuickItem *openPdf = page->findChild<QQuickItem *>(QStringLiteral("openCachedPdf"));
    QQuickItem *downloadZip = page->findChild<QQuickItem *>(QStringLiteral("downloadProjectArchive"));
    QQuickItem *projectFiles = page->findChild<QQuickItem *>(QStringLiteral("editorProjectFiles"));
    QObject *saveMenuItem = page->findChild<QObject *>(QStringLiteral("saveAndCompile"));
    QVERIFY(content);
    QVERIFY(appBar);
    QVERIFY(codePane);
    QVERIFY(pdfLoader);
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("editorPulleyWheel")));
    QVERIFY(page->findChild<QObject *>(QStringLiteral("projectSharing")));
    QVERIFY(page->findChild<QObject *>(QStringLiteral("projectHistory")));
    QObject *comments = page->findChild<QObject *>(QStringLiteral("projectComments"));
    QVERIFY(comments);
    QObject *commentsIcon = comments->property("icon").value<QObject *>();
    QVERIFY(commentsIcon);
    QVERIFY(commentsIcon->property("source").toUrl().toString().endsWith(QStringLiteral("/icons/comments.png")));
    QTRY_COMPARE(commentsIcon->property("status").toInt(), 1);
    QVERIFY(page->findChild<QObject *>(QStringLiteral("projectChat")));
    QVERIFY(settings);
    QVERIFY(openPdf);
    QVERIFY(downloadZip);
    QVERIFY(projectFiles);
    QVERIFY(saveMenuItem);
    const QList<QQuickItem *> menuItems = settings->parentItem()->childItems();
    const QList<QQuickItem *> expectedOrder = {
        qobject_cast<QQuickItem *>(saveMenuItem), projectFiles, downloadZip, openPdf,
        page->findChild<QQuickItem *>(QStringLiteral("editorSearch")),
        page->findChild<QQuickItem *>(QStringLiteral("projectChat")),
        page->findChild<QQuickItem *>(QStringLiteral("projectHistory")),
        page->findChild<QQuickItem *>(QStringLiteral("projectSharing")),
        page->findChild<QQuickItem *>(QStringLiteral("projectComments")),
        page->findChild<QQuickItem *>(QStringLiteral("editorAiAssistant")), settings
    };
    for (int i = 0; i + 1 < expectedOrder.size(); ++i) {
        const int position = menuItems.indexOf(expectedOrder.at(i));
        QVERIFY(position >= 0);
        QVERIFY(position < menuItems.indexOf(expectedOrder.at(i + 1)));
    }
    QCOMPARE(appBar->property("headerText").toString(), QStringLiteral("Test"));
    page->setProperty("projectName", QStringLiteral("Renamed"));
    QCOMPARE(appBar->property("headerText").toString(), QStringLiteral("Renamed"));
    QVERIFY(codePane->property("clip").toBool());
    QVERIFY(pdfLoader->property("clip").toBool());
    QVERIFY(content->property("height").toReal() > 0);
    QVERIFY(appBar->height() > 0);
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
    QCOMPARE(panes->mapToItem(pageItem, QPointF()).y(),
             appBar->mapToItem(pageItem, QPointF(0, appBar->height())).y());
    QVERIFY(page->property("initialized").toBool());
    editor->setProperty("text", QStringLiteral("\\section{Test}"));
    QTRY_VERIFY(!page->property("dirty").toBool());
    QVERIFY(drafts.load(client.server(), QStringLiteral("test")));
    QCOMPARE(drafts.text(), QStringLiteral("\\section{Test}"));
}

void AutoLoginTests::editorRedoShortcut()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QQmlComponent containerComponent(&engine);
    containerComponent.setData(
        "import QtQuick 2.5\nimport Sailfish.Silica 1.0\n"
        "Item { property int status: PageStatus.Active; property int navigation: 0; "
        "property int navigationPending: 0; property int direction: 0 }", QUrl());
    QScopedPointer<QObject> container(containerComponent.create());
    QVERIFY(container);
    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.resize(600, 800);
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("__stack_container", QVariant::fromValue(qobject_cast<QQuickItem *>(container.data())));
    component.completeCreate();
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    QVERIFY(pageItem);
    pageItem->setParentItem(window.contentItem());
    pageItem->setSize(QSizeF(600, 800));
    pageItem->setVisible(true);
    page->setProperty("initialized", true);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QObject *redo = page->findChild<QObject *>(QStringLiteral("editorRedoShortcut"));
    QVERIFY(editor && redo);
    QQuickItem *input = qobject_cast<QQuickItem *>(editor->property("_editor").value<QObject *>());
    QVERIFY(input);
    window.show();
    window.requestActivate();
    input->forceActiveFocus();
    QTRY_VERIFY(input->hasActiveFocus());
    editor->setProperty("text", QStringLiteral("abc"));
    input->setProperty("cursorPosition", 3);
    QTest::keyClick(&window, Qt::Key_D);
    QCOMPARE(input->property("text").toString(), QStringLiteral("abcd"));
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(input->property("text").toString(), QStringLiteral("abc"));
    QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(input->property("text").toString(), QStringLiteral("abcd"));

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(input->property("text").toString(), QStringLiteral("abc"));
    page->setProperty("searchVisible", true);
    QObject *search = page->findChild<QObject *>(QStringLiteral("searchInput"));
    QVERIFY(search);
    QQuickItem *searchInput = qobject_cast<QQuickItem *>(search->property("_editor").value<QObject *>());
    QVERIFY(searchInput);
    searchInput->forceActiveFocus();
    QTRY_VERIFY(searchInput->hasActiveFocus());
    QVERIFY(!redo->property("enabled").toBool());
    QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(input->property("text").toString(), QStringLiteral("abc"));
    input->forceActiveFocus();
    QTRY_VERIFY(input->hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(input->property("text").toString(), QStringLiteral("abcd"));
}

void AutoLoginTests::editorAndPdfWheelAfterEditing()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    const QString pdfPath = QDir(directory.path()).filePath(QStringLiteral("wheel.pdf"));
    {
        QPdfWriter writer(pdfPath);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        for (int i = 0; i < 4; ++i) {
            painter.drawText(100, 100, QStringLiteral("PDF page"));
            if (i < 3)
                QVERIFY(writer.newPage());
        }
    }
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QQmlComponent containerComponent(&engine);
    containerComponent.setData(
        "import QtQuick 2.5\nimport Sailfish.Silica 1.0\n"
        "Item { property int status: PageStatus.Active; property int navigation: 0; "
        "property int navigationPending: 0; property int direction: 0 }", QUrl());
    QScopedPointer<QObject> container(containerComponent.create());
    QVERIFY(container);
    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.resize(1000, 600);
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("__stack_container", QVariant::fromValue(qobject_cast<QQuickItem *>(container.data())));
    component.completeCreate();
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    QVERIFY(pageItem);
    pageItem->setParentItem(window.contentItem());
    pageItem->setSize(QSizeF(1000, 600));
    pageItem->setVisible(true);
    page->setProperty("largeScreen", true);
    page->setProperty("initialized", true);
    QQuickItem *pane = page->findChild<QQuickItem *>(QStringLiteral("editorCodePane"));
    QQuickItem *pdfPane = page->findChild<QQuickItem *>(QStringLiteral("editorPdfPane"));
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QObject *loader = page->findChild<QObject *>(QStringLiteral("pdfLoader"));
    QVERIFY(pane && pdfPane && editor && loader);
    editor->setProperty("text", QStringLiteral("Editor line\n").repeated(100));
    client.setPdfPath(pdfPath);
    QObject *pdf = loader->property("item").value<QObject *>();
    QVERIFY(pdf);
    QTRY_COMPARE(pdf->property("status").toInt(), 2);
    QQuickItem *pdfFlick = pdf->findChild<QQuickItem *>(QStringLiteral("pdfContentFlickable"));
    QVERIFY(pdfFlick);
    QQuickItem *input = qobject_cast<QQuickItem *>(editor->property("_editor").value<QObject *>());
    QVERIFY(input);
    window.show();
    window.requestActivate();
    input->forceActiveFocus();
    QTRY_VERIFY(input->hasActiveFocus());
    input->setProperty("cursorPosition", 0);
    QTest::keyClick(&window, Qt::Key_A);
    QVERIFY(input->property("text").toString().startsWith(QLatin1Char('a')));
    renderControl.polishItems();
    QVERIFY(pane->property("contentHeight").toReal() > pane->height());
    QTRY_VERIFY(pdfFlick->property("contentHeight").toReal() > pdfFlick->height());
    for (bool pdfOnLeft : {false, true}) {
        client.pdfOnLeft = pdfOnLeft;
        emit client.pdfOnLeftChanged();
        renderControl.polishItems();
        pane->setProperty("contentY", 0);
        pdf->setProperty("contentY", 0);
        const QPointF editorPoint = pane->mapToScene(QPointF(pane->width() / 2, pane->height() / 2));
        const QPointF pdfPoint = pdfPane->mapToScene(QPointF(pdfPane->width() / 2, pdfPane->height() / 2));
        QTest::mouseMove(&window, editorPoint.toPoint());
        QTRY_COMPARE(page->property("wheelTarget").toString(), QStringLiteral("editor"));
        QWheelEvent editorWheel(editorPoint, -120, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &editorWheel);
        QVERIFY(pane->property("contentY").toReal() > 0);
        const qreal editorY = pane->property("contentY").toReal();
        QTest::mouseMove(&window, pdfPoint.toPoint());
        QTRY_COMPARE(page->property("wheelTarget").toString(), QStringLiteral("pdf"));
        QWheelEvent pdfWheel(editorPoint, -120, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &pdfWheel);
        QTRY_VERIFY(pdf->property("contentY").toReal() > 0);
        QCOMPARE(pane->property("contentY").toReal(), editorY);
        QVERIFY(input->hasActiveFocus());
        const qreal pdfZoom = pdf->property("zoom").toReal();
        QWheelEvent pdfZoomWheel(pdfPoint, 120, Qt::NoButton, Qt::ControlModifier);
        QCoreApplication::sendEvent(&window, &pdfZoomWheel);
        QVERIFY(pdf->property("zoom").toReal() > pdfZoom);
        const qreal pdfY = pdf->property("contentY").toReal();
        QTest::mouseMove(&window, editorPoint.toPoint());
        QTRY_COMPARE(page->property("wheelTarget").toString(), QStringLiteral("editor"));
        QWheelEvent editorWheelAgain(pdfPoint, -120, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &editorWheelAgain);
        QVERIFY(pane->property("contentY").toReal() > editorY);
        QCOMPARE(pdf->property("contentY").toReal(), pdfY);
    }
}

void AutoLoginTests::completesLatexCommands()
{
    IsolatedAiData isolatedData;
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
    QVERIFY(hint->property("text").toString().startsWith(QStringLiteral(" -> \\begin{eqnarray}...")));
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

    QQmlComponent containerComponent(view.engine());
    containerComponent.setData(
        "import QtQuick 2.5\n"
        "import Sailfish.Silica 1.0\n"
        "Item { property int status: PageStatus.Active; property int activeStatus: PageStatus.Active; "
        "property int inactiveStatus: PageStatus.Inactive; property int navigation: 0; "
        "property int navigationPending: 0; property int direction: 0 }", QUrl());
    QScopedPointer<QObject> container(containerComponent.create());
    QVERIFY2(container, qPrintable(containerComponent.errorString()));
    page->setProperty("__stack_container", QVariant::fromValue(qobject_cast<QQuickItem *>(container.data())));
    QObject *wolframShortcut = page->findChild<QObject *>(QStringLiteral("wolframAlphaShortcut"));
    QQuickItem *wolframPopup = page->findChild<QQuickItem *>(QStringLiteral("wolframAlphaPopup"));
    QObject *wolframText = page->findChild<QObject *>(QStringLiteral("wolframAlphaPopupText"));
    QQuickItem *codePane = page->findChild<QQuickItem *>(QStringLiteral("editorCodePane"));
    QVERIFY(wolframShortcut && wolframPopup && wolframText && codePane);
    QCOMPARE(wolframShortcut->property("portableText").toString(), QStringLiteral("Ctrl+W"));
    QVERIFY(page->property("keyboardShortcutHintText").toString().contains(QStringLiteral("Ctrl+W")));
    input->forceActiveFocus();
    QVERIFY(wolframShortcut->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(wolframShortcut, "activated"));
    QVERIFY(!wolframPopup->isVisible());
    editor->setProperty("text", QStringLiteral("$x=1$"));
    input->setProperty("cursorPosition", 2);
    QVERIFY(!wolframPopup->isVisible());
    QVERIFY(QMetaObject::invokeMethod(wolframShortcut, "activated"));
    QCOMPARE(wolframText->property("text").toString(), QStringLiteral("Add your WolframAlpha AppID in Settings first."));
    for (const QSize &size : {QSize(600, 800), QSize(1024, 600)}) {
        view.resize(size);
        QCoreApplication::processEvents();
        QVERIFY(wolframPopup->width() <= codePane->width());
        QVERIFY(wolframPopup->height() <= codePane->height());
        QVERIFY(wolframPopup->x() >= codePane->x());
        QVERIFY(wolframPopup->x() + wolframPopup->width() <= codePane->x() + codePane->width());
        QVERIFY(wolframPopup->y() >= codePane->y());
        QVERIFY(wolframPopup->y() + wolframPopup->height() <= codePane->y() + codePane->height());
    }
    QObject *wolframButton = page->findChild<QObject *>(QStringLiteral("editorWolframAlpha"));
    QObject *wolframIcon = page->findChild<QObject *>(QStringLiteral("editorWolframAlphaIcon"));
    QQuickItem *inputPopup = page->findChild<QQuickItem *>(QStringLiteral("wolframAlphaInputPopup"));
    QObject *formula = page->findChild<QObject *>(QStringLiteral("wolframAlphaInput"));
    QObject *formulaResult = page->findChild<QObject *>(QStringLiteral("wolframAlphaInputResult"));
    QObject *submit = page->findChild<QObject *>(QStringLiteral("wolframAlphaSubmit"));
    QVERIFY(wolframButton && wolframIcon && inputPopup && formula && formulaResult && submit);
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("wolframAlphaInputClose")));
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("wolframAlphaClose")));
    QTRY_COMPARE(wolframIcon->property("status").toInt(), 1);
    const QString iconSource = wolframIcon->property("source").toUrl().toString();
    QVERIFY(iconSource.endsWith(QStringLiteral("rheniumbeta-dark.png"))
            || iconSource.endsWith(QStringLiteral("rheniumbeta-light.png")));
    for (const QString &name : {QStringLiteral("rheniumbeta-dark.png"),
                                QStringLiteral("rheniumbeta-light.png")}) {
        const QImage icon(base.resolved(QUrl(QStringLiteral("../icons/") + name)).toLocalFile());
        QVERIFY(!icon.isNull());
        QCOMPARE(qAlpha(icon.pixel(0, 0)), 0);
        QVERIFY(qAlpha(icon.pixel(icon.width() / 2, icon.height() / 2)) > 0);
    }
    QVERIFY(wolframButton->property("text").toString().isEmpty());
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(wolframButton, "clicked",
                                      QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY(inputPopup->isVisible());
    QVERIFY(!wolframPopup->isVisible());
    QVERIFY(!page->property("wolframInputSubmitted").toBool());
    formula->setProperty("text", QStringLiteral("x^2=1"));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(formula, &enter);
    QVERIFY(page->property("wolframInputSubmitted").toBool());
    QCOMPARE(formulaResult->property("text").toString(),
             QStringLiteral("Add your WolframAlpha AppID in Settings first."));
    for (const QSize &size : {QSize(600, 800), QSize(1024, 600)}) {
        view.resize(size);
        QCoreApplication::processEvents();
        QQuickItem *parent = inputPopup->parentItem();
        QVERIFY(inputPopup->width() <= parent->width());
        QVERIFY(inputPopup->height() <= parent->height());
        QVERIFY(inputPopup->x() >= 0);
        QVERIFY(inputPopup->x() + inputPopup->width() <= parent->width());
        QVERIFY(inputPopup->y() >= 0);
        QVERIFY(inputPopup->y() + inputPopup->height() <= parent->height());
    }
    QVERIFY(QMetaObject::invokeMethod(page, "dismissUtilityPopups"));
    QVERIFY(!inputPopup->isVisible());

    QObject *reversoButton = page->findChild<QObject *>(QStringLiteral("editorReverso"));
    QObject *reversoShortcut = page->findChild<QObject *>(QStringLiteral("reversoShortcut"));
    QQuickItem *reversoPopup = page->findChild<QQuickItem *>(QStringLiteral("reversoPopup"));
    QObject *reversoInput = page->findChild<QObject *>(QStringLiteral("reversoInput"));
    QObject *reversoResult = page->findChild<QObject *>(QStringLiteral("reversoResult"));
    QQuickItem *reversoInputContainer = page->findChild<QQuickItem *>(QStringLiteral("reversoInputContainer"));
    QQuickItem *reversoSubmit = page->findChild<QQuickItem *>(QStringLiteral("reversoSubmit"));
    QVERIFY(reversoButton && reversoShortcut && reversoPopup && reversoInput && reversoResult
            && reversoInputContainer && reversoSubmit);
    QObject *reversoIcon = page->findChild<QObject *>(QStringLiteral("editorReversoIcon"));
    QVERIFY(reversoIcon);
    QTRY_COMPARE(reversoIcon->property("status").toInt(), 1);
    QVERIFY(page->property("keyboardShortcutHintText").toString().contains(QStringLiteral("Ctrl+R")));
    QCOMPARE(reversoShortcut->property("portableText").toString(), QStringLiteral("Ctrl+R"));
    QVERIFY(QMetaObject::invokeMethod(reversoButton, "clicked",
                                      QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY(reversoPopup->isVisible());
    QVERIFY(reversoInputContainer->isVisible());
    QVERIFY(reversoSubmit->isVisible());
    QVERIFY(!inputPopup->isVisible());
    QVERIFY(!reversoShortcut->property("enabled").toBool());
    for (const QSize &size : {QSize(600, 800), QSize(1024, 600)}) {
        view.resize(size);
        QCoreApplication::processEvents();
        QQuickItem *parent = reversoPopup->parentItem();
        QVERIFY(reversoPopup->width() <= parent->width());
        QVERIFY(reversoPopup->height() <= parent->height());
        QVERIFY(reversoPopup->x() >= 0);
        QVERIFY(reversoPopup->x() + reversoPopup->width() <= parent->width());
        QVERIFY(reversoPopup->y() >= 0);
        QVERIFY(reversoPopup->y() + reversoPopup->height() <= parent->height());
    }
    QVERIFY(QMetaObject::invokeMethod(reversoPopup, "close"));
    const QString selection = QStringLiteral("first line\n") + QString(1001, QLatin1Char('a'));
    editor->setProperty("text", selection + QStringLiteral(" trailing"));
    QVERIFY(QMetaObject::invokeMethod(input, "select", Q_ARG(int, 0), Q_ARG(int, selection.size())));
    input->forceActiveFocus();
    QVERIFY(reversoShortcut->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(reversoShortcut, "activated"));
    QVERIFY(reversoPopup->isVisible());
    QVERIFY(reversoPopup->property("reverseDirection").toBool());
    QVERIFY(!reversoInputContainer->isVisible());
    QVERIFY(!reversoSubmit->isVisible());
    QCOMPARE(reversoPopup->property("translatedWord").toString(), selection);
    QVERIFY(reversoResult->property("text").toString().contains(QStringLiteral("too long")));
    for (const QSize &size : {QSize(600, 800), QSize(1024, 600)}) {
        view.resize(size);
        QCoreApplication::processEvents();
        const QPointF origin = codePane->mapToItem(reversoPopup->parentItem(), QPointF());
        const QRectF cursor = input->property("cursorRectangle").toRectF();
        const QPointF cursorPoint = input->mapToItem(codePane, cursor.topLeft());
        const qreal cursorTop = qBound(qreal(0), cursorPoint.y(), codePane->height());
        const qreal cursorBottom = qBound(qreal(0), cursorPoint.y() + cursor.height(), codePane->height());
        QVERIFY(reversoPopup->x() >= origin.x() - 0.1);
        QVERIFY(reversoPopup->x() + reversoPopup->width() <= origin.x() + codePane->width() + 0.1);
        QVERIFY(reversoPopup->y() >= origin.y() - 0.1);
        QVERIFY(reversoPopup->y() + reversoPopup->height() <= origin.y() + codePane->height() + 0.1);
        QVERIFY(qAbs(reversoPopup->y() - (origin.y() + cursorBottom)) < 1
                || qAbs(reversoPopup->y() + reversoPopup->height() - (origin.y() + cursorTop)) < 1);
    }
    editor->setProperty("text", QStringLiteral("changed"));
    QVERIFY(!reversoPopup->isVisible());
    editor->setProperty("text", QStringLiteral("ordinary $x^2$ tail"));
    input->setProperty("cursorPosition", 11);
    QVERIFY(QMetaObject::invokeMethod(reversoShortcut, "activated"));
    QVERIFY(!reversoPopup->isVisible());
    QVERIFY(QMetaObject::invokeMethod(input, "select", Q_ARG(int, 0), Q_ARG(int, 14)));
    QVERIFY(QMetaObject::invokeMethod(reversoShortcut, "activated"));
    QVERIFY(!reversoPopup->isVisible());
    editor->setProperty("text", QStringLiteral("ordinary words"));
    input->setProperty("cursorPosition", 3);
    input->forceActiveFocus();
    QObject *sourceLanguage = page->findChild<QObject *>(QStringLiteral("reversoSourceLanguage"));
    QObject *targetLanguage = page->findChild<QObject *>(QStringLiteral("reversoTargetLanguage"));
    QVERIFY(sourceLanguage && targetLanguage);
    targetLanguage->setProperty("currentIndex", sourceLanguage->property("currentIndex"));
    QVERIFY(QMetaObject::invokeMethod(reversoShortcut, "activated"));
    QVERIFY(reversoPopup->isVisible());
    QCOMPARE(reversoPopup->property("translatedWord").toString(), QStringLiteral("ordinary"));
    QVERIFY(!reversoInputContainer->isVisible());
    QVERIFY(QMetaObject::invokeMethod(reversoPopup, "close"));
    QVERIFY(QMetaObject::invokeMethod(reversoButton, "clicked",
                                      QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY(reversoPopup->isVisible());
    QVERIFY(reversoInputContainer->isVisible());
    QVERIFY(reversoSubmit->isVisible());
    container->setProperty("status", container->property("inactiveStatus"));
    QVERIFY(!wolframShortcut->property("enabled").toBool());
    QVERIFY(!wolframPopup->isVisible());
    QVERIFY(!reversoShortcut->property("enabled").toBool());
    QVERIFY(!reversoPopup->isVisible());
}

void AutoLoginTests::completionTabHint()
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
    QQuickItem *input = qobject_cast<QQuickItem *>(editor->property("_editor").value<QObject *>());
    QQuickItem *hint = page->findChild<QQuickItem *>(QStringLiteral("latexCompletionHint"));
    QQuickItem *popup = page->findChild<QQuickItem *>(QStringLiteral("latexCompletionPopup"));
    QObject *text = page->findChild<QObject *>(QStringLiteral("latexCompletionPopupText"));
    QVERIFY(input && hint && popup && text);
    page->setProperty("currentFile", QStringLiteral("/main.tex"));
    page->setProperty("initialized", true);
    input->forceActiveFocus();

    for (int i = 1; i <= 5; ++i) {
        editor->setProperty("text", QStringLiteral("\\beg"));
        input->setProperty("cursorPosition", 4);
        QTRY_COMPARE(client.completionHintCount, i);
        QVERIFY(hint->isVisible());
        QVERIFY(!popup->isVisible());
        QCOMPARE(hint->property("text").toString(), QStringLiteral("in{}  <- Press Tab to accept"));
        if (i == 1) {
            editor->setProperty("text", QStringLiteral("\\beg trailing"));
            input->setProperty("cursorPosition", 4);
            QCoreApplication::processEvents();
            QCOMPARE(client.completionHintCount, i);
            QVERIFY(popup->isVisible());
            QCOMPARE(text->property("text").toString(), QStringLiteral("in{}  <- Press Tab to accept"));
        }
        editor->setProperty("text", QStringLiteral("\\be"));
        input->setProperty("cursorPosition", 3);
        QCoreApplication::processEvents();
        QCOMPARE(client.completionHintCount, i);
        editor->setProperty("text", QStringLiteral("plain"));
        QVERIFY(!popup->isVisible());
        QTRY_VERIFY(!page->property("completionWasAvailable").toBool());
    }

    editor->setProperty("text", QStringLiteral("\\beg trailing"));
    input->setProperty("cursorPosition", 4);
    QCOMPARE(client.completionHintCount, 5);
    QVERIFY(popup->isVisible());
    QCOMPARE(text->property("text").toString(), QStringLiteral("in{}"));
    editor->setProperty("text", QStringLiteral("\\beg"));
    input->setProperty("cursorPosition", 4);
    QVERIFY(!popup->isVisible());
    QCOMPARE(hint->property("text").toString(), QStringLiteral("in{}"));
}

void AutoLoginTests::findsAndReplacesText()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    view.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    view.resize(600, 800);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    view.setSource(base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty()
             ? QString() : view.errors().first().toString()));
    QQuickItem *page = view.rootObject();
    page->setVisible(true);
    page->setProperty("initialized", true);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QObject *search = page->findChild<QObject *>(QStringLiteral("searchInput"));
    QObject *replacement = page->findChild<QObject *>(QStringLiteral("replaceInput"));
    QObject *shortcut = page->findChild<QObject *>(QStringLiteral("editorSearchShortcut"));
    QObject *menu = page->findChild<QObject *>(QStringLiteral("editorSearch"));
    QObject *previous = page->findChild<QObject *>(QStringLiteral("searchPrevious"));
    QObject *next = page->findChild<QObject *>(QStringLiteral("searchNext"));
    QObject *close = page->findChild<QObject *>(QStringLiteral("searchClose"));
    QObject *replace = page->findChild<QObject *>(QStringLiteral("searchReplace"));
    QObject *replaceAll = page->findChild<QObject *>(QStringLiteral("searchReplaceAll"));
    QVERIFY(editor && search && replacement && shortcut && menu && previous && next && close && replace && replaceAll);
    QCOMPARE(shortcut->property("portableText").toString(), QStringLiteral("Ctrl+F"));
    QVERIFY(QMetaObject::invokeMethod(shortcut, "activated"));
    QVERIFY(page->property("searchVisible").toBool());
    page->setProperty("searchVisible", false);
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(menu, "clicked",
                                      QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY(page->property("searchVisible").toBool());
    QQuickItem *input = qobject_cast<QQuickItem *>(editor->property("_editor").value<QObject *>());
    QVERIFY(input);
    editor->setProperty("text", QStringLiteral("one TWO one"));
    input->setProperty("cursorPosition", 0);
    search->setProperty("text", QStringLiteral("one"));
    replacement->setProperty("text", QStringLiteral("x"));
    QVERIFY(QMetaObject::invokeMethod(next, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(input->property("selectionStart").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(next, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(input->property("selectionStart").toInt(), 8);
    QVERIFY(QMetaObject::invokeMethod(page, "findText", Q_ARG(QVariant, false)));
    QCOMPARE(input->property("selectionStart").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(previous, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(input->property("selectionStart").toInt(), 8);
    QVERIFY(QMetaObject::invokeMethod(page, "replaceMatch"));
    QCOMPARE(input->property("text").toString(), QStringLiteral("one TWO x"));
    QVERIFY(QMetaObject::invokeMethod(page, "replaceAllMatches"));
    QCOMPARE(input->property("text").toString(), QStringLiteral("x TWO x"));
    QVERIFY(QMetaObject::invokeMethod(input, "undo"));
    QCOMPARE(input->property("text").toString(), QStringLiteral("one TWO x"));
    search->setProperty("text", QStringLiteral("two"));
    QVERIFY(QMetaObject::invokeMethod(page, "findText", Q_ARG(QVariant, false)));
    QCOMPARE(page->property("searchMessage").toString(), QStringLiteral("No matches found."));
    editor->setProperty("text", QStringLiteral("a.$ a.$"));
    search->setProperty("text", QStringLiteral(".$"));
    replacement->setProperty("text", QStringLiteral("$1"));
    QVERIFY(QMetaObject::invokeMethod(page, "replaceAllMatches"));
    QCOMPARE(input->property("text").toString(), QStringLiteral("a$1 a$1"));
    search->setProperty("text", QString());
    QVERIFY(QMetaObject::invokeMethod(page, "replaceAllMatches"));
    QCOMPARE(input->property("text").toString(), QStringLiteral("a$1 a$1"));
    search->setProperty("text", QStringLiteral("a"));
    replacement->setProperty("text", QString());
    QVERIFY(!replace->property("enabled").toBool());
    QVERIFY(!replaceAll->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(page, "replaceMatch"));
    QVERIFY(QMetaObject::invokeMethod(page, "replaceAllMatches"));
    QCOMPARE(input->property("text").toString(), QStringLiteral("a$1 a$1"));
    replacement->setProperty("text", QStringLiteral(" "));
    QVERIFY(replace->property("enabled").toBool());
    QVERIFY(replaceAll->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(page, "replaceAllMatches"));
    QCOMPARE(input->property("text").toString(), QStringLiteral(" $1  $1"));
    QVERIFY(QMetaObject::invokeMethod(close, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QVERIFY(!page->property("searchVisible").toBool());
    QVERIFY(page->property("dirty").toBool());
}

void AutoLoginTests::editorScrollsLongDocuments()
{
    IsolatedAiSettings settings;
    IsolatedAiData aiData;
    LocalOnlyProxy proxy;
    QVERIFY(aiData.saveKey());
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    FakePageStack stack;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
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
    editor->setProperty("text", QStringLiteral("Line of text\n").repeated(100) + QStringLiteral("needle\nneedle"));
    renderControl.polishItems();
    QTRY_VERIFY(pane->property("contentHeight").toReal() > pane->height());
    QObject *search = page->findChild<QObject *>(QStringLiteral("searchInput"));
    QQuickItem *panel = page->findChild<QQuickItem *>(QStringLiteral("editorSearchPanel"));
    QVERIFY(search && panel);
    page->setProperty("searchVisible", true);
    QQuickItem *actions = page->findChild<QQuickItem *>(QStringLiteral("searchActions"));
    QQuickItem *row = page->findChild<QQuickItem *>(QStringLiteral("searchActionsRow"));
    QVERIFY(actions && row);
    renderControl.polishItems();
    const QStringList actionNames = {QStringLiteral("searchPrevious"), QStringLiteral("searchNext"),
                                     QStringLiteral("searchReplace"), QStringLiteral("searchReplaceAll"),
                                     QStringLiteral("searchClose")};
    qreal previousRight = 0;
    for (const QString &name : actionNames) {
        QQuickItem *item = page->findChild<QQuickItem *>(name);
        QVERIFY(item);
        QCOMPARE(item->parentItem(), row);
        QVERIFY(item->x() >= previousRight);
        previousRight = item->x() + item->width();
        QCOMPARE(item->y() + item->height() / 2, row->height() / 2);
        if (name == QStringLiteral("searchPrevious") || name == QStringLiteral("searchNext")
                || name == QStringLiteral("searchClose"))
            QCOMPARE(item->width(), item->height());
    }
    QVERIFY(actions->property("contentWidth").toReal() >= previousRight);
    const QStringList iconNames = {QStringLiteral("searchPrevious"), QStringLiteral("searchNext"),
                                   QStringLiteral("searchClose")};
    const QStringList iconSources = {QStringLiteral("image://theme/icon-m-up-alt"),
                                     QStringLiteral("image://theme/icon-m-down-alt"),
                                     QStringLiteral("image://theme/icon-m-cancel")};
    for (int i = 0; i < iconNames.size(); ++i) {
        QObject *button = page->findChild<QObject *>(iconNames.at(i));
        QObject *icon = button->findChild<QObject *>(QStringLiteral("image"));
        QVERIFY(icon);
        QCOMPARE(icon->property("source").toUrl().toString(), iconSources.at(i));
    }
    search->setProperty("text", QStringLiteral("needle"));
    page->setProperty("initialized", true);
    QQuickItem *searchField = qobject_cast<QQuickItem *>(search->property("_editor").value<QObject *>());
    QQuickItem *textInput = qobject_cast<QQuickItem *>(editor->property("_editor").value<QObject *>());
    QObject *previousShortcut = page->findChild<QObject *>(QStringLiteral("searchPreviousShortcut"));
    QObject *nextShortcut = page->findChild<QObject *>(QStringLiteral("searchNextShortcut"));
    QVERIFY(searchField && textInput && previousShortcut && nextShortcut);
    window.show();
    window.requestActivate();
    textInput->forceActiveFocus();
    QTRY_VERIFY(textInput->hasActiveFocus());
    OpenRouterClient *aiClient = page->findChild<OpenRouterClient *>(QStringLiteral("editorAiClient"));
    QVERIFY(aiClient);
    QVERIFY(aiClient->authenticated());
    const QString longContext = QStringLiteral("a").repeated(600) + QStringLiteral("b").repeated(200);
    QVariant contextResult;
    QVERIFY(QMetaObject::invokeMethod(page.data(), "aiCompletionContext", Q_RETURN_ARG(QVariant, contextResult),
                                      Q_ARG(QVariant, longContext), Q_ARG(QVariant, 600)));
    const QVariantMap context = contextResult.value<QJSValue>().toVariant().toMap();
    QCOMPARE(context.value(QStringLiteral("before")).toString(), QStringLiteral("a").repeated(512));
    QCOMPARE(context.value(QStringLiteral("after")).toString(), QStringLiteral("b").repeated(128));
    const bool oldAiEnabled = aiClient->aiEnabled();
    aiClient->setAiEnabled(false);
    const int disabledPushes = stack.pushes;
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(stack.pushes, disabledPushes);
    QVERIFY(!aiClient->busy());
    aiClient->setAiEnabled(true);
    editor->setProperty("text", QStringLiteral("\\beg"));
    textInput->setProperty("cursorPosition", 4);
    QCOMPARE(page->property("latexCompletion").toString(), QStringLiteral("in{}"));
    QTest::qWait(3200);
    QVERIFY(!aiClient->busy());
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
    QVERIFY(aiClient->busy());
    QVERIFY(page->property("aiCompletionPending").toBool());
    QQuickItem *aiProgress = page->findChild<QQuickItem *>(QStringLiteral("aiCompletionProgress"));
    QVERIFY(aiProgress && aiProgress->isVisible());
    QCOMPARE(aiProgress->parentItem(), pane->parentItem());
    QVERIFY(aiProgress->width() > 0 && aiProgress->width() <= pane->width());
    QCOMPARE(aiProgress->property("leftMargin").toReal(), qreal(0));
    QCOMPARE(aiProgress->property("rightMargin").toReal(), qreal(0));
    const QRectF cursorRect = textInput->property("cursorRectangle").toRectF();
    const QPointF cursorPoint = textInput->mapToItem(pane, cursorRect.topLeft());
    QCOMPARE(aiProgress->x(), pane->x() + qMax(qreal(0), qMin(pane->width() - aiProgress->width(), cursorPoint.x())));
    QCOMPARE(aiProgress->y(), pane->y() + qMax(qreal(0), cursorPoint.y() + cursorRect.height()));
    QCOMPARE(page->property("latexCompletion").toString(), QStringLiteral("in{}"));
    emit aiClient->resultReady(QStringLiteral(
        "```json\n{\"action\":\"replace\",\"old_text\":\"\\\\beg\",\"new_text\":\"\\\\begin{}\"}\n```"));
    aiClient->cancel();
    QVERIFY(!aiProgress->isVisible());
    QCOMPARE(page->property("aiSuggestion").toString(), QStringLiteral("\\begin{}"));
    QVERIFY(page->property("latexCompletion").toString().isEmpty());
    QQuickItem *popup = page->findChild<QQuickItem *>(QStringLiteral("latexCompletionPopup"));
    QObject *popupText = page->findChild<QObject *>(QStringLiteral("latexCompletionPopupText"));
    QVERIFY(popup && popupText && popup->isVisible());
    QVERIFY(popupText->property("text").toString().contains(QStringLiteral("\\beg")));
    QVERIFY(popupText->property("text").toString().contains(QStringLiteral("\\begin{}")));
    QTest::keyClick(&window, Qt::Key_Tab);
    QCOMPARE(textInput->property("text").toString(), QStringLiteral("\\begin{}"));

    textInput->setProperty("cursorPosition", textInput->property("text").toString().length());
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
    QVERIFY(aiClient->busy());
    textInput->setProperty("cursorPosition", 1);
    QVERIFY(!aiClient->busy());
    QVERIFY(!aiProgress->isVisible());
    QVERIFY(!page->property("aiCompletionPending").toBool());
    emit aiClient->resultReady(QStringLiteral(" stale"));
    QVERIFY(page->property("aiSuggestion").toString().isEmpty());
    textInput->setProperty("cursorPosition", textInput->property("text").toString().length());
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
    QVERIFY(aiClient->busy());
    client.offline = true;
    emit client.offlineChanged();
    QVERIFY(!aiClient->busy());
    QVERIFY(!page->property("aiCompletionPending").toBool());
    client.offline = false;
    emit client.offlineChanged();
    const int pushes = stack.pushes;
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    QQuickItem *generatorPopup = page->findChild<QQuickItem *>(QStringLiteral("aiGeneratorPopup"));
    QVERIFY(generatorPopup);
    QTRY_VERIFY(generatorPopup->isVisible());
    QCOMPARE(stack.pushes, pushes);
    QVERIFY(QMetaObject::invokeMethod(generatorPopup, "close"));
    QTest::qWait(20);
    textInput->forceActiveFocus();
    editor->setProperty("text", QStringLiteral("Line of text\n").repeated(100) + QStringLiteral("needle\nneedle"));
    const int cursor = textInput->property("text").toString().length();
    textInput->setProperty("cursorPosition", cursor);
    page->setProperty("aiRequestText", editor->property("text"));
    page->setProperty("aiRequestFile", page->property("currentFile"));
    page->setProperty("aiRequestPosition", cursor);
    page->setProperty("aiCompletionPending", true);
    emit aiClient->resultReady(QStringLiteral("{\"action\":\"insert\",\"new_text\":\" AI\"}"));
    QCOMPARE(page->property("aiSuggestion").toString(), QStringLiteral(" AI"));
    QTest::keyClick(&window, Qt::Key_Tab);
    QTRY_VERIFY(textInput->property("text").toString().endsWith(QStringLiteral("needle AI")));
    page->setProperty("aiRequestText", QStringLiteral("stale"));
    page->setProperty("aiRequestPosition", textInput->property("cursorPosition"));
    page->setProperty("aiCompletionPending", true);
    emit aiClient->resultReady(QStringLiteral(" stale"));
    QVERIFY(page->property("aiSuggestion").toString().isEmpty());
    const QString beforeMidline = textInput->property("text").toString();
    const int midlinePosition = beforeMidline.length() - 3;
    textInput->setProperty("cursorPosition", midlinePosition);
    page->setProperty("aiSuggestion", QStringLiteral("inline"));
    page->setProperty("aiRequestText", editor->property("text"));
    page->setProperty("aiRequestFile", page->property("currentFile"));
    page->setProperty("aiRequestPosition", midlinePosition);
    page->setProperty("aiCompletionStart", midlinePosition);
    page->setProperty("aiCompletionEnd", midlinePosition);
    QVERIFY(popup && popupText);
    QVERIFY(popup->isVisible());
    QCOMPARE(popupText->property("text").toString(), QStringLiteral("inline"));
    QTest::keyClick(&window, Qt::Key_Tab);
    QCOMPARE(textInput->property("text").toString(),
             beforeMidline.left(midlinePosition) + QStringLiteral("inline") + beforeMidline.mid(midlinePosition));

    editor->setProperty("text", QStringLiteral("xabcx"));
    textInput->setProperty("cursorPosition", 3);
    page->setProperty("aiRequestText", editor->property("text"));
    page->setProperty("aiRequestFile", page->property("currentFile"));
    page->setProperty("aiRequestPosition", 3);
    page->setProperty("aiCompletionPending", true);
    emit aiClient->resultReady(QStringLiteral("{\"action\":\"replace\",\"old_text\":\"abc\",\"new_text\":\"z\"}"));
    QCOMPARE(page->property("aiCompletionStart").toInt(), 1);
    QCOMPARE(page->property("aiCompletionEnd").toInt(), 4);
    QTest::keyClick(&window, Qt::Key_Tab);
    QCOMPARE(textInput->property("text").toString(), QStringLiteral("xzx"));

    editor->setProperty("text", QStringLiteral("aaaa"));
    textInput->setProperty("cursorPosition", 2);
    page->setProperty("aiRequestText", editor->property("text"));
    page->setProperty("aiRequestFile", page->property("currentFile"));
    page->setProperty("aiRequestPosition", 2);
    page->setProperty("aiCompletionPending", true);
    emit aiClient->resultReady(QStringLiteral("{\"action\":\"replace\",\"old_text\":\"aa\",\"new_text\":\"z\"}"));
    QVERIFY(page->property("aiSuggestion").toString().isEmpty());
    QVERIFY(!page->property("aiCompletionError").toString().isEmpty());
    QCOMPARE(textInput->property("text").toString(), QStringLiteral("aaaa"));

    page->setProperty("aiCompletionPending", true);
    emit aiClient->resultReady(QStringLiteral("{\"action\":\"replace\",\"old_text\":\"missing\",\"new_text\":\"z\"}"));
    QVERIFY(page->property("aiSuggestion").toString().isEmpty());
    QVERIFY(!page->property("aiCompletionError").toString().isEmpty());
    page->setProperty("aiCompletionPending", true);
    emit aiClient->resultReady(QStringLiteral("not JSON"));
    QVERIFY(page->property("aiSuggestion").toString().isEmpty());
    QVERIFY(!page->property("aiCompletionError").toString().isEmpty());
    QVERIFY(popup->isVisible());
    QCOMPARE(popupText->property("text").toString(), page->property("aiCompletionError").toString());

    QCoreApplication::processEvents();
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
    QVERIFY(aiClient->busy());
    QTRY_VERIFY(!aiClient->busy());
    QVERIFY(!aiProgress->isVisible());
    QVERIFY(!aiClient->error().isEmpty());
    QCOMPARE(page->property("aiCompletionError").toString(), aiClient->error());
    QVERIFY(popup->isVisible());
    QCOMPARE(popupText->property("text").toString(), aiClient->error());
    const int errorChatPushes = stack.pushes;
    QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(generatorPopup->isVisible());
    QCOMPARE(stack.pushes, errorChatPushes);
    OpenRouterClient *generatorClient = page->findChild<OpenRouterClient *>(QStringLiteral("aiGeneratorClient"));
    QVERIFY(generatorClient);
    QVERIFY(generatorClient != aiClient);
    QCOMPARE(generatorPopup->property("client").value<QObject *>(), static_cast<QObject *>(generatorClient));
    QVERIFY(generatorClient->authenticated());
    QVERIFY(generatorClient->error().isEmpty());
    QVERIFY(!aiClient->error().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(generatorPopup, "close"));
    textInput->forceActiveFocus();
    aiClient->signOut();
    QVERIFY(!aiClient->authenticated());
    for (int attempt = 0; attempt < 2; ++attempt) {
        QTest::keyClick(&window, Qt::Key_I, Qt::ControlModifier);
        QVERIFY(!aiClient->busy());
        QVERIFY(!page->property("aiCompletionPending").toBool());
        QVERIFY(!page->property("aiCompletionError").toString().isEmpty());
        QVERIFY(popup->isVisible());
        QCOMPARE(popupText->property("text").toString(), aiClient->error());
    }
    aiClient->setAiEnabled(oldAiEnabled);
    editor->setProperty("text", QStringLiteral("Line of text\n").repeated(100) + QStringLiteral("needle\nneedle"));
    searchField->forceActiveFocus();
    QTRY_VERIFY(searchField->hasActiveFocus());
    QVERIFY(previousShortcut->property("enabled").toBool());
    QVERIFY(nextShortcut->property("enabled").toBool());
    textInput->setProperty("cursorPosition", 0);
    QTest::keyClick(&window, Qt::Key_Down);
    const int first = textInput->property("selectionStart").toInt();
    QCOMPARE(first, QStringLiteral("Line of text\n").repeated(100).size());
    QTest::keyClick(&window, Qt::Key_Down);
    QCOMPARE(textInput->property("selectionStart").toInt(), first + 7);
    QTest::keyClick(&window, Qt::Key_Up);
    QCOMPARE(textInput->property("selectionStart").toInt(), first);
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!page->property("searchVisible").toBool());
    page->setProperty("searchVisible", true);
    QObject *replaceField = page->findChild<QObject *>(QStringLiteral("replaceInput"));
    QVERIFY(replaceField);
    QQuickItem *replaceEditor = qobject_cast<QQuickItem *>(replaceField->property("_editor").value<QObject *>());
    QVERIFY(replaceEditor);
    replaceEditor->forceActiveFocus();
    QTRY_VERIFY(replaceEditor->hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_Down);
    QCOMPARE(textInput->property("selectionStart").toInt(), first + 7);
    replaceField->setProperty("text", QStringLiteral("x"));
    QObject *replaceShortcut = page->findChild<QObject *>(QStringLiteral("searchReplaceShortcut"));
    QVERIFY(replaceShortcut);
    QVERIFY(replaceShortcut->property("enabled").toBool());
    QTest::keyClick(&window, Qt::Key_Tab);
    QVERIFY(textInput->property("text").toString().endsWith(QStringLiteral("needle\nx")));
    QVERIFY(replaceEditor->hasActiveFocus());
    searchField->forceActiveFocus();
    QTRY_VERIFY(searchField->hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_Tab);
    QVERIFY(textInput->property("text").toString().endsWith(QStringLiteral("x\nx")));
    QVERIFY(searchField->hasActiveFocus());
    editor->setProperty("text", QStringLiteral("Line of text\n").repeated(100) + QStringLiteral("needle\nneedle"));
    client.offline = true;
    emit client.offlineChanged();
    QVERIFY(!replaceShortcut->property("enabled").toBool());
    client.offline = false;
    emit client.offlineChanged();
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!page->property("searchVisible").toBool());
    page->setProperty("searchVisible", true);
    textInput->forceActiveFocus();
    QTRY_VERIFY(textInput->hasActiveFocus());
    QVERIFY(!nextShortcut->property("enabled").toBool());
    QVERIFY(!replaceShortcut->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(page.data(), "findText", Q_ARG(QVariant, false)));
    QVERIFY(pane->property("contentY").toReal() > 0);
    QVERIFY(panel->isVisible());
    QCOMPARE(panel->y(), qreal(0));
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!page->property("searchVisible").toBool());
    QVERIFY(!nextShortcut->property("enabled").toBool());
    const qreal bottom = pane->property("contentHeight").toReal() - pane->height();
    QVERIFY(pane->setProperty("contentY", bottom));
    QCOMPARE(pane->property("contentY").toReal(), bottom);
    pane->setProperty("contentY", 0);
    window.show();
    const QPointF point = pane->mapToScene(QPointF(pane->width() / 2, pane->height() / 2));
    QWheelEvent wheel(point, -120, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &wheel);
    QVERIFY(pane->property("contentY").toReal() > 0);
    QQuickItem *appBar = page->findChild<QQuickItem *>(QStringLiteral("editorAppBar"));
    QVERIFY(appBar);
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("editorPulleyWheel")));
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
    page->setProperty("largeScreen", true);
    window.setGeometry(0, 0, 800, 600);
    pageItem->setSize(QSizeF(800, 600));
    textInput->forceActiveFocus();
    QTRY_VERIFY(textInput->hasActiveFocus());
    const int midDocument = QStringLiteral("Line of text\n").repeated(40).length();
    textInput->setProperty("cursorPosition", midDocument);
    QQuickItem *contentItem = qobject_cast<QQuickItem *>(pane->property("contentItem").value<QObject *>());
    QVERIFY(contentItem);
    const QRectF beforePolishCursor = textInput->property("cursorRectangle").toRectF();
    const QPointF documentPoint = textInput->mapToItem(contentItem, beforePolishCursor.topLeft());
    pane->setProperty("contentY", qMax(qreal(0), documentPoint.y() - pane->height() / 2));
    page->setProperty("aiRequestPosition", midDocument);
    page->setProperty("aiCompletionPending", true);
    renderControl.polishItems();
    QVERIFY(aiProgress->isVisible());
    QVERIFY(aiProgress->width() > 0 && aiProgress->width() <= pane->width());
    const QRectF middleCursor = textInput->property("cursorRectangle").toRectF();
    const QPointF middlePoint = textInput->mapToItem(pane, middleCursor.topLeft());
    QCOMPARE(aiProgress->x(), pane->x() + qMax(qreal(0), qMin(pane->width() - aiProgress->width(), middlePoint.x())));
    const qreal expectedY = middlePoint.y() + middleCursor.height() + aiProgress->height() <= pane->height()
            ? qMax(qreal(0), middlePoint.y() + middleCursor.height())
            : qMax(qreal(0), middlePoint.y() - aiProgress->height());
    QCOMPARE(aiProgress->y(), pane->y() + expectedY);
    page->setProperty("aiCompletionError", QStringLiteral("OpenRouter error"));
    QVERIFY(popup->isVisible());
    QCOMPARE(popupText->property("text").toString(), QStringLiteral("OpenRouter error"));
    QVERIFY(popup->x() >= 0 && popup->x() + popup->width() <= pane->width());
    QVERIFY(popup->y() >= 0 && popup->y() + popup->height() <= pane->height());
    page->setProperty("aiCompletionError", QString());
    const qreal previousY = aiProgress->y();
    pane->setProperty("contentY", pane->property("contentY").toReal() + 20);
    QCOMPARE(aiProgress->y(), previousY - 20);
    client.pdfOnLeft = true;
    emit client.pdfOnLeftChanged();
    const QPointF movedPoint = textInput->mapToItem(pane, middleCursor.topLeft());
    QCOMPARE(aiProgress->x(), pane->x() + qMax(qreal(0), qMin(pane->width() - aiProgress->width(), movedPoint.x())));
    QVERIFY(aiProgress->x() + aiProgress->width() <= pane->x() + pane->width());
    client.pdfOnLeft = false;
    emit client.pdfOnLeftChanged();
    page->setProperty("aiCompletionPending", false);
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
    const QPointF editorPoint = pane->mapToScene(QPointF(pane->width() / 2, pane->height() / 2));
    QTest::mouseMove(&window, pdfPoint.toPoint());
    page->setProperty("zoomTarget", QStringLiteral("pdf"));
    QTest::mouseMove(&window, editorPoint.toPoint());
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
    QTest::mouseMove(&window, editorPoint.toPoint());
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

void AutoLoginTests::offlineEditorDoesNotUseLegacyDraft()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    client.offline = true;
    QVERIFY(drafts.saveFile(client.server(), QStringLiteral("one"), QStringLiteral("/main.tex"),
                            QStringLiteral("another account's draft")));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("projectId", QStringLiteral("one"));
    component.completeCreate();
    QCOMPARE(client.mainOpens, 1);
    client.setSource(QStringLiteral("/main.tex"), QStringLiteral("cached server text"));
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    QCOMPARE(editor->property("text").toString(), QStringLiteral("cached server text"));
    QObject *input = editor->property("_editor").value<QObject *>();
    QVERIFY(input);
    QVERIFY(input->property("readOnly").toBool());
    client.error = QStringLiteral("connection failed");
    emit client.errorChanged();
    QObject *error = page->findChild<QObject *>(QStringLiteral("editorError"));
    QVERIFY(error);
    QCOMPARE(error->property("text").toString(), QStringLiteral("connection failed"));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "saveAndCompile"));
    QCOMPARE(client.saves, 0);
    QCOMPARE(client.compiles, 0);
    QVERIFY(drafts.loadFile(client.server(), QStringLiteral("one"), QStringLiteral("/main.tex")));
    QCOMPARE(drafts.text(), QStringLiteral("another account's draft"));
    client.offline = false;
    emit client.offlineChanged();
    QVERIFY(client.openedPath.isEmpty());
    emit client.reconnected();
    QCOMPARE(client.openedPath, QStringLiteral("/main.tex"));
}

void AutoLoginTests::projectViewFiltersArchived()
{
    FakeProjectClient client;
    client.projectsCached = true;
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
    QObject *create = page->findChild<QObject *>(QStringLiteral("createProject"));
    QObject *archive = page->findChild<QObject *>(QStringLiteral("showArchived"));
    QObject *trash = page->findChild<QObject *>(QStringLiteral("showTrash"));
    QObject *server = page->findChild<QObject *>(QStringLiteral("changeServer"));
    QObject *settings = page->findChild<QObject *>(QStringLiteral("projectSettings"));
    QQuickItem *pageItem = qobject_cast<QQuickItem *>(page.data());
    QQuickItem *appBar = page->findChild<QQuickItem *>(QStringLiteral("projectsAppBar"));
    QQuickItem *listItem = qobject_cast<QQuickItem *>(list);
    QVERIFY(list);
    QVERIFY(pageItem && appBar && listItem);
    QVERIFY(loading);
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("refreshProjects")));
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("projectsRefreshSpacer")));
    QVERIFY(create && archive && trash && server && settings);
    const QList<QQuickItem *> actionOrder = {
        qobject_cast<QQuickItem *>(create), qobject_cast<QQuickItem *>(archive),
        qobject_cast<QQuickItem *>(trash), qobject_cast<QQuickItem *>(server),
        qobject_cast<QQuickItem *>(settings)
    };
    const QList<QQuickItem *> appBarItems = actionOrder.first()->parentItem()->childItems();
    for (int i = 0; i + 1 < actionOrder.size(); ++i) {
        const int position = appBarItems.indexOf(actionOrder.at(i));
        QVERIFY(position >= 0);
        QVERIFY(position < appBarItems.indexOf(actionOrder.at(i + 1)));
    }
    QCOMPARE(create->property("text").toString(), QStringLiteral("New project"));
    QCOMPARE(archive->property("text").toString(), QStringLiteral("Archive"));
    QCOMPARE(trash->property("text").toString(), QStringLiteral("Trash"));
    QCOMPARE(server->property("text").toString(), QStringLiteral("Server"));
    QCOMPARE(settings->property("text").toString(), QStringLiteral("Settings"));
    for (const QSize &size : {QSize(600, 800), QSize(800, 600)}) {
        pageItem->setSize(size);
        QTRY_COMPARE(pageItem->width(), qreal(size.width()));
        QCOMPARE(appBar->width(), pageItem->width());
        QVERIFY(qAbs(listItem->y() - (appBar->y() + appBar->height())) < 1.0);
        QVERIFY(qAbs(listItem->height() + listItem->y() - pageItem->height()) < 1.0);
    }
    QVERIFY(!loading->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshProjectsFromPull"));
    QCOMPARE(client.refreshCount, 1);
    QVERIFY(page->property("pullRefreshPending").toBool());
    client.projectsCached = false;
    client.setBusy(true);
    QVERIFY(page->property("pullRefreshPending").toBool());
    QVERIFY(loading->property("enabled").toBool());
    QCOMPARE(loading->property("text").toString(), QStringLiteral("Loading projects…"));
    client.setBusy(false);
    QTRY_VERIFY(!page->property("pullRefreshPending").toBool());
    QVERIFY(!loading->property("enabled").toBool());
    QCOMPARE(list->property("count").toInt(), 1);
    const QVariantList active = list->property("model").toList();
    QCOMPARE(active.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Active"));
    page->setProperty("showArchived", true);
    QCOMPARE(archive->property("text").toString(), QStringLiteral("Projects"));
    QVERIFY(!create->property("visible").toBool());
    QCOMPARE(list->property("count").toInt(), 1);
    const QVariantList archived = list->property("model").toList();
    QCOMPARE(archived.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Archived"));
    page->setProperty("showTrash", true);
    QVERIFY(!archive->property("visible").toBool());
    QCOMPARE(trash->property("text").toString(), QStringLiteral("Projects"));
    QCOMPARE(list->property("count").toInt(), 2);
}

void AutoLoginTests::createsProjectFromToolbar()
{
    FakeProjectClient client;
    FakePageStack stack;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectsPage.qml"))));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QObject *action = page->findChild<QObject *>(QStringLiteral("createProject"));
    QVERIFY(action);
    QVERIFY(action->property("enabled").toBool());
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(action, "clicked",
                                      QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QQmlComponent *dialogComponent = qobject_cast<QQmlComponent *>(stack.pushedComponent);
    QVERIFY(dialogComponent);
    QScopedPointer<QObject> dialog(dialogComponent->create());
    QVERIFY2(dialog, qPrintable(dialogComponent->errorString()));
    QObject *input = dialog->findChild<QObject *>(QStringLiteral("projectNameInput"));
    QVERIFY(input);
    input->setProperty("text", QStringLiteral("  "));
    QVERIFY(!dialog->property("canAccept").toBool());
    input->setProperty("text", QStringLiteral("  Thesis  "));
    QVERIFY(dialog->property("canAccept").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "accepted"));
    QCOMPARE(client.createdName, QStringLiteral("Thesis"));
    client.projectCreationBusy = true;
    emit client.projectCreationChanged();
    QVERIFY(!action->property("enabled").toBool());
}

void AutoLoginTests::managesProjectTrash()
{
    FakeProjectClient client;
    FakePageStack stack;
    FakeFileEditor editor;
    editor.projectId = QStringLiteral("one");
    client.coverEditor = &editor;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QVERIFY(drafts.saveLastProject(client.server(), QStringLiteral("one")));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectsPage.qml"))));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    const QVariant project = QVariantMap{{QStringLiteral("id"), QStringLiteral("one")},
                                         {QStringLiteral("name"), QStringLiteral("Active")}};
    editor.dirty = true;
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openProjectActionDialog",
                                      Q_ARG(QVariant, project), Q_ARG(QVariant, QVariant("trash"))));
    QCOMPARE(stack.pushes, 0);
    editor.dirty = false;
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openProjectActionDialog",
                                      Q_ARG(QVariant, project), Q_ARG(QVariant, QVariant("trash"))));
    QCOMPARE(stack.pushes, 1);
    QQmlComponent *dialogComponent = qobject_cast<QQmlComponent *>(stack.pushedComponent);
    QVERIFY(dialogComponent);
    QScopedPointer<QObject> dialog(dialogComponent->beginCreate(engine.rootContext()));
    QVERIFY(dialog);
    dialog->setProperty("project", project);
    dialog->setProperty("action", QStringLiteral("trash"));
    dialogComponent->completeCreate();
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "accepted"));
    QCOMPARE(client.actionProject, QStringLiteral("one"));
    QCOMPARE(client.actionName, QStringLiteral("trash"));
    QCOMPARE(drafts.loadLastProject(client.server()), QStringLiteral("one"));
    emit client.projectTrashChanged(QStringLiteral("one"), QStringLiteral("trash"));
    QVERIFY(drafts.loadLastProject(client.server()).isEmpty());
    QVERIFY(!client.coverEditor);
}

void AutoLoginTests::opensCollaborationPage()
{
    OverleafClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectCollaborationPage.qml"))));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("projectId", QStringLiteral("test"));
    page->setProperty("mode", QStringLiteral("chat"));
    component.completeCreate();
    QObject *collaboration = client.collaboration();
    QVERIFY(collaboration);
    QVERIFY(!collaboration->property("error").toString().isEmpty());
}

void AutoLoginTests::pullRefreshPages()
{
    FakeFileClient client;
    FakeFileEditor editor;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    for (const QString &file : {QStringLiteral("ProjectFilesPage.qml"), QStringLiteral("CompileRootPage.qml")}) {
        QQmlComponent component(&engine, base.resolved(QUrl(file)));
        QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
        QVERIFY2(page, qPrintable(component.errorString()));
        page->setProperty("projectId", QStringLiteral("project"));
        page->setProperty("editorPage", QVariant::fromValue(static_cast<QObject *>(&editor)));
        component.completeCreate();
        const int initialLoads = client.loadCount;
        client.asyncLoad = true;
        QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshFromPull"));
        QCOMPARE(client.loadCount, initialLoads + 1);
        QVERIFY(page->property("pullRefreshPending").toBool());
        QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshFromPull"));
        QCOMPARE(client.loadCount, initialLoads + 1);
        client.filesErrorValue = QStringLiteral("network error");
        emit client.filesErrorChanged();
        client.finishFileLoad();
        QTRY_VERIFY(!page->property("pullRefreshPending").toBool());
        client.filesErrorValue.clear();
        emit client.filesErrorChanged();
        client.asyncLoad = false;
        QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshFromPull"));
        QTRY_VERIFY(!page->property("pullRefreshPending").toBool());
        QCOMPARE(client.loadCount, initialLoads + 2);
        if (file == QStringLiteral("ProjectFilesPage.qml")) {
            client.offline = true;
            emit client.offlineChanged();
            QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshFromPull"));
            QCOMPARE(client.loadCount, initialLoads + 2);
            client.offline = false;
            emit client.offlineChanged();
            client.sourceBusy = true;
            emit client.sourceBusyChanged();
            QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshFromPull"));
            QCOMPARE(client.loadCount, initialLoads + 2);
            client.sourceBusy = false;
            emit client.sourceBusyChanged();
        }
    }

    FakeCollaborationHost host;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &host);
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectCollaborationPage.qml"))));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshFromPull"));
    QCOMPARE(host.client.refreshCount, 1);
    QVERIFY(page->property("pullRefreshPending").toBool());
    QVERIFY(QMetaObject::invokeMethod(page.data(), "refreshFromPull"));
    QCOMPARE(host.client.refreshCount, 1);
    host.client.errorValue = QStringLiteral("network error");
    host.client.finishRefresh();
    QTRY_VERIFY(!page->property("pullRefreshPending").toBool());
}

void AutoLoginTests::historyNavigatesToCurrentFile()
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
    page->setProperty("visible", true);
    page->setProperty("projectId", QStringLiteral("project"));
    page->setProperty("currentFile", QStringLiteral("/main.tex"));
    page->setProperty("initialized", true);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    const QString text = QStringLiteral("first\nunique passage\nlast");
    editor->setProperty("text", text);
    page->setProperty("baseText", text);
    page->setProperty("baseKnown", true);
    client.setPdfPath(QStringLiteral("/nonexistent-test.pdf"));
    QVariant result;
    const QVariant target = QVariantMap{{QStringLiteral("path"), QStringLiteral("main.tex")},
                                        {QStringLiteral("text"), QStringLiteral("unique passage")}};
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openHistoryLocation", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, target)));
    QVERIFY(result.toBool());
    QObject *input = editor->property("_editor").value<QObject *>();
    QVERIFY(input);
    QCOMPARE(input->property("cursorPosition").toInt(), 6);
    QCOMPARE(client.syncFile, QStringLiteral("/main.tex"));
    QCOMPARE(client.syncLine, 2);
    QCOMPARE(client.syncColumn, 0);
    client.syncLine = 0;
    const QVariant ambiguous = QVariantMap{{QStringLiteral("path"), QStringLiteral("main.tex")},
                                           {QStringLiteral("text"), QStringLiteral("s")}};
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openHistoryLocation", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, ambiguous)));
    QVERIFY(result.toBool());
    QCOMPARE(client.syncLine, 0);
    const QVariant invalid = QVariantMap{{QStringLiteral("path"), QStringLiteral("../escape.tex")}};
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openHistoryLocation", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, invalid)));
    QVERIFY(!result.toBool());
}

void AutoLoginTests::historyListUpdatesVisibleEntries()
{
    FakeCollaborationHost host;
    QVariantList entries;
    for (int i = 0; i < 100; ++i) {
        entries.append(QVariantMap{
            {QStringLiteral("body"), QStringLiteral("Version %1").arg(i)},
            {QStringLiteral("changes"), QVariantList{
                QVariantMap{{QStringLiteral("action"), QStringLiteral("Edited")},
                            {QStringLiteral("path"), QStringLiteral("main.tex")}}}}});
    }
    host.client.model.reset(entries);
    QQuickView view;
    view.resize(600, 800);
    view.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &host);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(view.engine());
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        import "." as Pages
        ApplicationWindow {
            width: 600; height: 800
            initialPage: Component { Pages.ProjectCollaborationPage { projectId: "test"; mode: "history" } }
        }
    )", base);
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    qobject_cast<QQuickItem *>(window.data())->setParentItem(view.contentItem());
    QObject *list = window->findChild<QObject *>(QStringLiteral("collaborationList"));
    QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), 100);
    QTRY_VERIFY(list->property("currentItem").value<QObject *>());
    QObject *current = list->property("currentItem").value<QObject *>();
    QPointer<QObject> first = current->findChild<QObject *>(QStringLiteral("historyChanges"));
    QVERIFY(first);
    QQuickItem *content = list->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QVERIFY(content->childItems().size() < 100);
    QVariantMap updated = entries.at(0).toMap();
    updated[QStringLiteral("preview")] = QVariantList{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("i")},
                    {QStringLiteral("text"), QStringLiteral("new content")}}};
    host.client.model.update(0, updated);
    QTRY_VERIFY(first);
    QCOMPARE(list->property("currentItem").value<QObject *>(), current);
    QTRY_VERIFY(current->property("entryData").toMap().contains(QStringLiteral("preview")));
    QObject *repeater = first->findChild<QObject *>(QStringLiteral("historyPreviewRepeater"));
    QVERIFY(repeater);
    QTRY_COMPARE(repeater->property("count").toInt(), 1);
    QQuickItem *preview = nullptr;
    QVERIFY(QMetaObject::invokeMethod(repeater, "itemAt", Q_RETURN_ARG(QQuickItem *, preview), Q_ARG(int, 0)));
    QVERIFY(preview);
    QCOMPARE(preview->property("text").toString(), QStringLiteral("+ new content"));
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
    const QVariant folder = QVariantMap{{QStringLiteral("type"), QStringLiteral("folder")},
                                        {QStringLiteral("path"), QStringLiteral("/figures")}};
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openFile", Q_ARG(QVariant, folder)));
    QCOMPARE(stack.pushProperties.value(QStringLiteral("directory")).toString(), QStringLiteral("/figures"));
    QVERIFY(!stack.pushProperties.value(QStringLiteral("ownsFiles")).toBool());
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

void AutoLoginTests::folderBackNavigation()
{
    FakeFileClient client;
    FakeFileEditor editor;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("fakeEditor"), &editor);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        ApplicationWindow {
            property var rootFiles
            property var folderFiles
            property var activePage: pageStack.currentPage
            initialPage: Component { Page { id: editorPage } }
            function openFiles() {
                rootFiles = pageStack.push(Qt.resolvedUrl("ProjectFilesPage.qml"),
                    { projectId: "project", projectName: "Project", editorPage: fakeEditor },
                    PageStackAction.Immediate)
            }
            function openFolder() {
                rootFiles.openFile({ type: "folder", path: "/figures" })
                pageStack.completeAnimation()
                folderFiles = pageStack.currentPage
            }
            function back() { pageStack.navigateBack(PageStackAction.Immediate) }
        }
    )", base);
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "openFiles"));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "openFolder"));
    QObject *folder = window->property("folderFiles").value<QObject *>();
    QVERIFY(folder);
    QCOMPARE(folder->property("directory").toString(), QStringLiteral("/figures"));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "back"));
    QCOMPARE(window->property("activePage").value<QObject *>(),
             window->property("rootFiles").value<QObject *>());
}

void AutoLoginTests::protectsDraftsDuringFileChanges()
{
    FakeFileClient client;
    client.files = {QVariantMap{{QStringLiteral("type"), QStringLiteral("doc")},
                                {QStringLiteral("path"), QStringLiteral("chapter/a.tex")}}};
    FakeFileEditor editor;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QVERIFY(drafts.saveFileWithBase(client.server(), QStringLiteral("project"),
                                    QStringLiteral("/chapter/a.tex"), QStringLiteral("unsaved"),
                                    QStringLiteral("base"), true));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectFilesPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("projectId", QStringLiteral("project"));
    page->setProperty("editorPage", QVariant::fromValue(static_cast<QObject *>(&editor)));
    QObject *list = page->findChild<QObject *>(QStringLiteral("projectFileList"));
    QVERIFY(list);
    QVariantList shown = list->property("model").toList();
    QCOMPARE(shown.size(), 1);
    QCOMPARE(shown.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("chapter"));
    QCOMPARE(shown.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("folder"));
    page->setProperty("directory", QStringLiteral("/chapter"));
    shown = list->property("model").toList();
    QCOMPARE(shown.size(), 1);
    QCOMPARE(shown.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("a.tex"));
    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(page.data(), "canChange", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QVariant("/chapter")),
                                      Q_ARG(QVariant, QVariant("folder")),
                                      Q_ARG(QVariant, QVariant("/renamed")),
                                      Q_ARG(QVariant, false)));
    QVERIFY(!result.toBool());
    QVERIFY(drafts.loadFile(client.server(), QStringLiteral("project"), QStringLiteral("/chapter/a.tex")));
    QCOMPARE(drafts.text(), QStringLiteral("unsaved"));
    QVERIFY(drafts.saveFileWithBase(client.server(), QStringLiteral("project"),
                                    QStringLiteral("/chapter/a.tex"), QStringLiteral("base"),
                                    QStringLiteral("base"), true));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "canChange", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QVariant("/chapter")),
                                      Q_ARG(QVariant, QVariant("folder")),
                                      Q_ARG(QVariant, QVariant("/renamed")),
                                      Q_ARG(QVariant, false)));
    QVERIFY(result.toBool());
    editor.currentFile = QStringLiteral("/chapter/a.tex");
    QVERIFY(QMetaObject::invokeMethod(page.data(), "canChange", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QVariant("/chapter")),
                                      Q_ARG(QVariant, QVariant("folder")),
                                      Q_ARG(QVariant, QVariant("/renamed")),
                                      Q_ARG(QVariant, false)));
    QVERIFY(!result.toBool());
}

void AutoLoginTests::savesOpenFileBeforeFileOperations()
{
    FakeFileClient client;
    client.files = {QVariantMap{{QStringLiteral("type"), QStringLiteral("doc")},
                                {QStringLiteral("path"), QStringLiteral("/chapter/a.tex")}}};
    FakeFileEditor editor;
    editor.currentFile = QStringLiteral("/chapter/a.tex");
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectFilesPage.qml"))));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("project"));
    page->setProperty("editorPage", QVariant::fromValue(static_cast<QObject *>(&editor)));
    auto start = [&]() {
        return QMetaObject::invokeMethod(page.data(), "startFileOperation",
                                         Q_ARG(QVariant, "rename"), Q_ARG(QVariant, "/chapter"),
                                         Q_ARG(QVariant, "folder"), Q_ARG(QVariant, "/renamed"),
                                         Q_ARG(QVariant, "renamed"), Q_ARG(QVariant, ""));
    };
    QVERIFY(start());
    QCOMPARE(editor.saves, 1);
    QVERIFY(client.operation.isEmpty());
    client.sourceError = QStringLiteral("conflict");
    emit client.sourceErrorChanged();
    QCOMPARE(page->property("externalError").toString(), QStringLiteral("conflict"));
    QVERIFY(client.operation.isEmpty());

    client.sourceError.clear();
    QVERIFY(start());
    emit client.sourceSaved(editor.currentFile, QStringLiteral("saved"));
    QCOMPARE(client.operation, QStringLiteral("rename"));
    QCOMPARE(client.operationPath, QStringLiteral("/chapter"));
    emit client.projectFileOperationFinished(QStringLiteral("rename"), QStringLiteral("/chapter"),
                                             QStringLiteral("folder"), QStringLiteral("/renamed"));
    QCOMPARE(editor.changedTo, QStringLiteral("/renamed"));

    client.operation.clear();
    editor.finishSuccess = false;
    QVERIFY(start());
    emit client.sourceSaved(editor.currentFile, QStringLiteral("saved"));
    QVERIFY(client.operation.isEmpty());
    QVERIFY(!page->property("externalError").toString().isEmpty());
}

void AutoLoginTests::uploadsMultipleFiles()
{
    FakeFileClient client;
    FakeFileEditor editor;
    FakePageStack stack;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ProjectFilesPage.qml"))));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    page->setProperty("projectId", QStringLiteral("project"));
    page->setProperty("directory", QStringLiteral("/figures"));
    page->setProperty("editorPage", QVariant::fromValue(static_cast<QObject *>(&editor)));
    const QVariant files = QVariantList{
        QVariantMap{{QStringLiteral("name"), QStringLiteral("one.png")},
                    {QStringLiteral("url"), QUrl(QStringLiteral("file:///one.png"))}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("two.png")},
                    {QStringLiteral("url"), QUrl(QStringLiteral("file:///two.png"))}}
    };
    QVERIFY(QMetaObject::invokeMethod(page.data(), "uploadSelected", Q_ARG(QVariant, files)));
    QCOMPARE(client.uploads.size(), 1);
    QCOMPARE(client.operationPath, QStringLiteral("/figures"));
    emit client.projectFileOverwriteRequested(QStringLiteral("/figures/one.png"),
                                              QStringLiteral("file"), QStringLiteral("file-id"));
    QCOMPARE(stack.pushes, 1);
    QCOMPARE(stack.pushProperties.value(QStringLiteral("entityId")).toString(), QStringLiteral("file-id"));
    QQmlComponent *dialogComponent = qobject_cast<QQmlComponent *>(stack.pushedComponent);
    QVERIFY(dialogComponent);
    QScopedPointer<QObject> dialog(dialogComponent->beginCreate(dialogComponent->creationContext()));
    QVERIFY(dialog);
    for (auto it = stack.pushProperties.constBegin(); it != stack.pushProperties.constEnd(); ++it)
        dialog->setProperty(it.key().toUtf8().constData(), it.value());
    dialogComponent->completeCreate();
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "accepted"));
    QCOMPARE(client.operation, QStringLiteral("overwrite"));
    QCOMPARE(client.overwriteId, QStringLiteral("file-id"));
    QCOMPARE(client.overwriteType, QStringLiteral("file"));
    emit client.projectFileOperationFinished(QStringLiteral("upload"), QStringLiteral("/figures"),
                                             QStringLiteral("folder"), QString());
    QTRY_COMPARE(client.uploads.size(), 3);
    QCOMPARE(client.uploads.at(2), QUrl(QStringLiteral("file:///two.png")));
    emit client.projectFileOperationFinished(QStringLiteral("upload"), QStringLiteral("/figures"),
                                             QStringLiteral("folder"), QString());
    QTRY_VERIFY(page->property("backNavigation").toBool());

    QVERIFY(QMetaObject::invokeMethod(page.data(), "uploadSelected", Q_ARG(QVariant, files)));
    QCOMPARE(client.uploads.size(), 4);
    emit client.projectFileOverwriteRequested(QStringLiteral("/figures/one.png"),
                                              QStringLiteral("file"), QStringLiteral("file-id"));
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "rejected"));
    QTRY_COMPARE(client.uploads.size(), 5);
    QCOMPARE(client.uploads.last(), QUrl(QStringLiteral("file:///two.png")));
    emit client.projectFileOperationFinished(QStringLiteral("upload"), QStringLiteral("/figures"),
                                             QStringLiteral("folder"), QString());
    QTRY_VERIFY(page->property("backNavigation").toBool());

    QVERIFY(QMetaObject::invokeMethod(page.data(), "uploadSelected", Q_ARG(QVariant, files)));
    QCOMPARE(client.uploads.size(), 6);
    client.sourceError = QStringLiteral("upload failed");
    emit client.sourceErrorChanged();
    QCOMPARE(page->property("externalError").toString(), QStringLiteral("upload failed"));
    QVERIFY(page->property("backNavigation").toBool());
    QTest::qWait(150);
    QCOMPARE(client.uploads.size(), 6);
    client.sourceError.clear();
    page->setProperty("externalError", QString());

    QVERIFY(drafts.saveFile(client.server(), QStringLiteral("project"),
                            QStringLiteral("/figures/one.png"), QStringLiteral("unsaved")));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "uploadSelected", Q_ARG(QVariant, files)));
    QCOMPARE(client.uploads.size(), 6);
    QVERIFY(!page->property("externalError").toString().isEmpty());
}

void AutoLoginTests::updatesEditorAfterFileOperations()
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
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("project"));
    page->setProperty("currentFile", QStringLiteral("/chapter/a.tex"));
    page->setProperty("compileRoot", QStringLiteral("/chapter/a.tex"));
    page->setProperty("initialized", true);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    editor->setProperty("text", QStringLiteral("changed"));
    QVariant saved;
    QVERIFY(QMetaObject::invokeMethod(page.data(), "finishFileOperationSave", Q_RETURN_ARG(QVariant, saved),
                                      Q_ARG(QVariant, "/chapter/a.tex"), Q_ARG(QVariant, "changed")));
    QVERIFY(saved.toBool());
    QVERIFY(QMetaObject::invokeMethod(page.data(), "fileOperationFinished",
                                      Q_ARG(QVariant, "rename"), Q_ARG(QVariant, "/chapter"),
                                      Q_ARG(QVariant, "folder"), Q_ARG(QVariant, "/renamed")));
    QCOMPARE(page->property("currentFile").toString(), QStringLiteral("/renamed/a.tex"));
    QCOMPARE(page->property("compileRoot").toString(), QStringLiteral("/renamed/a.tex"));
    QCOMPARE(drafts.loadLastFile(client.server(), QStringLiteral("project")), QStringLiteral("/renamed/a.tex"));
    QVERIFY(drafts.loadFile(client.server(), QStringLiteral("project"), QStringLiteral("/renamed/a.tex")));
    QCOMPARE(drafts.text(), QStringLiteral("changed"));
    QVERIFY(drafts.baseKnown());
    QCOMPARE(drafts.baseText(), QStringLiteral("changed"));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "fileOperationFinished",
                                      Q_ARG(QVariant, "delete"), Q_ARG(QVariant, "/renamed/a.tex"),
                                      Q_ARG(QVariant, "doc"), Q_ARG(QVariant, "")));
    QVERIFY(page->property("currentFile").toString().isEmpty());
    QVERIFY(page->property("compileRoot").toString().isEmpty());
    QVERIFY(editor->property("text").toString().isEmpty());
    QVERIFY(drafts.loadLastFile(client.server(), QStringLiteral("project")).isEmpty());
    QVERIFY(drafts.error().isEmpty());
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
    page->setProperty("largeScreen", true);
    page->setProperty("width", 800);
    page->setProperty("height", 600);
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
    QVERIFY(drafts.loadFile(client.server(), QStringLiteral("test"), QStringLiteral("/main.tex")));
    QCOMPARE(drafts.text(), serverText);
    QVERIFY(drafts.baseKnown());
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
    QVERIFY(drafts.saveFileWithBase(client.server(), QStringLiteral("test"), QStringLiteral("/chapter.tex"),
                                   QStringLiteral("cached local"), QStringLiteral("cached base"), true));
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
    QCOMPARE(client.mainOpens, 0);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    QCOMPARE(editor->property("text").toString(), QStringLiteral("cached local"));
    QCOMPARE(page->property("baseText").toString(), QStringLiteral("cached base"));
    client.setSource(QStringLiteral("/chapter.tex"), QStringLiteral("changed server"));
    QCOMPARE(editor->property("text").toString(), QStringLiteral("cached local"));
    client.setSource(QStringLiteral("/other.bib"), QStringLiteral("content"));
    QCOMPARE(drafts.loadLastFile(client.server(), QStringLiteral("test")), QStringLiteral("/other.bib"));
}

void AutoLoginTests::opensMainForNewProject()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("EditorPage.qml"))));
    QScopedPointer<QObject> page(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("new-project"));
    component.completeCreate();
    QCOMPARE(client.mainOpens, 1);
    client.setSource(QStringLiteral("/paper.tex"), QStringLiteral("document"));
    QCOMPARE(page->property("currentFile").toString(), QStringLiteral("/paper.tex"));
    QCOMPARE(page->property("compileRoot").toString(), QStringLiteral("/paper.tex"));
    page.reset();

    QVERIFY(drafts.save(client.server(), QStringLiteral("draft-project"), QStringLiteral("local draft")));
    client.mainOpens = 0;
    page.reset(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("draft-project"));
    component.completeCreate();
    QCOMPARE(client.mainOpens, 0);
    QObject *editor = page->findChild<QObject *>(QStringLiteral("draftEditor"));
    QVERIFY(editor);
    QCOMPARE(editor->property("text").toString(), QStringLiteral("local draft"));
    page.reset();

    QVERIFY(drafts.saveCompileRoot(client.server(), QStringLiteral("root-project"), QStringLiteral("/chosen.tex")));
    client.openedPath.clear();
    page.reset(component.beginCreate(engine.rootContext()));
    QVERIFY(page);
    page->setProperty("projectId", QStringLiteral("root-project"));
    component.completeCreate();
    QCOMPARE(client.openedPath, QStringLiteral("/chosen.tex"));
    QCOMPARE(client.mainOpens, 0);
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

void AutoLoginTests::removesServerCookies()
{
    FakeRemovalClient client;
    FakeRemovalCookies cookies;
    QQmlEngine engine;
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("ServerCookieRemoval.qml"))));
    QScopedPointer<QObject> controller(component.beginCreate(engine.rootContext()));
    QVERIFY2(controller, qPrintable(component.errorString()));
    controller->setProperty("client", QVariant::fromValue(static_cast<QObject *>(&client)));
    controller->setProperty("cookieManager", QVariant::fromValue(static_cast<QObject *>(&cookies)));
    component.completeCreate();
    QVERIFY(QMetaObject::invokeMethod(controller.data(), "start",
                                     Q_ARG(QVariant, QStringLiteral("https://example.org"))));
    QVERIFY(client.pending);
    QCOMPARE(cookies.reads, 1);
    const QVariantMap first{{QStringLiteral("domain"), QStringLiteral("example.org")},
                            {QStringLiteral("path"), QStringLiteral("/")},
                            {QStringLiteral("name"), QStringLiteral("session")}};
    const QVariantMap second{{QStringLiteral("domain"), QStringLiteral("example.org")},
                             {QStringLiteral("path"), QStringLiteral("/private")},
                             {QStringLiteral("name"), QStringLiteral("other")}};
    const QVariantMap unrelated{{QStringLiteral("domain"), QStringLiteral("other.example")},
                                {QStringLiteral("path"), QStringLiteral("/")},
                                {QStringLiteral("name"), QStringLiteral("session")}};
    emit cookies.allCookiesAvailable({first, first, second, unrelated});
    QCOMPARE(cookies.deleted.size(), 1);
    QVERIFY(client.removed.isEmpty());
    emit cookies.cookiesDeleted(1);
    QCOMPARE(cookies.deleted.size(), 2);
    QVERIFY(client.removed.isEmpty());
    emit cookies.cookiesDeleted(1);
    QVERIFY(client.removed.isEmpty());
    QCOMPARE(cookies.reads, 2);
    emit cookies.allCookiesAvailable({unrelated});
    QCOMPARE(client.removed, QStringLiteral("https://example.org"));
    QVERIFY(!client.pending);
    client.removed.clear();
    cookies.allowRead = false;
    QVERIFY(QMetaObject::invokeMethod(controller.data(), "start",
                                     Q_ARG(QVariant, QStringLiteral("https://example.org"))));
    QVERIFY(!client.pending);
    QVERIFY(client.removed.isEmpty());
    QVERIFY(!client.error.isEmpty());
    cookies.allowRead = true;
    client.error.clear();
    QVERIFY(QMetaObject::invokeMethod(controller.data(), "start",
                                     Q_ARG(QVariant, QStringLiteral("https://example.org"))));
    emit cookies.allCookiesAvailable({});
    emit cookies.allCookiesAvailable({first});
    QVERIFY(!client.pending);
    QVERIFY(client.removed.isEmpty());
    QVERIFY(!client.error.isEmpty());
}

void AutoLoginTests::reconnectsInBackground()
{
    FakeBackgroundClient client;
    QQmlEngine engine;
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("BackgroundLogin.qml"))));
    QScopedPointer<QObject> controller(component.beginCreate(engine.rootContext()));
    QVERIFY2(controller, qPrintable(component.errorString()));
    controller->setProperty("client", QVariant::fromValue(static_cast<QObject *>(&client)));
    component.completeCreate();
    client.offline = true;
    emit client.offlineChanged();
    QTRY_COMPARE(client.attempts, 1);
    emit client.reconnectRequested();
    QCOMPARE(client.attempts, 2);
    client.busy = true;
    emit client.reconnectRequested();
    QCOMPARE(client.attempts, 2);
    client.busy = false;
    client.offline = false;
    emit client.reconnectRequested();
    QCOMPARE(client.attempts, 2);
}

void AutoLoginTests::opensCachedAccountAtStartup()
{
    FakeLoginClient client;
    client.cachedAccounts = {QVariantMap{{QStringLiteral("server"), QStringLiteral("https://cached.example")},
                                        {QStringLiteral("userId"), QStringLiteral("user-one")},
                                        {QStringLiteral("label"), QStringLiteral("User")}}};
    FakePageStack stack;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStack"), &stack);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine, base.resolved(QUrl(QStringLiteral("LoginPage.qml"))));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    QTRY_COMPARE(stack.pushes, 1);
    QCOMPARE(client.selected, QStringLiteral("https://cached.example"));
    QCOMPARE(client.cachedUser, QStringLiteral("user-one"));
    QVERIFY(stack.lastUrl.toString().endsWith(QStringLiteral("ProjectsPage.qml")));
    QObject *repeater = page->findChild<QObject *>(QStringLiteral("savedAccounts"));
    QVERIFY(repeater);
    QQuickItem *row = nullptr;
    QVERIFY(QMetaObject::invokeMethod(repeater, "itemAt", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, 0)));
    QVERIFY(row);
    QObject *remove = row->findChild<QObject *>(QStringLiteral("removeSavedAccount"));
    QVERIFY(remove);
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(remove, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(stack.pushProperties.value(QStringLiteral("accountId")).toString(), QStringLiteral("user-one"));
    QQmlComponent *dialogComponent = qobject_cast<QQmlComponent *>(stack.pushedComponent);
    QVERIFY(dialogComponent);
    QScopedPointer<QObject> dialog(dialogComponent->beginCreate(engine.rootContext()));
    QVERIFY(dialog);
    for (auto it = stack.pushProperties.constBegin(); it != stack.pushProperties.constEnd(); ++it)
        dialog->setProperty(it.key().toUtf8().constData(), it.value());
    dialogComponent->completeCreate();
    QVERIFY(QMetaObject::invokeMethod(dialog.data(), "accepted"));
    QCOMPARE(client.removedAccount, QStringLiteral("user-one"));
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
    QObject *previousButton = previous->findChild<QObject *>(QStringLiteral("previousServer-1"));
    QVERIFY(previousButton);
    QCOMPARE(previousButton->property("text").toString(), QStringLiteral("https://other.example"));
    QVERIFY(QMetaObject::invokeMethod(page.data(), "openServer",
                                     Q_ARG(QVariant, previousButton->property("text"))));
    QCOMPARE(client.selected, QStringLiteral("https://other.example"));
    QCOMPARE(stack.pushes, 2);
    QObject *removeServer = previous->findChild<QObject *>(QStringLiteral("removeSavedServer"));
    QVERIFY(removeServer);
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(removeServer, "clicked", QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(stack.pushProperties.value(QStringLiteral("serverAddress")).toString(), QStringLiteral("https://other.example"));
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
    client.autoRestoreServer = false;
    QScopedPointer<QObject> forgottenPage(component.create());
    QVERIFY2(forgottenPage, qPrintable(component.errorString()));
    QVERIFY(!forgottenPage->property("restoringServer").toBool());
    QTest::qWait(50);
    QCOMPARE(stack.pushes, 0);
    client.autoRestoreServer = true;
    client.remembered = QStringLiteral("https://example.org");
    client.acceptServer = false;
    QScopedPointer<QObject> failedPage(component.create());
    QVERIFY2(failedPage, qPrintable(component.errorString()));
    QTRY_VERIFY(!failedPage->property("restoringServer").toBool());
    QCOMPARE(stack.pushes, 0);
}

void AutoLoginTests::editorAppBarActions()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    client.commentsAvailable = false;
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
    QQuickItem *appBar = page->findChild<QQuickItem *>(QStringLiteral("editorAppBar"));
    QQuickItem *appBarScroller = page->findChild<QQuickItem *>(QStringLiteral("editorAppBarScroller"));
    QQuickItem *content = page->findChild<QQuickItem *>(QStringLiteral("editorContent"));
    QVERIFY(appBar && appBarScroller);
    QVERIFY(content);
    const QStringList actionNames = {
        QStringLiteral("saveAndCompile"), QStringLiteral("editorProjectFiles"),
        QStringLiteral("editorMoreActions"), QStringLiteral("downloadProjectArchive"),
        QStringLiteral("openCachedPdf"),
        QStringLiteral("editorSearch"), QStringLiteral("projectChat"),
        QStringLiteral("projectHistory"), QStringLiteral("projectSharing"),
        QStringLiteral("projectComments"), QStringLiteral("editorAiAssistant"),
        QStringLiteral("editorKeyboardShortcuts"),
        QStringLiteral("editorSettings")
    };
    QList<QQuickItem *> actions;
    for (const QString &name : actionNames) {
        QQuickItem *action = page->findChild<QQuickItem *>(name);
        QVERIFY(action);
        actions.append(action);
    }
    QVERIFY(!actions.at(9)->property("enabled").toBool());
    QQuickItem *shortcutList = page->findChild<QQuickItem *>(QStringLiteral("editorShortcutList"));
    QVERIFY(shortcutList);
    const QString shortcutText = shortcutList->property("text").toString();
    QVERIFY(shortcutText.contains(QStringLiteral("Ctrl+S")));
    QVERIFY(shortcutText.contains(QStringLiteral("Ctrl+F")));
    QVERIFY(shortcutText.contains(QStringLiteral("Ctrl++ / Ctrl+-")));
    QVERIFY(shortcutText.contains(QStringLiteral("Escape")));
    QQuickItem *moreShortcutList = page->findChild<QQuickItem *>(
        QStringLiteral("editorMoreShortcutList"));
    QVERIFY(moreShortcutList);
    QCOMPARE(moreShortcutList->property("text").toString(), shortcutText);
    QObject *morePopup = page->findChild<QObject *>(QStringLiteral("editorMorePopup"));
    QVERIFY(morePopup);
    for (const QString &name : {
             QStringLiteral("moreDownloadProjectArchive"), QStringLiteral("moreEditorSearch"),
             QStringLiteral("moreProjectChat"), QStringLiteral("moreProjectHistory"),
             QStringLiteral("moreProjectSharing"), QStringLiteral("moreProjectComments"),
             QStringLiteral("moreKeyboardShortcuts"), QStringLiteral("moreEditorSettings")
         }) {
        QVERIFY(page->findChild<QObject *>(name));
    }
    QVERIFY(page->findChild<QObject *>(QStringLiteral("moreAiAssistant")));
    const QList<QQuickItem *> appBarItems = actions.first()->parentItem()->childItems();
    QQuickItem *reverso = page->findChild<QQuickItem *>(QStringLiteral("editorReverso"));
    QQuickItem *wolfram = page->findChild<QQuickItem *>(QStringLiteral("editorWolframAlpha"));
    QVERIFY(reverso && wolfram);
    QVERIFY(appBarItems.indexOf(reverso) >= 0);
    QVERIFY(appBarItems.indexOf(reverso) < appBarItems.indexOf(wolfram));
    QVERIFY(appBarItems.indexOf(wolfram) < appBarItems.indexOf(actions.at(10)));
    QQuickItem *rightActionsSpacer = page->findChild<QQuickItem *>(
        QStringLiteral("editorRightActionsSpacer"));
    QVERIFY(rightActionsSpacer);
    for (int i = 0; i + 1 < actions.size(); ++i) {
        const int position = appBarItems.indexOf(actions.at(i));
        QVERIFY(position >= 0);
        QVERIFY(position < appBarItems.indexOf(actions.at(i + 1)));
    }
    QVERIFY(appBarItems.indexOf(actions.at(2)) < appBarItems.indexOf(rightActionsSpacer));
    QVERIFY(appBarItems.indexOf(rightActionsSpacer) < appBarItems.indexOf(actions.at(3)));
    QQuickItem *sciHubPopup = page->findChild<QQuickItem *>(QStringLiteral("sciHubPopup"));
    QQuickItem *sciHubHat = page->findChild<QQuickItem *>(QStringLiteral("sciHubHat"));
    QQuickItem *sciHubTitle = page->findChild<QQuickItem *>(QStringLiteral("sciHubTitle"));
    QQuickItem *sciHubInput = page->findChild<QQuickItem *>(QStringLiteral("sciHubInput"));
    QQuickItem *sciHubSearch = page->findChild<QQuickItem *>(QStringLiteral("sciHubSearch"));
    QQuickItem *wolframFrame = page->findChild<QQuickItem *>(QStringLiteral("wolframAlphaInputFrame"));
    QVERIFY(sciHubPopup && sciHubHat && sciHubTitle && sciHubInput && sciHubSearch && wolframFrame);
    const qreal popupRadius = wolframFrame->property("radius").toReal();
    QVERIFY(popupRadius > 0);
    for (const QString &name : {QStringLiteral("sciHubPopup"), QStringLiteral("reversoPopup"),
                                QStringLiteral("aiGeneratorPopup"), QStringLiteral("wolframAlphaInputPopup"),
                                QStringLiteral("wolframAlphaPopup"), QStringLiteral("latexCompletionPopup")}) {
        QQuickItem *popup = page->findChild<QQuickItem *>(name);
        QVERIFY(popup);
        QCOMPARE(popup->property("radius").toReal(), popupRadius);
    }
    sciHubPopup->setProperty("visible", true);
    for (const QSize &size : {QSize(600, 800), QSize(800, 600)}) {
        view.resize(size);
        page->setSize(size);
        QTRY_COMPARE(page->width(), qreal(size.width()));
        QCOMPARE(appBarScroller->width(), page->width());
        QVERIFY(qAbs(content->y() - (appBarScroller->y() + appBarScroller->height())) < 1.0);
        QVERIFY(qAbs(content->height() + content->y() - page->height()) < 1.0);
        QTRY_COMPARE(sciHubHat->property("status").toInt(), 1);
        QCOMPARE(sciHubPopup->y(), qreal(0));
        QCOMPARE(sciHubPopup->y(), page->findChild<QQuickItem *>(
                     QStringLiteral("wolframAlphaInputPopup"))->y());
        QVERIFY(sciHubHat->width() > 0 && sciHubHat->height() > 0);
        const QRectF hatRect(sciHubHat->mapToItem(sciHubPopup, QPointF()),
                             QSizeF(sciHubHat->width(), sciHubHat->height()));
        const QFontMetricsF titleMetrics(qvariant_cast<QFont>(sciHubTitle->property("font")));
        QVERIFY(qAbs(hatRect.x() - sciHubHat->property("baseX").toReal()
                     - titleMetrics.width(QStringLiteral("Sci"))) < 1.0);
        QVERIFY(qAbs(hatRect.y() + sciHubHat->height() * 0.62
                     + titleMetrics.height() / 3) < 1.0);
        const QRectF titleRect(sciHubTitle->mapToItem(sciHubPopup, QPointF()),
                               QSizeF(sciHubTitle->width(), sciHubTitle->height()));
        QVERIFY(hatRect.intersects(titleRect));
        QCOMPARE(sciHubInput->parentItem(), sciHubSearch->parentItem());
        QVERIFY(sciHubInput->width() > 0);
        QVERIFY(sciHubInput->x() + sciHubInput->width() <= sciHubSearch->x());
        QQuickItem *sciHubText = qobject_cast<QQuickItem *>(sciHubInput->property("_editor").value<QObject *>());
        QVERIFY(sciHubText);
        const qreal textCenter = sciHubText->mapToItem(sciHubInput->parentItem(),
                                                       QPointF(0, sciHubText->height() / 2)).y();
        QVERIFY(qAbs(textCenter - sciHubSearch->y() - sciHubSearch->height() / 2) < 1.0);
    }
    const QStringList labels = {
        QStringLiteral("Compile"), QStringLiteral("Files"), QStringLiteral("More"),
        QString(),         QString(), QString(), QString(), QString(), QString(),
        QString(), QString(), QString(), QString()
    };
    for (int i = 0; i < actions.size(); ++i) {
        QCOMPARE(actions.at(i)->property("text").toString(), labels.at(i));
        if (!labels.at(i).isEmpty())
            QCOMPARE(actions.at(i)->property("layoutDirection").toInt(), int(Qt::LeftToRight));
    }
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("editorPulleyWheel")));
    QCOMPARE(appBar->property("headerText").toString(), QString());
}

void AutoLoginTests::editorAppBarMouseClicks()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        ApplicationWindow {
            width: 800; height: 600
            property var editorPage
            property var activePage: pageStack.currentPage
            initialPage: Component { Page {} }
            function openEditor() {
                editorPage = pageStack.push(Qt.resolvedUrl("EditorPage.qml"),
                    { projectId: "test", largeScreen: true }, PageStackAction.Immediate)
            }
        }
    )", base);
    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.resize(800, 600);
    window.contentItem()->setSize(QSizeF(800, 600));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QQuickItem *rootItem = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(rootItem);
    rootItem->setParentItem(window.contentItem());
    QVERIFY(QMetaObject::invokeMethod(root.data(), "openEditor"));
    QObject *page = root->property("editorPage").value<QObject *>();
    QVERIFY(page);
    QQuickItem *scroller = page->findChild<QQuickItem *>(QStringLiteral("editorAppBarScroller"));
    QQuickItem *settings = page->findChild<QQuickItem *>(QStringLiteral("editorSettings"));
    QQuickItem *reverso = page->findChild<QQuickItem *>(QStringLiteral("editorReverso"));
    QQuickItem *reversoPopup = page->findChild<QQuickItem *>(QStringLiteral("reversoPopup"));
    QQuickItem *wolfram = page->findChild<QQuickItem *>(QStringLiteral("editorWolframAlpha"));
    QQuickItem *wolframPopup = page->findChild<QQuickItem *>(QStringLiteral("wolframAlphaInputPopup"));
    QQuickItem *sciHub = page->findChild<QQuickItem *>(QStringLiteral("editorSciHub"));
    QQuickItem *sciHubPopup = page->findChild<QQuickItem *>(QStringLiteral("sciHubPopup"));
    QQuickItem *sciHubHat = page->findChild<QQuickItem *>(QStringLiteral("sciHubHat"));
    QQuickItem *aiPopup = page->findChild<QQuickItem *>(QStringLiteral("aiGeneratorPopup"));
    QQuickItem *wolframResult = page->findChild<QQuickItem *>(QStringLiteral("wolframAlphaPopup"));
    QQuickItem *content = page->findChild<QQuickItem *>(QStringLiteral("editorContent"));
    QVERIFY(scroller && settings && reverso && reversoPopup && wolfram && wolframPopup
            && sciHub && sciHubPopup && sciHubHat && aiPopup && wolframResult && content);
    for (const QString &name : {QStringLiteral("aiGeneratorClose"), QStringLiteral("reversoClose"),
                                QStringLiteral("reversoQuickClose"), QStringLiteral("sciHubClose"),
                                QStringLiteral("wolframAlphaInputClose"), QStringLiteral("wolframAlphaClose")})
        QVERIFY(!page->findChild<QObject *>(name));
    QTest::qWait(50);
    renderControl.polishItems();
    QVERIFY(page->property("wideTablet").toBool());
    QVERIFY(settings->isVisible() && settings->isEnabled());
    QVERIFY(settings->property("text").toString().isEmpty());
    QVERIFY(scroller->property("contentWidth").toReal() > scroller->width());

    const QPointF top = scroller->mapToScene(QPointF(0, 0));
    for (int i = 0; i < 4; ++i) {
        const QPoint start = (top + QPointF(700, scroller->height() / 2)).toPoint();
        const QPoint end = (top + QPointF(100, scroller->height() / 2)).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        for (int x = 650; x >= 100; x -= 50)
            QTest::mouseMove(&window, (top + QPointF(x, scroller->height() / 2)).toPoint(), 10);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(50);
    }
    QVERIFY(scroller->property("contentX").toReal() > 0);
    QVERIFY(QMetaObject::invokeMethod(scroller, "cancelFlick"));
    renderControl.polishItems();
    const QPoint reversoPoint = reverso->mapToScene(
        QPointF(reverso->width() / 2, reverso->height() / 2)).toPoint();
    QVERIFY(QRect(QPoint(0, 0), window.size()).contains(reversoPoint));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, reversoPoint);
    QVERIFY(reversoPopup->isVisible());
    renderControl.polishItems();
    QQuickItem *swap = page->findChild<QQuickItem *>(QStringLiteral("reversoSwapLanguages"));
    QObject *sourceLanguage = page->findChild<QObject *>(QStringLiteral("reversoSourceLanguage"));
    QObject *targetLanguage = page->findChild<QObject *>(QStringLiteral("reversoTargetLanguage"));
    QVERIFY(swap && sourceLanguage && targetLanguage);
    const QPoint swapPoint = swap->mapToScene(QPointF(swap->width() / 2, swap->height() / 2)).toPoint();
    QVERIFY(QRect(QPoint(0, 0), window.size()).contains(swapPoint));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, swapPoint);
    QCOMPARE(sourceLanguage->property("languageCode").toString(), QStringLiteral("en"));
    QCOMPARE(targetLanguage->property("languageCode").toString(), QStringLiteral("ru"));
    const QPoint outsidePoint = scroller->mapToScene(QPointF(5, scroller->height() / 2)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, outsidePoint);
    QVERIFY(!reversoPopup->isVisible());
    const QPoint wolframPoint = wolfram->mapToScene(
        QPointF(wolfram->width() / 2, wolfram->height() / 2)).toPoint();
    QVERIFY(QRect(QPoint(0, 0), window.size()).contains(wolframPoint));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, wolframPoint);
    QVERIFY(wolframPopup->isVisible());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, outsidePoint);
    QVERIFY(!wolframPopup->isVisible());
    const QPoint sciHubPoint = sciHub->mapToScene(
        QPointF(sciHub->width() / 2, sciHub->height() / 2)).toPoint();
    QVERIFY(QRect(QPoint(0, 0), window.size()).contains(sciHubPoint));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, sciHubPoint);
    QVERIFY(sciHubPopup->isVisible());
    QVERIFY(sciHubHat->isVisible());
    const QPoint insidePoint = sciHubPopup->mapToScene(QPointF(sciHubPopup->width() - 10, 10)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, insidePoint);
    QVERIFY(sciHubPopup->isVisible());
    const QPoint contentOutside = content->mapToScene(QPointF(5, content->height() - 5)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, contentOutside);
    QVERIFY(!sciHubPopup->isVisible());
    aiPopup->setProperty("showing", true);
    QVERIFY(aiPopup->isVisible());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, outsidePoint);
    QVERIFY(!aiPopup->isVisible());
    page->setProperty("wolframVisible", true);
    QVERIFY(wolframResult->isVisible());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, outsidePoint);
    QVERIFY(!wolframResult->isVisible());
    window.show();
    window.requestActivate();
    QVERIFY(QMetaObject::invokeMethod(page, "openWolframInput"));
    QVERIFY(wolframPopup->isVisible());
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!wolframPopup->isVisible());
    QVERIFY(QMetaObject::invokeMethod(page, "openReverso"));
    QVERIFY(reversoPopup->isVisible());
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!reversoPopup->isVisible());
    sciHubPopup->setProperty("visible", true);
    QVERIFY(sciHubPopup->isVisible());
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!sciHubPopup->isVisible());
    aiPopup->setProperty("showing", true);
    QVERIFY(aiPopup->isVisible());
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!aiPopup->isVisible());
}

void AutoLoginTests::editorSettingsMouseClick()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        ApplicationWindow {
            width: 800; height: 600
            property var editorPage
            property var activePage: pageStack.currentPage
            initialPage: Component { Page {} }
            function openEditor() {
                editorPage = pageStack.push(Qt.resolvedUrl("EditorPage.qml"),
                    { projectId: "test", largeScreen: true }, PageStackAction.Immediate)
            }
        }
    )", base);
    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.resize(800, 600);
    window.contentItem()->setSize(QSizeF(800, 600));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QQuickItem *rootItem = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(rootItem);
    rootItem->setParentItem(window.contentItem());
    QVERIFY(QMetaObject::invokeMethod(root.data(), "openEditor"));
    QObject *page = root->property("editorPage").value<QObject *>();
    QVERIFY(page);
    QQuickItem *scroller = page->findChild<QQuickItem *>(QStringLiteral("editorAppBarScroller"));
    QQuickItem *settings = page->findChild<QQuickItem *>(QStringLiteral("editorSettings"));
    QVERIFY(scroller && settings);
    QTest::qWait(50);
    scroller->setProperty("contentX", scroller->property("contentWidth").toReal() - scroller->width());
    renderControl.polishItems();
    const QPoint point = settings->mapToScene(QPointF(settings->width() / 2, settings->height() / 2)).toPoint();
    QVERIFY(QRect(QPoint(0, 0), window.size()).contains(point));
    QSignalSpy clicks(settings, SIGNAL(clicked(QQuickMouseEvent*)));
    QVERIFY(clicks.isValid());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(clicks.count(), 1);
    QTRY_VERIFY(root->property("activePage").value<QObject *>() != page);
}

void AutoLoginTests::portraitAppBarNavigation()
{
    for (const QSize &size : {QSize(600, 800), QSize(360, 800)}) {
        QTemporaryDir directory;
        DraftStore drafts(directory.path());
        FakePdfClient client;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
        engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
        const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick 2.0
            import Sailfish.Silica 1.0
            ApplicationWindow {
                property var editorPage
                property var activePage: pageStack.currentPage
                initialPage: Component { Page {} }
                function openEditor(largeScreen) {
                    editorPage = pageStack.push(Qt.resolvedUrl("EditorPage.qml"),
                        { projectId: "test", largeScreen: largeScreen }, PageStackAction.Immediate)
                }
            }
        )", base);
        QQuickRenderControl renderControl;
        QQuickWindow window(&renderControl);
        window.resize(size);
        window.contentItem()->setSize(size);
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickItem *rootItem = qobject_cast<QQuickItem *>(root.data());
        QVERIFY(rootItem);
        rootItem->setParentItem(window.contentItem());
        rootItem->setSize(size);
        QVERIFY(QMetaObject::invokeMethod(root.data(), "openEditor",
                                          Q_ARG(QVariant, size.width() == 600)));
        QObject *page = root->property("editorPage").value<QObject *>();
        QVERIFY(page);
        QTRY_VERIFY(page->property("forwardNavigation").toBool());
        QObject *pdfPage = page->property("attachedPdfPage").value<QObject *>();
        QVERIFY(pdfPage);
        QQuickItem *scroller = page->findChild<QQuickItem *>(QStringLiteral("editorAppBarScroller"));
        QQuickItem *button = page->findChild<QQuickItem *>(QStringLiteral("editorSciHub"));
        QQuickItem *popup = page->findChild<QQuickItem *>(QStringLiteral("sciHubPopup"));
        QVERIFY(scroller && button && popup);
        QVERIFY(scroller->width() < size.width());
        scroller->setProperty("contentX", scroller->property("contentWidth").toReal() - scroller->width());
        renderControl.polishItems();
        const QPoint buttonPoint = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
        QVERIFY(buttonPoint.x() < scroller->width());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, buttonPoint);
        QVERIFY(popup->isVisible());
        QCOMPARE(root->property("activePage").value<QObject *>(), page);
        QVERIFY(QMetaObject::invokeMethod(page, "dismissUtilityPopups"));
        QTest::qWait(100);
        QObject *stackObject = root->property("pageStack").value<QObject *>();
        QObject *indicator = stackObject ? stackObject->property("_pageStackIndicator").value<QObject *>() : nullptr;
        QQuickItem *indicatorItem = qobject_cast<QQuickItem *>(indicator);
        QVERIFY(indicatorItem);
        const QPoint indicatorPoint = indicatorItem->mapToScene(
            QPointF(indicatorItem->width() - 2, indicatorItem->height() / 2)).toPoint();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          indicatorPoint);
        QTRY_COMPARE(root->property("activePage").value<QObject *>(), pdfPage);
    }
}

void AutoLoginTests::pdfAppBarMouseClick()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        ApplicationWindow {
            width: 360; height: 800
            property var editorPage
            property var activePage: pageStack.currentPage
            initialPage: Component { Page {} }
            function openEditor() {
                editorPage = pageStack.push(Qt.resolvedUrl("EditorPage.qml"),
                    { projectId: "test", largeScreen: false }, PageStackAction.Immediate)
            }
            function forward() { pageStack.navigateForward(PageStackAction.Immediate) }
        }
    )", base);
    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.resize(360, 800);
    window.contentItem()->setSize(QSizeF(360, 800));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QQuickItem *rootItem = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(rootItem);
    rootItem->setParentItem(window.contentItem());
    QVERIFY(QMetaObject::invokeMethod(root.data(), "openEditor"));
    QObject *editor = root->property("editorPage").value<QObject *>();
    QVERIFY(editor);
    QTRY_VERIFY(editor->property("attachedPdfPage").value<QObject *>());
    QObject *pdfPage = editor->property("attachedPdfPage").value<QObject *>();
    QVERIFY(QMetaObject::invokeMethod(root.data(), "forward"));
    QTRY_COMPARE(root->property("activePage").value<QObject *>(), pdfPage);
    QQuickItem *compile = pdfPage->findChild<QQuickItem *>(QStringLiteral("pdfPageCompile"));
    QVERIFY(compile && compile->isVisible() && compile->isEnabled());
    QVERIFY(compile->property("text").toString().isEmpty());
    QTest::qWait(50);
    renderControl.polishItems();
    const QPoint point = compile->mapToScene(
        QPointF(compile->width() / 2, compile->height() / 2)).toPoint();
    QVERIFY(QRect(QPoint(0, 0), window.size()).contains(point));
    QCOMPARE(editor->property("actionError").toString(), QString());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(editor->property("actionError").toString(),
             QStringLiteral("Open a project file before saving."));
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
    page->setProperty("largeScreen", true);
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
    QCOMPARE(code->height(), qobject_cast<QQuickItem *>(code->parentItem())->height());
    QVERIFY(!pdf->isVisible());
    QVERIFY(QMetaObject::invokeMethod(page.data(), "adjustZoom", Q_ARG(QVariant, 1)));
    QCOMPARE(client.editorFontScale, 110);
    QVERIFY(QMetaObject::invokeMethod(page.data(), "adjustZoom", Q_ARG(QVariant, -1)));
    QCOMPARE(client.editorFontScale, 100);

    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(ABEDUL_VERSION));
    QQmlComponent settings(&engine, base.resolved(QUrl(QStringLiteral("SettingsPage.qml"))));
    QVERIFY2(settings.isReady(), qPrintable(settings.errorString()));
    client.editorFontScale = 130;
    QQuickRenderControl settingsRenderControl;
    QQuickWindow settingsWindow(&settingsRenderControl);
    QScopedPointer<QObject> settingsPage(settings.create());
    QVERIFY2(settingsPage, qPrintable(settings.errorString()));
    QCOMPARE(client.editorFontScale, 130);
    QObject *slider = settingsPage->findChild<QObject *>(QStringLiteral("editorFontScaleSetting"));
    QVERIFY(slider);
    slider->setProperty("value", 140);
    QCOMPARE(client.editorFontScale, 140);
    QObject *zoomHint = settingsPage->findChild<QObject *>(QStringLiteral("zoomShortcutHint"));
    QVERIFY(zoomHint);
    QVERIFY(zoomHint->property("text").toString().contains(QStringLiteral("Ctrl")));
    QObject *saveHint = settingsPage->findChild<QObject *>(QStringLiteral("saveShortcutHint"));
    QVERIFY(saveHint);
    QVERIFY(saveHint->property("text").toString().contains(QStringLiteral("Ctrl+S")));
    QObject *apiHint = settingsPage->findChild<QObject *>(QStringLiteral("wolframAlphaApiHint"));
    QVERIFY(apiHint);
    QVERIFY(apiHint->property("text").toString().contains(QStringLiteral("Full Results API")));
    QVERIFY(apiHint->property("text").toString().contains(QStringLiteral("free")));
    QVERIFY(apiHint->property("text").toString().contains(QStringLiteral("https://products.wolframalpha.com/api")));
    QObject *about = settingsPage->findChild<QObject *>(QStringLiteral("aboutDescription"));
    QObject *links = settingsPage->findChild<QObject *>(QStringLiteral("aboutLinks"));
    QVERIFY(about);
    QVERIFY(about->property("text").toString().contains(QStringLiteral("Overleaf")));
    QVERIFY(links);
    QCOMPARE(links->property("url").toString(), QStringLiteral("https://github.com/Arpolend2/Abedul"));
    QObject *version = settingsPage->findChild<QObject *>(QStringLiteral("aboutVersion"));
    QObject *heading = settingsPage->findChild<QObject *>(QStringLiteral("aboutReferencesHeading"));
    QObject *notes = settingsPage->findChild<QObject *>(QStringLiteral("aboutReleaseNotes"));
    QVERIFY(version && heading && notes);
    QCOMPARE(version->property("text").toString(), QStringLiteral("Abedul ") + QStringLiteral(ABEDUL_VERSION));
    QVERIFY(!heading->property("text").toString().isEmpty());
    QVERIFY(notes->property("text").toString().contains(QStringLiteral("Reverso")));
    QQuickItem *settingsContent = settingsPage->findChild<QQuickItem *>(QStringLiteral("settingsContent"));
    QVERIFY(settingsContent);
    QList<QQuickItem *> references;
    for (QQuickItem *item : settingsContent->childItems()) {
        if (item->objectName() == QStringLiteral("aboutReferenceLink"))
            references.append(item);
    }
    QCOMPARE(references.size(), 5);
    QVERIFY(references.at(2)->property("url").toString().contains(QStringLiteral("pdfium")));

    QQuickItem *settingsItem = qobject_cast<QQuickItem *>(settingsPage.data());
    QQuickItem *aboutHeader = settingsPage->findChild<QQuickItem *>(QStringLiteral("aboutHeader"));
    QQuickItem *icon = settingsPage->findChild<QQuickItem *>(QStringLiteral("aboutIcon"));
    QQuickItem *description = qobject_cast<QQuickItem *>(about);
    QQuickItem *versionItem = qobject_cast<QQuickItem *>(version);
    QQuickItem *flickable = settingsPage->findChild<QQuickItem *>(QStringLiteral("settingsFlickable"));
    QVERIFY(settingsItem && aboutHeader && icon && description && versionItem && flickable);
    settingsItem->setParentItem(settingsWindow.contentItem());
    settingsItem->setVisible(true);
    const QUrl iconUrl = icon->property("source").toUrl();
    const QImage aboutImage(iconUrl.toLocalFile());
    QVERIFY(!aboutImage.isNull());
    QCOMPARE(aboutImage.pixelColor(0, 0).alpha(), 0);
    QVERIFY(aboutImage.pixelColor(aboutImage.width() / 2, aboutImage.height() / 2).alpha() > 0);
    for (const QSizeF size : { QSizeF(360, 800), QSizeF(800, 360) }) {
        settingsItem->setSize(size);
        settingsWindow.resize(size.toSize());
        settingsWindow.contentItem()->setSize(size);
        QCoreApplication::processEvents();
        settingsRenderControl.polishItems();
        const QPointF iconPos = icon->mapToItem(aboutHeader, QPointF());
        const QPointF descriptionPos = description->mapToItem(aboutHeader, QPointF());
        const QPointF versionPos = versionItem->mapToItem(aboutHeader, QPointF());
        QVERIFY(icon->width() > 0 && description->width() > 0);
        QVERIFY(iconPos.x() + icon->width() <= descriptionPos.x());
        QVERIFY(descriptionPos.x() + description->width() <= aboutHeader->width());
        QVERIFY(descriptionPos.y() + description->height() <= aboutHeader->height() + 0.1);
        QVERIFY(versionPos.y() >= 0);
        QVERIFY(flickable->property("contentHeight").toReal() > size.height());
        QVERIFY(flickable->setProperty("contentY", flickable->property("contentHeight").toReal() - size.height()));
        QVERIFY(flickable->property("contentY").toReal() > 0);
    }
}

void AutoLoginTests::portraitPdfNavigation()
{
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    FakePdfClient client;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        ApplicationWindow {
            width: 360; height: 800
            property var editorPage
            property var activePage: pageStack.currentPage
            initialPage: Component { Page {} }
            function openEditor() {
                editorPage = pageStack.push(Qt.resolvedUrl("EditorPage.qml"),
                    { projectId: "test", largeScreen: false }, PageStackAction.Immediate)
            }
            function forward() { pageStack.navigateForward(PageStackAction.Immediate) }
            function back() { pageStack.navigateBack(PageStackAction.Immediate) }
        }
    )", base);
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "openEditor"));
    QObject *editorPage = window->property("editorPage").value<QObject *>();
    QVERIFY(editorPage);
    QTRY_VERIFY(editorPage->property("attachedPdfPage").value<QObject *>());
    QObject *pdfPage = editorPage->property("attachedPdfPage").value<QObject *>();
    QVERIFY(editorPage->property("forwardNavigation").toBool());
    QQuickItem *pdf = editorPage->findChild<QQuickItem *>(QStringLiteral("editorPdfPane"));
    QQuickItem *editorAppBar = editorPage->findChild<QQuickItem *>(QStringLiteral("editorAppBar"));
    QQuickItem *editorCompile = editorPage->findChild<QQuickItem *>(QStringLiteral("saveAndCompile"));
    QQuickItem *editorOpenPdf = editorPage->findChild<QQuickItem *>(QStringLiteral("openCachedPdf"));
    QQuickItem *editorFiles = editorPage->findChild<QQuickItem *>(QStringLiteral("editorProjectFiles"));
    QQuickItem *editorMore = editorPage->findChild<QQuickItem *>(QStringLiteral("editorMoreActions"));
    QQuickItem *editorSpacer = editorPage->findChild<QQuickItem *>(
        QStringLiteral("editorRightActionsSpacer"));
    QQuickItem *pdfAppBar = pdfPage->findChild<QQuickItem *>(QStringLiteral("pdfPageAppBar"));
    QQuickItem *pdfActionsSpacer = pdfPage->findChild<QQuickItem *>(
        QStringLiteral("pdfPageActionsSpacer"));
    QQuickItem *pdfCompile = pdfPage->findChild<QQuickItem *>(QStringLiteral("pdfPageCompile"));
    QQuickItem *pdfOpenExternal = pdfPage->findChild<QQuickItem *>(QStringLiteral("pdfPageOpenExternal"));
    QVERIFY(pdf);
    QVERIFY(editorAppBar && editorCompile && editorOpenPdf && editorFiles && editorMore && editorSpacer);
    QVERIFY(pdfAppBar && pdfActionsSpacer && pdfCompile && pdfOpenExternal);
    QVERIFY(!editorPage->property("wideTablet").toBool());
    QVERIFY(editorPage->property("editorActionsInMore").toBool());
    QVERIFY(!editorCompile->property("visible").toBool());
    QVERIFY(!editorOpenPdf->property("visible").toBool());
    QVERIFY(editorFiles->property("visible").toBool());
    QVERIFY(editorMore->property("visible").toBool());
    QVERIFY(editorSpacer->property("visible").toBool());
    for (const QString &name : {
             QStringLiteral("downloadProjectArchive"), QStringLiteral("editorSearch"),
             QStringLiteral("projectChat"), QStringLiteral("projectHistory"),
             QStringLiteral("projectSharing"), QStringLiteral("projectComments"),
             QStringLiteral("editorAiAssistant"), QStringLiteral("editorKeyboardShortcuts"),
             QStringLiteral("editorSettings")
         }) {
        QQuickItem *action = editorPage->findChild<QQuickItem *>(name);
        QVERIFY(action);
        QVERIFY2(!action->property("visible").toBool(), qPrintable(name));
    }
    QVERIFY(editorPage->findChild<QObject *>(QStringLiteral("editorMorePopup")));
    QVERIFY(editorPage->findChild<QObject *>(QStringLiteral("moreKeyboardShortcuts")));
    QQuickItem *moreShortcutList = editorPage->findChild<QQuickItem *>(
        QStringLiteral("editorMoreShortcutList"));
    QVERIFY(moreShortcutList);
    QVERIFY(moreShortcutList->property("text").toString().contains(QStringLiteral("Ctrl+S")));
    QCOMPARE(pdf->parentItem(), pdfPage->property("contentHost").value<QQuickItem *>());
    QVERIFY(QMetaObject::invokeMethod(window.data(), "forward"));
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), pdfPage);
    QVERIFY(pdfAppBar->property("visible").toBool());
    QVERIFY(pdfCompile->property("visible").toBool());
    QVERIFY(pdfOpenExternal->property("visible").toBool());
    const QList<QQuickItem *> pdfAppBarItems = pdfCompile->parentItem()->childItems();
    QVERIFY(pdfAppBarItems.indexOf(pdfActionsSpacer) < pdfAppBarItems.indexOf(pdfCompile));
    QVERIFY(pdfAppBarItems.indexOf(pdfCompile) < pdfAppBarItems.indexOf(pdfOpenExternal));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "back"));
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), editorPage);
    QVERIFY(QMetaObject::invokeMethod(window.data(), "forward"));
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), pdfPage);
    window->setProperty("width", 900);
    window->setProperty("height", 600);
    qobject_cast<QQuickItem *>(editorPage)->setSize(QSizeF(900, 600));
    qobject_cast<QQuickItem *>(pdfPage)->setSize(QSizeF(900, 600));
    editorPage->setProperty("largeScreen", true);
    QVERIFY(editorPage->property("wideTablet").toBool());
    QVERIFY(!editorPage->property("editorActionsInMore").toBool());
    QVERIFY(!editorMore->property("visible").toBool());
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), editorPage);
    QVERIFY(editorCompile->property("visible").toBool());
    QVERIFY(editorOpenPdf->property("visible").toBool());
    QTRY_VERIFY(!editorPage->property("forwardNavigation").toBool());
    QQuickItem *panes = editorPage->findChild<QQuickItem *>(QStringLiteral("editorPanes"));
    QVERIFY(pdf && panes);
    QCOMPARE(pdf->parentItem(), panes);

    window->setProperty("width", 600);
    window->setProperty("height", 800);
    qobject_cast<QQuickItem *>(editorPage)->setSize(QSizeF(600, 800));
    qobject_cast<QQuickItem *>(pdfPage)->setSize(QSizeF(600, 800));
    QVERIFY(!editorPage->property("wideTablet").toBool());
    QVERIFY(editorPage->property("editorActionsInMore").toBool());
    QVERIFY(editorMore->property("visible").toBool());
    QVERIFY(!editorCompile->property("visible").toBool());
    QVERIFY(!editorOpenPdf->property("visible").toBool());
    for (const QString &name : {
             QStringLiteral("downloadProjectArchive"), QStringLiteral("editorSearch"),
             QStringLiteral("projectChat"), QStringLiteral("projectHistory"),
             QStringLiteral("projectSharing"), QStringLiteral("projectComments"),
             QStringLiteral("editorAiAssistant"), QStringLiteral("editorKeyboardShortcuts"),
             QStringLiteral("editorSettings")
         }) {
        QQuickItem *action = editorPage->findChild<QQuickItem *>(name);
        QVERIFY(action);
        QVERIFY2(!action->property("visible").toBool(), qPrintable(name));
    }

    window->setProperty("width", 900);
    window->setProperty("height", 600);
    qobject_cast<QQuickItem *>(editorPage)->setSize(QSizeF(900, 600));
    qobject_cast<QQuickItem *>(pdfPage)->setSize(QSizeF(900, 600));
    editorPage->setProperty("largeScreen", false);
    QVERIFY(!editorPage->property("wideTablet").toBool());
    QVERIFY(editorPage->property("editorActionsInMore").toBool());
    QVERIFY(editorMore->property("visible").toBool());
    QVERIFY(!editorCompile->property("visible").toBool());
    QVERIFY(!editorOpenPdf->property("visible").toBool());
    for (const QString &name : {
             QStringLiteral("downloadProjectArchive"), QStringLiteral("editorSearch"),
             QStringLiteral("projectChat"), QStringLiteral("projectHistory"),
             QStringLiteral("projectSharing"), QStringLiteral("projectComments"),
             QStringLiteral("editorAiAssistant"), QStringLiteral("editorKeyboardShortcuts"),
             QStringLiteral("editorSettings")
         }) {
        QQuickItem *action = editorPage->findChild<QQuickItem *>(name);
        QVERIFY(action);
        QVERIFY2(!action->property("visible").toBool(), qPrintable(name));
    }
    QTRY_VERIFY(editorPage->property("attachedPdfPage").value<QObject *>());
    QObject *landscapePhonePdfPage = editorPage->property("attachedPdfPage").value<QObject *>();
    QVERIFY(QMetaObject::invokeMethod(window.data(), "forward"));
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), landscapePhonePdfPage);
    QQuickItem *landscapePhonePdfAppBar = landscapePhonePdfPage->findChild<QQuickItem *>(
        QStringLiteral("pdfPageAppBar"));
    QVERIFY(landscapePhonePdfAppBar);
    QVERIFY(landscapePhonePdfAppBar->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(window.data(), "back"));
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), editorPage);

    window->setProperty("width", 360);
    window->setProperty("height", 800);
    qobject_cast<QQuickItem *>(editorPage)->setSize(QSizeF(360, 800));
    qobject_cast<QQuickItem *>(pdfPage)->setSize(QSizeF(360, 800));
    QVERIFY(!editorPage->property("wideTablet").toBool());
    QVERIFY(editorPage->property("editorActionsInMore").toBool());
    QTRY_VERIFY(editorPage->property("forwardNavigation").toBool());
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
        for (int i = 0; i < 6; ++i)
            first.recordCompletionHintShown();
        OverleafClient second;
        QCOMPARE(second.pdfOnLeft(), true);
        QCOMPARE(second.editorFontScale(), 130);
        QCOMPARE(second.completionHintCount(), 5);
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
    void *mouseEvent = nullptr;
    QVERIFY(QMetaObject::invokeMethod(menu, "clicked",
                                      QGenericArgument("QQuickMouseEvent*", &mouseEvent)));
    QCOMPARE(stack.pops, 1);
}

void AutoLoginTests::restoresLastProjectNavigation()
{
    FakeProjectClient client;
    client.offline = true;
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

void AutoLoginTests::projectForwardNavigationRequiresAttachedEditor()
{
    FakeProjectClient client;
    client.offline = true;
    client.projectsCached = true;
    QTemporaryDir directory;
    DraftStore drafts(directory.path());
    drafts.setAccount(client.server(), QStringLiteral("alice"));
    QVERIFY(drafts.loadLastProject(QStringLiteral("https://wrong.example")).isEmpty());
    QVERIFY(!drafts.error().isEmpty());
    drafts.setAccount(client.server(), QStringLiteral("bob"));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("overleafClient"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("draftStore"), &drafts);
    const QUrl base = QUrl::fromLocalFile(QStringLiteral(AUTO_LOGIN_QML_PATH));
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.0
        import Sailfish.Silica 1.0
        ApplicationWindow {
            property var projectsPage
            property var activePage: pageStack.currentPage
            initialPage: Component { Page { objectName: "serverPage" } }
            function openProjects() {
                projectsPage = pageStack.push(Qt.resolvedUrl("ProjectsPage.qml"), {}, PageStackAction.Immediate)
            }
            function forward() { pageStack.navigateForward(PageStackAction.Immediate) }
            function next() { return pageStack.nextPage(projectsPage) }
        }
    )", base);
    QScopedPointer<QObject> window(component.create());
    QVERIFY2(window, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "openProjects"));
    QObject *page = window->property("projectsPage").value<QObject *>();
    QVERIFY(page);
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), page);
    QVERIFY(page->property("navigationError").toString().isEmpty());
    QVERIFY(!page->property("forwardNavigation").toBool());
    QVERIFY(QMetaObject::invokeMethod(window.data(), "forward"));
    QCOMPARE(window->property("activePage").value<QObject *>(), page);
    const QVariant project = QVariantMap{{QStringLiteral("id"), QStringLiteral("one")},
                                          {QStringLiteral("name"), QStringLiteral("Active")}};
    QVERIFY(QMetaObject::invokeMethod(page, "rememberProject", Q_ARG(QVariant, project)));
    QCOMPARE(drafts.loadLastProject(client.server()), QStringLiteral("one"));
    QVERIFY(QMetaObject::invokeMethod(page, "attachLastProject"));
    QTRY_VERIFY(page->property("forwardNavigation").toBool());
    QVariant next;
    QVERIFY(QMetaObject::invokeMethod(window.data(), "next", Q_RETURN_ARG(QVariant, next)));
    QObject *editor = next.value<QObject *>();
    QVERIFY(editor);
    QCOMPARE(editor->property("projectId").toString(), QStringLiteral("one"));
    QVERIFY(QMetaObject::invokeMethod(window.data(), "forward"));
    QTRY_COMPARE(window->property("activePage").value<QObject *>(), editor);
}

QTEST_MAIN(AutoLoginTests)
#include "autologin_tests.moc"
