#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QVariantList>
#include <QPointer>
#include <QScopedPointer>
#include <QTemporaryFile>
#include "synctex.h"

class QNetworkReply;
class RealtimeSocket;
class QJsonObject;
class ProjectCollaboration;

class OverleafClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString server READ server NOTIFY serverChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY accountChanged)
    Q_PROPERTY(QString rememberedServer READ rememberedServer NOTIFY rememberedServerChanged)
    Q_PROPERTY(QStringList rememberedServers READ rememberedServers NOTIFY rememberedServersChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(QObject *coverEditor READ coverEditor NOTIFY coverEditorChanged)
    Q_PROPERTY(bool coverShowsProjects READ coverShowsProjects WRITE setCoverShowsProjects NOTIFY coverShowsProjectsChanged)
    Q_PROPERTY(bool archiveBusy READ archiveBusy NOTIFY archiveBusyChanged)
    Q_PROPERTY(QString archiveError READ archiveError NOTIFY archiveStatusChanged)
    Q_PROPERTY(QString archivePath READ archivePath NOTIFY archiveStatusChanged)
    Q_PROPERTY(QString archiveProject READ archiveProject NOTIFY archiveStatusChanged)
    Q_PROPERTY(QString pdfPath READ pdfPath NOTIFY pdfPathChanged)
    Q_PROPERTY(QString pdfError READ pdfError NOTIFY pdfErrorChanged)
    Q_PROPERTY(bool pdfBusy READ pdfBusy NOTIFY pdfBusyChanged)
    Q_PROPERTY(QVariantList projectFiles READ projectFiles NOTIFY projectFilesChanged)
    Q_PROPERTY(QString filesError READ filesError NOTIFY filesErrorChanged)
    Q_PROPERTY(bool filesBusy READ filesBusy NOTIFY filesBusyChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY sourceTextChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceTextChanged)
    Q_PROPERTY(QString sourceError READ sourceError NOTIFY sourceErrorChanged)
    Q_PROPERTY(bool sourceBusy READ sourceBusy NOTIFY sourceBusyChanged)
    Q_PROPERTY(bool pdfOnLeft READ pdfOnLeft WRITE setPdfOnLeft NOTIFY pdfOnLeftChanged)
    Q_PROPERTY(int editorFontScale READ editorFontScale WRITE setEditorFontScale NOTIFY editorFontScaleChanged)
    Q_PROPERTY(QString sciHubAddress READ sciHubAddress NOTIFY sciHubAddressChanged)
    Q_PROPERTY(int completionHintCount READ completionHintCount NOTIFY completionHintCountChanged)
    Q_PROPERTY(QObject *collaboration READ collaboration CONSTANT)
    Q_PROPERTY(bool projectCreationBusy READ projectCreationBusy NOTIFY projectCreationChanged)
    Q_PROPERTY(QString projectCreationError READ projectCreationError NOTIFY projectCreationChanged)
    Q_PROPERTY(QString projectActionError READ projectActionError NOTIFY projectActionChanged)
    Q_PROPERTY(bool commentsAvailable READ commentsAvailable NOTIFY commentsAvailableChanged)
    Q_PROPERTY(bool projectsCached READ projectsCached NOTIFY projectsCachedChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)
    Q_PROPERTY(QVariantList cachedAccounts READ cachedAccounts NOTIFY cachedAccountsChanged)
    Q_PROPERTY(bool backgroundCookiePending READ backgroundCookiePending NOTIFY backgroundCookiePendingChanged)
    Q_PROPERTY(bool interactiveCookiePending READ interactiveCookiePending NOTIFY interactiveCookiePendingChanged)
    Q_PROPERTY(bool autoRestoreServer READ autoRestoreServer NOTIFY rememberedServerChanged)
    Q_PROPERTY(bool serverRemovalPending READ serverRemovalPending NOTIFY serverRemovalPendingChanged)
public:
    explicit OverleafClient(QObject *parent = nullptr);
    QString server() const;
    QString userId() const;
    QString rememberedServer() const;
    QStringList rememberedServers() const;
    QString error() const;
    bool busy() const;
    bool authenticated() const;
    QVariantList projects() const;
    QObject *coverEditor() const;
    bool coverShowsProjects() const;
    void setCoverShowsProjects(bool show);
    bool archiveBusy() const;
    QString archiveError() const;
    QString archivePath() const;
    QString archiveProject() const;
    QString pdfPath() const;
    QString pdfError() const;
    bool pdfBusy() const;
    QVariantList projectFiles() const;
    QString filesError() const;
    bool filesBusy() const;
    QString sourceText() const;
    QString sourcePath() const;
    QString sourceError() const;
    bool sourceBusy() const;
    bool pdfOnLeft() const;
    void setPdfOnLeft(bool enabled);
    int editorFontScale() const;
    void setEditorFontScale(int percent);
    QString sciHubAddress() const;
    Q_INVOKABLE bool setSciHubAddress(const QString &address);
    Q_INVOKABLE QUrl sciHubSearchUrl(const QString &query) const;
    int completionHintCount() const;
    Q_INVOKABLE void recordCompletionHintShown();
    QObject *collaboration() const;
    bool projectCreationBusy() const;
    QString projectCreationError() const;
    Q_INVOKABLE void createProject(const QString &name);
    QString projectActionError() const;
    Q_INVOKABLE void changeProjectTrash(const QString &projectId, const QString &action);
    bool commentsAvailable() const;
    Q_INVOKABLE void probeProjectComments(const QString &projectId);
    bool projectsCached() const;
    bool offline() const;
    QVariantList cachedAccounts() const;
    bool backgroundCookiePending() const;
    Q_INVOKABLE void setBackgroundCookiePending(bool pending);
    bool interactiveCookiePending() const;
    Q_INVOKABLE void setInteractiveCookiePending(bool pending);
    Q_INVOKABLE void reportConnectionError(const QString &message);
    Q_INVOKABLE bool openOfflineAccount(const QString &server, const QString &userId);
    Q_INVOKABLE void resumeSavedSession();
    bool autoRestoreServer() const;
    bool serverRemovalPending() const;
    Q_INVOKABLE void setServerRemovalPending(bool pending);
    Q_INVOKABLE bool removeCachedAccount(const QString &server, const QString &userId);
    Q_INVOKABLE bool removeSavedServer(const QString &server);
    Q_INVOKABLE void requestServerRemoval(const QString &server);
    Q_INVOKABLE QString cookieRemovalUrl(const QString &server, const QString &domain, const QString &path) const;
    Q_INVOKABLE void reportAccountRemovalError(const QString &message);

    Q_INVOKABLE bool setServer(const QString &address);
    Q_INVOKABLE bool isProjectListUrl(const QUrl &url) const;
    Q_INVOKABLE void finishBrowserLogin(const QVariantList &cookies);
    Q_INVOKABLE void refreshProjects();
    Q_INVOKABLE void compileProjectPdf(const QString &projectId, const QString &rootPath);
    Q_INVOKABLE void closeProjectPdf();
    Q_INVOKABLE void restoreProjectPdf(const QString &projectId);
    Q_INVOKABLE QUrl cachedPdfForExternalOpen();
    Q_INVOKABLE void setCoverEditor(QObject *editor);
    Q_INVOKABLE void downloadProjectArchive(const QString &projectId);
    Q_INVOKABLE void syncSourceToPdf(const QString &projectId, const QString &path, int line, int column);
    Q_INVOKABLE void syncPdfToSource(const QString &projectId, int page, double h, double v);
    Q_INVOKABLE void loadProjectFiles(const QString &projectId);
    Q_INVOKABLE void closeProjectFiles();
    Q_INVOKABLE void loadProjectFileTree(const QString &projectId);
    Q_INVOKABLE void renameProjectEntity(const QString &projectId, const QString &path,
                                         const QString &type, const QString &name);
    Q_INVOKABLE void deleteProjectEntity(const QString &projectId, const QString &path,
                                         const QString &type);
    Q_INVOKABLE void uploadProjectFile(const QString &projectId, const QString &folder,
                                       const QUrl &localUrl);
    Q_INVOKABLE void overwriteProjectFile(const QString &projectId, const QString &folder,
                                          const QUrl &localUrl, const QString &id, const QString &type);
    Q_INVOKABLE void openProjectSource(const QString &projectId, const QString &path);
    Q_INVOKABLE void openMainProjectSource(const QString &projectId);
    Q_INVOKABLE void openProjectFileExternally(const QString &projectId, const QString &path);
    Q_INVOKABLE void saveProjectSource(const QString &projectId, const QString &path,
                                       const QString &base, bool baseKnown, const QString &text);
    Q_INVOKABLE void closeProjectSource();

signals:
    void serverChanged();
    void accountChanged();
    void serverRemovalRequested(const QString &server);
    void serverRemovalPendingChanged();
    void rememberedServerChanged();
    void rememberedServersChanged();
    void errorChanged();
    void busyChanged();
    void authenticatedChanged();
    void projectsChanged();
    void coverEditorChanged();
    void coverShowsProjectsChanged();
    void archiveBusyChanged();
    void archiveStatusChanged();
    void archiveDownloaded(const QString &path);
    void pdfPathChanged();
    void pdfErrorChanged();
    void pdfBusyChanged();
    void projectFilesChanged();
    void filesErrorChanged();
    void filesBusyChanged();
    void sourceTextChanged();
    void sourceErrorChanged();
    void sourceBusyChanged();
    void sourceSaved(const QString &path, const QString &text);
    void projectFileOperationFinished(const QString &action, const QString &path,
                                      const QString &type, const QString &newPath);
    void projectFileOverwriteRequested(const QString &path, const QString &type, const QString &entityId);
    void pdfOnLeftChanged();
    void editorFontScaleChanged();
    void sciHubAddressChanged();
    void completionHintCountChanged();
    void projectCreationChanged();
    void projectActionChanged();
    void projectTrashChanged(const QString &projectId, const QString &action);
    void commentsAvailableChanged();
    void projectsCachedChanged();
    void offlineChanged();
    void cachedAccountsChanged();
    void reconnectRequested();
    void reconnected();
    void backgroundCookiePendingChanged();
    void interactiveCookiePendingChanged();
    void syncPdfPositions(const QVariantList &positions);
    void syncSourcePositions(const QVariantList &positions);
    void syncError(const QString &message);
    void externalFileReady(const QUrl &url);

private:
    void setError(const QString &message);
    void setBusy(bool busy);
    QNetworkRequest request(const QString &path) const;
    bool checkReply(QNetworkReply *reply);
    void loadProjects(bool reconnecting = false);
    void writeProjectCache();
    QString accountCachePath(const QString &server, const QString &userId) const;
    QString projectResourcePath(const QString &projectId, const QString &kind, const QString &path = QString()) const;
    bool writeCache(const QString &path, const QByteArray &data);
    bool copyToCache(const QString &source, const QString &destination);
    QString sessionFilePath() const;
    bool saveSession(QString *error) const;
    bool purgeAccountCache(const QString &server, const QString &userId);
    void rememberCachedAccount();
    QString projectPdfCachePath(const QString &projectId) const;
    void rememberConnectedServer();
    void setPdfError(const QString &message);
    void setPdfBusy(bool busy);
    void downloadProjectPdf(const QByteArray &response, quint64 generation,
                            const QString &projectId, const QString &editorId, const QString &rootPath);
    void syncRequest(const QUrl &url, bool toPdf);
    void sourceFailed(const QString &message);
    void startProjectSource(const QString &projectId, const QString &path, bool saving,
                            const QString &base, bool baseKnown, const QString &text,
                            bool openingExternal = false, const QString &fileAction = QString(),
                            const QString &fileType = QString(), const QString &fileName = QString(),
                            const QUrl &localUrl = QUrl(), bool openingMain = false,
                            const QString &overwriteId = QString(), const QString &overwriteType = QString());
    void runFileAction(const QJsonObject &project, const QString &projectId,
                       const QString &path, quint64 generation);
    void downloadExternalFile(const QString &projectId, const QString &fileId, const QString &path,
                              quint64 generation);
    void completeSourceSave();

    QNetworkAccessManager m_network;
    ProjectCollaboration *m_collaboration = nullptr;
    QPointer<QNetworkReply> m_createProjectReply;
    QString m_projectCreationError;
    QString m_projectActionError;
    bool m_commentsAvailable = false;
    quint64 m_commentsGeneration = 0;
    QPointer<QNetworkReply> m_commentsReply;
    QString m_projectCachePath;
    QString m_userId;
    QString m_userLabel;
    QString m_sourceProject;
    bool m_offline = false;
    bool m_backgroundCookiePending = false;
    bool m_interactiveCookiePending = false;
    bool m_serverRemovalPending = false;
    bool m_projectsCached = false;
    QUrl m_server;
    QString m_rememberedServer;
    QStringList m_rememberedServers;
    QByteArray m_cookieHeader;
    QVariantList m_pendingSessionCookies;
    QByteArray m_csrf;
    QVariantList m_projects;
    QPointer<QObject> m_coverEditor;
    bool m_coverShowsProjects = true;
    QPointer<QNetworkReply> m_archiveReply;
    QScopedPointer<QTemporaryFile> m_archiveFile;
    QString m_archiveError;
    QString m_archivePath;
    QString m_archiveProject;
    bool m_archiveBusy = false;
    QString m_error;
    bool m_busy = false;
    bool m_authenticated = false;
    QString m_pdfPath;
    QString m_pdfError;
    bool m_pdfBusy = false;
    quint64 m_pdfGeneration = 0;
    QPointer<QNetworkReply> m_pdfReply;
    QScopedPointer<QTemporaryFile> m_pdfFile;
    SyncTeXBuild m_syncBuild;
    QString m_syncProject;
    QString m_syncRoot;
    QPointer<QNetworkReply> m_syncReply;
    quint64 m_syncGeneration = 0;
    QVariantList m_projectFiles;
    QString m_filesProject;
    QString m_filesError;
    bool m_filesBusy = false;
    quint64 m_filesGeneration = 0;
    QPointer<QNetworkReply> m_filesReply;
    QString m_sourceText;
    QString m_sourcePath;
    QString m_sourceError;
    bool m_sourceBusy = false;
    bool m_pdfOnLeft = false;
    int m_editorFontScale = 100;
    QString m_sciHubAddress;
    int m_completionHintCount = 0;
    quint64 m_sourceGeneration = 0;
    QPointer<QNetworkReply> m_sourceReply;
    QPointer<RealtimeSocket> m_sourceSocket;
    bool m_savingSource = false;
    bool m_openingExternal = false;
    bool m_openingMainSource = false;
    QString m_fileAction;
    QString m_fileType;
    QString m_fileName;
    QUrl m_fileLocalUrl;
    QString m_overwriteId;
    QString m_overwriteType;
    QScopedPointer<QTemporaryFile> m_externalFile;
    bool m_saveBaseKnown = false;
    bool m_saveSubmitted = false;
    bool m_saveConcurrent = false;
    QString m_savePath;
    QString m_saveBase;
    QString m_saveText;
    QString m_saveDocId;
    qint64 m_saveVersion = 0;
};
