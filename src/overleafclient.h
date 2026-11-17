#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QVariantList>
#include <QPointer>
#include <QScopedPointer>
#include <QTemporaryFile>

class QNetworkReply;
class RealtimeSocket;

class OverleafClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString server READ server NOTIFY serverChanged)
    Q_PROPERTY(QString rememberedServer READ rememberedServer NOTIFY rememberedServerChanged)
    Q_PROPERTY(QStringList rememberedServers READ rememberedServers NOTIFY rememberedServersChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
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
public:
    explicit OverleafClient(QObject *parent = nullptr);
    QString server() const;
    QString rememberedServer() const;
    QStringList rememberedServers() const;
    QString error() const;
    bool busy() const;
    bool authenticated() const;
    QVariantList projects() const;
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

    Q_INVOKABLE bool setServer(const QString &address);
    Q_INVOKABLE bool isProjectListUrl(const QUrl &url) const;
    Q_INVOKABLE void finishBrowserLogin(const QVariantList &cookies);
    Q_INVOKABLE void refreshProjects();
    Q_INVOKABLE void compileProjectPdf(const QString &projectId, const QString &rootPath);
    Q_INVOKABLE void closeProjectPdf();
    Q_INVOKABLE void loadProjectFiles(const QString &projectId);
    Q_INVOKABLE void closeProjectFiles();
    Q_INVOKABLE void openProjectSource(const QString &projectId, const QString &path);
    Q_INVOKABLE void saveProjectSource(const QString &projectId, const QString &path,
                                       const QString &base, bool baseKnown, const QString &text);
    Q_INVOKABLE void closeProjectSource();

signals:
    void serverChanged();
    void rememberedServerChanged();
    void rememberedServersChanged();
    void errorChanged();
    void busyChanged();
    void authenticatedChanged();
    void projectsChanged();
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
    void pdfOnLeftChanged();
    void editorFontScaleChanged();

private:
    void setError(const QString &message);
    void setBusy(bool busy);
    QNetworkRequest request(const QString &path) const;
    bool checkReply(QNetworkReply *reply);
    void loadProjects();
    void setPdfError(const QString &message);
    void setPdfBusy(bool busy);
    void downloadProjectPdf(const QByteArray &response, quint64 generation);
    void sourceFailed(const QString &message);
    void startProjectSource(const QString &projectId, const QString &path, bool saving,
                            const QString &base, bool baseKnown, const QString &text);
    void completeSourceSave();

    QNetworkAccessManager m_network;
    QUrl m_server;
    QString m_rememberedServer;
    QStringList m_rememberedServers;
    QByteArray m_cookieHeader;
    QByteArray m_csrf;
    QVariantList m_projects;
    QString m_error;
    bool m_busy = false;
    bool m_authenticated = false;
    QString m_pdfPath;
    QString m_pdfError;
    bool m_pdfBusy = false;
    quint64 m_pdfGeneration = 0;
    QPointer<QNetworkReply> m_pdfReply;
    QScopedPointer<QTemporaryFile> m_pdfFile;
    QVariantList m_projectFiles;
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
    quint64 m_sourceGeneration = 0;
    QPointer<QNetworkReply> m_sourceReply;
    QPointer<RealtimeSocket> m_sourceSocket;
    bool m_savingSource = false;
    bool m_saveBaseKnown = false;
    bool m_saveSubmitted = false;
    bool m_saveConcurrent = false;
    QString m_savePath;
    QString m_saveBase;
    QString m_saveText;
    QString m_saveDocId;
    qint64 m_saveVersion = 0;
};
