#include "overleafclient.h"
#include "overleafhtml.h"
#include "projectlist.h"
#include "compileoutput.h"
#include "socketprotocol.h"
#include "realtimesocket.h"
#include "projectcollaboration.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUuid>
#include <QtMath>
#include <QNetworkCookieJar>
#include <QNetworkRequest>
#include <QVariantMap>
#include <QTimer>
#include <QDateTime>
#include <QSettings>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QHttpMultiPart>
#include <QFile>
#include <QSet>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QTextDocumentFragment>
#include <QDebug>

namespace {
bool validEntityName(const QString &name)
{
    return !name.isEmpty() && name.size() <= 150 && name != QStringLiteral(".")
            && name != QStringLiteral("..") && !name.contains(QLatin1Char('/'))
            && !name.contains(QLatin1Char('\\')) && !name.contains(QLatin1Char('"'))
            && !name.contains(QChar::Null) && !name.contains(QLatin1Char('\r'))
            && !name.contains(QLatin1Char('\n'));
}
}

OverleafClient::OverleafClient(QObject *parent) : QObject(parent)
{
    m_collaboration = new ProjectCollaboration(&m_network, &m_server, &m_cookieHeader, this);
    QSettings settings;
    m_pdfOnLeft = settings.value(QStringLiteral("pdfOnLeft"), false).toBool();
    m_editorFontScale = qBound(70, settings.value(QStringLiteral("editorFontScale"), 100).toInt(), 160);
    m_completionHintCount = qBound(0, settings.value(QStringLiteral("completionHintCount"), 0).toInt(), 5);
    m_rememberedServers = settings.value(QStringLiteral("servers")).toStringList();
    m_rememberedServer = settings.value(QStringLiteral("lastServer")).toString();
    if (!m_rememberedServer.isEmpty() && !m_rememberedServers.contains(m_rememberedServer))
        m_rememberedServers.prepend(m_rememberedServer);
    if (m_rememberedServer.isEmpty() && !m_rememberedServers.isEmpty())
        m_rememberedServer = m_rememberedServers.first();
}
QString OverleafClient::server() const { return m_server.toString(); }
QString OverleafClient::userId() const { return m_userId; }
QObject *OverleafClient::collaboration() const { return m_collaboration; }
bool OverleafClient::projectCreationBusy() const { return !m_createProjectReply.isNull(); }
QString OverleafClient::projectCreationError() const { return m_projectCreationError; }
QString OverleafClient::projectActionError() const { return m_projectActionError; }
bool OverleafClient::commentsAvailable() const { return m_commentsAvailable; }
bool OverleafClient::projectsCached() const { return m_projectsCached; }
bool OverleafClient::offline() const { return m_offline; }
bool OverleafClient::backgroundCookiePending() const { return m_backgroundCookiePending; }
bool OverleafClient::interactiveCookiePending() const { return m_interactiveCookiePending; }
void OverleafClient::setInteractiveCookiePending(bool pending)
{
    if (m_interactiveCookiePending == pending)
        return;
    m_interactiveCookiePending = pending;
    emit interactiveCookiePendingChanged();
}
void OverleafClient::setBackgroundCookiePending(bool pending)
{
    if (m_backgroundCookiePending == pending)
        return;
    m_backgroundCookiePending = pending;
    emit backgroundCookiePendingChanged();
}
void OverleafClient::reportConnectionError(const QString &message)
{
    if (m_offline)
        setError(message);
}
QVariantList OverleafClient::cachedAccounts() const
{
    return QSettings().value(QStringLiteral("cachedAccounts")).toList();
}

bool OverleafClient::autoRestoreServer() const
{
    return QSettings().value(QStringLiteral("autoRestoreServer"), true).toBool();
}

bool OverleafClient::serverRemovalPending() const { return m_serverRemovalPending; }

void OverleafClient::setServerRemovalPending(bool pending)
{
    if (m_serverRemovalPending == pending)
        return;
    m_serverRemovalPending = pending;
    emit serverRemovalPendingChanged();
}

void OverleafClient::reportAccountRemovalError(const QString &message)
{
    setError(message);
}

void OverleafClient::requestServerRemoval(const QString &address)
{
    if (!m_rememberedServers.contains(address)) {
        setError(tr("This saved server no longer exists."));
        return;
    }
    if (m_busy || m_interactiveCookiePending || m_serverRemovalPending) {
        setError(tr("Wait for the current operation before removing the account."));
        return;
    }
    setError(QString());
    setServerRemovalPending(true);
    emit serverRemovalRequested(address);
}

QString OverleafClient::cookieRemovalUrl(const QString &address, const QString &domain,
                                         const QString &path) const
{
    QUrl url(address, QUrl::StrictMode);
    const QString host = url.host().toLower();
    const QString cookieDomain = domain.toLower();
    const bool domainCookie = cookieDomain.startsWith(QLatin1Char('.'));
    const QString bareDomain = domainCookie ? cookieDomain.mid(1) : cookieDomain;
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || host.isEmpty()
            || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
            || (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
            || bareDomain.isEmpty()
            || (host != bareDomain && (!domainCookie || !host.endsWith(QLatin1Char('.') + bareDomain)))
            || !path.startsWith(QLatin1Char('/')) || path.contains(QChar::Null)
            || path.contains(QLatin1Char('\r')) || path.contains(QLatin1Char('\n')))
        return QString();
    url.setPath(path, QUrl::TolerantMode);
    return url.toString(QUrl::FullyEncoded);
}

bool OverleafClient::purgeAccountCache(const QString &address, const QString &userId)
{
    const QString cachePath = accountCachePath(address, userId);
    if (cachePath.isEmpty()) {
        setError(tr("Could not locate the account cache."));
        return false;
    }
    if (server() == address && m_userId == userId) {
        if (m_busy || m_sourceBusy || m_filesBusy || m_pdfBusy || m_archiveBusy || m_createProjectReply) {
            setError(tr("Wait for the current operation before removing the account."));
            return false;
        }
        if (!setServer(address))
            return false;
    }

    QStringList paths;
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString sessionPath = QDir(root).filePath(QStringLiteral("sessions/") + QFileInfo(cachePath).fileName());
    const QString legacyRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString legacyPath = legacyRoot.isEmpty() ? QString()
            : QDir(legacyRoot).filePath(QFileInfo(cachePath).fileName());
    const QRegularExpression resource(QStringLiteral("^(tree|binary|source|last|pdf)-[0-9a-f]{64}$"));
    const QStringList bases{cachePath, legacyPath};
    for (const QString &base : bases) {
        if (base.isEmpty())
            continue;
        const QFileInfo baseInfo(base);
        const QFileInfo dirInfo(baseInfo.absolutePath());
        if (dirInfo.isSymLink() || (dirInfo.exists() && !dirInfo.isDir())) {
            setError(tr("Could not safely access the account cache."));
            return false;
        }
        paths.append(base);
        const QString prefix = baseInfo.fileName() + QLatin1Char('.');
        const QFileInfoList entries = QDir(dirInfo.absoluteFilePath()).entryInfoList(
                    QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().startsWith(prefix)
                    && resource.match(entry.fileName().mid(prefix.size())).hasMatch())
                paths.append(entry.absoluteFilePath());
        }
    }
    const QFileInfo sessionDirectory(QFileInfo(sessionPath).absolutePath());
    if (sessionDirectory.isSymLink() || (sessionDirectory.exists() && !sessionDirectory.isDir())) {
        setError(tr("Could not safely access the account cache."));
        return false;
    }
    paths.prepend(sessionPath);
    const QString draftsRoot = QDir(root).filePath(QStringLiteral("drafts"));
    const QString draftsPath = QDir(draftsRoot).filePath(QFileInfo(cachePath).fileName());
    const QFileInfo draftRootInfo(draftsRoot);
    const QFileInfo draftInfo(draftsPath);
    if (draftRootInfo.isSymLink() || draftInfo.isSymLink()
            || (draftInfo.exists() && !draftInfo.isDir())) {
        setError(tr("Could not safely access the account cache."));
        return false;
    }
    if (draftInfo.isDir()) {
        const QFileInfoList entries = QDir(draftsPath).entryInfoList(
                    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (entry.isDir() && !entry.isSymLink()) {
                setError(tr("Could not safely access the account cache."));
                return false;
            }
            paths.append(entry.absoluteFilePath());
        }
    }
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if ((info.exists() || info.isSymLink()) && !QFile::remove(path)) {
            setError(tr("Could not remove the account's saved files: %1").arg(path));
            return false;
        }
    }
    if (draftInfo.isDir() && !QDir(draftsRoot).rmdir(draftInfo.fileName())) {
        setError(tr("Could not remove the account's saved files: %1").arg(draftsPath));
        return false;
    }
    const auto temporaryFiles = findChildren<QTemporaryFile *>(QString(), Qt::FindDirectChildrenOnly);
    for (QTemporaryFile *file : temporaryFiles) {
        if (file->property("accountCachePath").toString() != cachePath)
            continue;
        if (QFileInfo::exists(file->fileName()) && !file->remove()) {
            setError(tr("Could not remove the account's saved files: %1").arg(file->fileName()));
            return false;
        }
        delete file;
    }
    return true;
}

bool OverleafClient::removeCachedAccount(const QString &address, const QString &userId)
{
    QVariantList accounts = cachedAccounts();
    bool found = false;
    for (const QVariant &entry : accounts) {
        const QVariantMap account = entry.toMap();
        if (account.value(QStringLiteral("server")).toString() == address
                && account.value(QStringLiteral("userId")).toString() == userId) {
            found = true;
            break;
        }
    }
    if (!found) {
        setError(tr("This saved account no longer exists."));
        return false;
    }
    if (!purgeAccountCache(address, userId))
        return false;
    for (int i = accounts.size() - 1; i >= 0; --i) {
        const QVariantMap account = accounts.at(i).toMap();
        if (account.value(QStringLiteral("server")).toString() == address
                && account.value(QStringLiteral("userId")).toString() == userId)
            accounts.removeAt(i);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("cachedAccounts"), accounts);
    if (accounts.isEmpty())
        settings.setValue(QStringLiteral("autoRestoreServer"), false);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(tr("Could not update the saved account list."));
        return false;
    }
    setError(QString());
    emit cachedAccountsChanged();
    emit rememberedServerChanged();
    return true;
}

bool OverleafClient::removeSavedServer(const QString &address)
{
    if (!m_rememberedServers.contains(address)) {
        setError(tr("This saved server no longer exists."));
        return false;
    }
    QVariantList accounts = cachedAccounts();
    for (const QVariant &entry : accounts) {
        const QVariantMap account = entry.toMap();
        if (account.value(QStringLiteral("server")).toString() == address
                && !purgeAccountCache(address, account.value(QStringLiteral("userId")).toString()))
            return false;
    }
    if (server() == address && m_authenticated) {
        if (m_busy || m_sourceBusy || m_filesBusy || m_pdfBusy || m_archiveBusy || m_createProjectReply) {
            setError(tr("Wait for the current operation before removing the account."));
            return false;
        }
        if (!setServer(address))
            return false;
    }
    for (int i = accounts.size() - 1; i >= 0; --i) {
        if (accounts.at(i).toMap().value(QStringLiteral("server")).toString() == address)
            accounts.removeAt(i);
    }
    QStringList servers = m_rememberedServers;
    servers.removeAll(address);
    const QString lastServer = m_rememberedServer == address
            ? (servers.isEmpty() ? QString() : servers.first()) : m_rememberedServer;
    QSettings settings;
    settings.setValue(QStringLiteral("cachedAccounts"), accounts);
    settings.setValue(QStringLiteral("servers"), servers);
    settings.setValue(QStringLiteral("lastServer"), lastServer);
    if (accounts.isEmpty())
        settings.setValue(QStringLiteral("autoRestoreServer"), false);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(tr("Could not update the saved account list."));
        return false;
    }
    m_rememberedServers = servers;
    m_rememberedServer = lastServer;
    setError(QString());
    emit cachedAccountsChanged();
    emit rememberedServerChanged();
    emit rememberedServersChanged();
    return true;
}

QString OverleafClient::accountCachePath(const QString &server, const QString &userId) const
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty() || server.isEmpty() || userId.isEmpty())
        return QString();
    const QByteArray key = server.toUtf8() + '\0' + userId.toUtf8();
    return QDir(directory).filePath(QStringLiteral("offline/projects-")
            + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
            + QStringLiteral(".json"));
}

QString OverleafClient::projectResourcePath(const QString &projectId, const QString &kind,
                                            const QString &path) const
{
    if (m_projectCachePath.isEmpty() || projectId.isEmpty())
        return QString();
    const QByteArray key = projectId.toUtf8() + '\0' + path.toUtf8();
    return m_projectCachePath + QLatin1Char('.') + kind + QLatin1Char('-')
            + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex());
}

bool OverleafClient::writeCache(const QString &path, const QByteArray &data)
{
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "Cannot prepare offline cache";
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
            || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || file.write(data) != data.size() || !file.commit()) {
        qWarning() << "Cannot save offline cache:" << file.errorString();
        return false;
    }
    return true;
}

bool OverleafClient::copyToCache(const QString &source, const QString &destination)
{
    QFile input(source);
    if (destination.isEmpty() || !input.open(QIODevice::ReadOnly)
            || !QDir().mkpath(QFileInfo(destination).absolutePath())) {
        qWarning() << "Cannot prepare cached file:" << input.errorString();
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)
            || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        qWarning() << "Cannot create cached file:" << output.errorString();
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray data = input.read(65536);
        if (input.error() != QFileDevice::NoError || output.write(data) != data.size()) {
            qWarning() << "Cannot copy cached file:" << input.errorString() << output.errorString();
            output.cancelWriting();
            return false;
        }
    }
    if (!output.commit()) {
        qWarning() << "Cannot finish cached file:" << output.errorString();
        return false;
    }
    return true;
}

QString OverleafClient::sessionFilePath() const
{
    if (m_projectCachePath.isEmpty())
        return QString();
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        return QString();
    return QDir(root).filePath(QStringLiteral("sessions/")
                               + QFileInfo(m_projectCachePath).fileName());
}

bool OverleafClient::saveSession(QString *error) const
{
    const QString path = sessionFilePath();
    const QString directory = QFileInfo(path).absolutePath();
    const QFile::Permissions unsafe = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    if (path.isEmpty() || !QDir().mkpath(directory) || QFileInfo(directory).isSymLink()
            || !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)
            || (QFileInfo(directory).permissions() & unsafe)) {
        *error = tr("Could not protect the saved sign-in directory.");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
            || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
        *error = tr("Could not create the saved sign-in file.");
        return false;
    }
    const QJsonObject data{{QStringLiteral("server"), server()},
                           {QStringLiteral("userId"), m_userId},
                           {QStringLiteral("cookies"), QJsonArray::fromVariantList(m_pendingSessionCookies)}};
    const QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact);
    if (file.write(json) != json.size() || !file.commit()) {
        *error = tr("Could not save the sign-in session.");
        return false;
    }
    return true;
}

void OverleafClient::resumeSavedSession()
{
    if (!m_offline || m_busy)
        return;
    const QString path = sessionFilePath();
    const QFileInfo directory(QFileInfo(path).absolutePath());
    const QFileInfo info(path);
    const QFile::Permissions unsafe = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    if (path.isEmpty() || directory.isSymLink() || !directory.isDir()
            || (directory.permissions() & unsafe) || info.isSymLink() || !info.isFile()
            || (info.permissions() & unsafe)) {
        setError(tr("Saved sign-in is missing or not private. Sign in again to enable reconnection."));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(tr("Could not read the saved sign-in. Cached projects remain available."));
        return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject data = doc.object();
    if (error.error != QJsonParseError::NoError || !doc.isObject()
            || data.value(QStringLiteral("server")).toString() != server()
            || data.value(QStringLiteral("userId")).toString() != m_userId
            || !data.value(QStringLiteral("cookies")).isArray()) {
        setError(tr("The saved sign-in is invalid. Sign in again to enable reconnection."));
        return;
    }
    finishBrowserLogin(data.value(QStringLiteral("cookies")).toArray().toVariantList());
}

void OverleafClient::rememberCachedAccount()
{
    if (m_userId.isEmpty() || m_projectCachePath.isEmpty() || !QFile::exists(m_projectCachePath))
        return;
    QVariantList accounts = cachedAccounts();
    for (int i = accounts.size() - 1; i >= 0; --i) {
        const QVariantMap item = accounts.at(i).toMap();
        if (item.value(QStringLiteral("server")).toString() == server()
                && item.value(QStringLiteral("userId")).toString() == m_userId)
            accounts.removeAt(i);
    }
    accounts.prepend(QVariantMap{{QStringLiteral("server"), server()},
                                 {QStringLiteral("userId"), m_userId},
                                 {QStringLiteral("label"), m_userLabel}});
    QSettings settings;
    settings.setValue(QStringLiteral("cachedAccounts"), accounts);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning() << "Cannot save offline account list";
    emit cachedAccountsChanged();
}

bool OverleafClient::openOfflineAccount(const QString &address, const QString &userId)
{
    QVariantMap selected;
    for (const QVariant &entry : cachedAccounts()) {
        const QVariantMap account = entry.toMap();
        if (account.value(QStringLiteral("server")).toString() == address
                && account.value(QStringLiteral("userId")).toString() == userId) {
            selected = account;
            break;
        }
    }
    if (selected.isEmpty() || !setServer(address)) {
        setError(tr("This offline account is not available."));
        return false;
    }
    const QString path = accountCachePath(server(), userId);
    QFile file(path);
    QVariantList projects;
    QString detail;
    if (!file.open(QIODevice::ReadOnly) || !parseProjectList(file.readAll(), &projects, &detail)) {
        setError(tr("Could not read the cached project list."));
        return false;
    }
    m_userId = userId;
    m_userLabel = selected.value(QStringLiteral("label")).toString();
    m_projectCachePath = path;
    emit accountChanged();
    m_projects = projects;
    m_projectsCached = true;
    m_offline = true;
    m_authenticated = true;
    rememberCachedAccount();
    emit offlineChanged();
    emit projectsChanged();
    emit projectsCachedChanged();
    emit authenticatedChanged();
    return true;
}
int OverleafClient::completionHintCount() const { return m_completionHintCount; }
void OverleafClient::recordCompletionHintShown()
{
    if (m_completionHintCount >= 5)
        return;
    ++m_completionHintCount;
    QSettings settings;
    settings.setValue(QStringLiteral("completionHintCount"), m_completionHintCount);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning("Could not save completion hint count");
    emit completionHintCountChanged();
}
QString OverleafClient::rememberedServer() const { return m_rememberedServer; }
QStringList OverleafClient::rememberedServers() const { return m_rememberedServers; }
QString OverleafClient::error() const { return m_error; }
bool OverleafClient::busy() const { return m_busy; }
bool OverleafClient::authenticated() const { return m_authenticated; }
QVariantList OverleafClient::projects() const { return m_projects; }
QObject *OverleafClient::coverEditor() const { return m_coverEditor.data(); }
bool OverleafClient::coverShowsProjects() const { return m_coverShowsProjects; }
void OverleafClient::setCoverShowsProjects(bool show)
{
    if (m_coverShowsProjects == show)
        return;
    m_coverShowsProjects = show;
    emit coverShowsProjectsChanged();
}
bool OverleafClient::archiveBusy() const { return m_archiveBusy; }
QString OverleafClient::archiveError() const { return m_archiveError; }
QString OverleafClient::archivePath() const { return m_archivePath; }
QString OverleafClient::archiveProject() const { return m_archiveProject; }
QString OverleafClient::pdfPath() const { return m_pdfPath; }
QString OverleafClient::pdfError() const { return m_pdfError; }
bool OverleafClient::pdfBusy() const { return m_pdfBusy; }
QVariantList OverleafClient::projectFiles() const { return m_projectFiles; }
QString OverleafClient::filesError() const { return m_filesError; }
bool OverleafClient::filesBusy() const { return m_filesBusy; }
QString OverleafClient::sourceText() const { return m_sourceText; }
QString OverleafClient::sourcePath() const { return m_sourcePath; }
QString OverleafClient::sourceError() const { return m_sourceError; }
bool OverleafClient::sourceBusy() const { return m_sourceBusy; }
bool OverleafClient::pdfOnLeft() const { return m_pdfOnLeft; }
int OverleafClient::editorFontScale() const { return m_editorFontScale; }

void OverleafClient::setCoverEditor(QObject *editor)
{
    if (m_coverEditor == editor)
        return;
    if (m_coverEditor)
        disconnect(m_coverEditor, &QObject::destroyed, this, nullptr);
    m_coverEditor = editor;
    if (editor) {
        connect(editor, &QObject::destroyed, this, [this]() {
            m_coverEditor.clear();
            emit coverEditorChanged();
        });
    }
    emit coverEditorChanged();
}

QUrl OverleafClient::cachedPdfForExternalOpen()
{
    if (m_pdfPath.isEmpty()) {
        setPdfError(tr("There is no cached PDF to open."));
        return QUrl();
    }
    QFile source(m_pdfPath);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!source.open(QIODevice::ReadOnly)) {
        setPdfError(tr("Could not read the cached PDF: %1").arg(source.errorString()));
        return QUrl();
    }
    if (cache.isEmpty() || !QDir().mkpath(cache)) {
        setPdfError(tr("Could not prepare a temporary file."));
        return QUrl();
    }
    QScopedPointer<QTemporaryFile> file(new QTemporaryFile(
        QDir(cache).filePath(QStringLiteral("Abedul-XXXXXX.pdf"))));
    if (!file->open()) {
        setPdfError(tr("Could not create a temporary PDF: %1").arg(file->errorString()));
        return QUrl();
    }
    while (!source.atEnd()) {
        const QByteArray data = source.read(64 * 1024);
        if (data.isEmpty() || file->write(data) != data.size()) {
            setPdfError(tr("Could not copy the cached PDF."));
            return QUrl();
        }
        if (source.error() != QFileDevice::NoError) {
            setPdfError(tr("Could not read the cached PDF: %1").arg(source.errorString()));
            return QUrl();
        }
    }
    if (!file->flush()) {
        setPdfError(tr("Could not write the temporary PDF: %1").arg(file->errorString()));
        return QUrl();
    }
    const QUrl result = QUrl::fromLocalFile(file->fileName());
    file->close();
    file->setProperty("accountCachePath", m_projectCachePath);
    file.take()->setParent(this);
    setPdfError(QString());
    return result;
}

void OverleafClient::downloadProjectArchive(const QString &projectId)
{
    if (m_offline) {
        m_archiveError = tr("This operation is not available offline.");
        emit archiveStatusChanged();
        return;
    }
    if (m_archiveBusy)
        return;
    QString name;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            name = project.value(QStringLiteral("name")).toString();
            break;
        }
    }
    m_archiveError.clear();
    m_archivePath.clear();
    m_archiveProject = projectId;
    if (!m_authenticated || name.isEmpty()) {
        m_archiveError = tr("This project is not available in the current session.");
        emit archiveStatusChanged();
        return;
    }
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    name = name.trimmed().left(100);
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("project");
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads.isEmpty() || !QDir().mkpath(downloads)) {
        m_archiveError = tr("Could not access the Downloads folder.");
        emit archiveStatusChanged();
        return;
    }
    m_archiveFile.reset(new QTemporaryFile(QDir(downloads).filePath(name + QStringLiteral("-XXXXXX.zip"))));
    if (!m_archiveFile->open()) {
        m_archiveError = tr("Could not create the ZIP archive: %1").arg(m_archiveFile->errorString());
        m_archiveFile.reset();
        emit archiveStatusChanged();
        return;
    }
    m_archiveBusy = true;
    emit archiveBusyChanged();
    emit archiveStatusChanged();
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    const QUrl archiveServer = m_server;
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/project/") + id + QStringLiteral("/download/zip")));
    m_archiveReply = reply;
    QTimer::singleShot(120000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        const QByteArray data = reply->readAll();
        if (m_archiveFile && m_archiveFile->write(data) != data.size()) {
            m_archiveError = tr("Could not write the ZIP archive: %1").arg(m_archiveFile->errorString());
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, archiveServer]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString detail = reply->errorString();
        const QByteArray remaining = reply->readAll();
        m_archiveReply.clear();
        reply->deleteLater();
        if (m_server != archiveServer) {
            m_archiveFile.reset();
            m_archiveBusy = false;
            emit archiveBusyChanged();
            return;
        }
        if (!ok && m_archiveError.isEmpty())
            m_archiveError = tr("Could not download the ZIP archive (HTTP %1): %2").arg(status).arg(detail);
        if (m_archiveError.isEmpty() && m_archiveFile
                && (m_archiveFile->write(remaining) != remaining.size() || !m_archiveFile->flush()))
            m_archiveError = tr("Could not write the ZIP archive: %1").arg(m_archiveFile->errorString());
        if (m_archiveError.isEmpty() && m_archiveFile) {
            m_archiveFile->seek(0);
            const QByteArray signature = m_archiveFile->read(4);
            if (signature != QByteArray("PK\003\004", 4) && signature != QByteArray("PK\005\006", 4))
                m_archiveError = tr("The server did not return a ZIP archive.");
        }
        if (m_archiveError.isEmpty() && m_archiveFile) {
            m_archivePath = m_archiveFile->fileName();
            m_archiveFile->setAutoRemove(false);
        }
        m_archiveFile.reset();
        m_archiveBusy = false;
        emit archiveBusyChanged();
        emit archiveStatusChanged();
        if (!m_archivePath.isEmpty())
            emit archiveDownloaded(m_archivePath);
    });
}

void OverleafClient::setEditorFontScale(int percent)
{
    percent = qBound(70, percent, 160);
    if (m_editorFontScale == percent)
        return;
    m_editorFontScale = percent;
    QSettings settings;
    settings.setValue(QStringLiteral("editorFontScale"), percent);
    emit editorFontScaleChanged();
}

void OverleafClient::setPdfOnLeft(bool enabled)
{
    if (m_pdfOnLeft == enabled)
        return;
    m_pdfOnLeft = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("pdfOnLeft"), enabled);
    emit pdfOnLeftChanged();
}

void OverleafClient::closeProjectSource()
{
    ++m_sourceGeneration;
    if (m_sourceReply)
        m_sourceReply->abort();
    m_sourceReply.clear();
    if (m_sourceSocket) {
        m_sourceSocket->abort();
        m_sourceSocket->deleteLater();
        m_sourceSocket.clear();
    }
    m_sourceBusy = false;
    m_sourceError.clear();
    m_sourcePath.clear();
    m_sourceText.clear();
    m_savingSource = false;
    m_openingExternal = false;
    m_openingMainSource = false;
    m_fileAction.clear();
    m_fileType.clear();
    m_fileName.clear();
    m_fileLocalUrl = QUrl();
    m_overwriteId.clear();
    m_overwriteType.clear();
    m_externalFile.reset();
    m_saveSubmitted = false;
    m_saveConcurrent = false;
    m_savePath.clear();
    m_saveBase.clear();
    m_saveText.clear();
    m_saveDocId.clear();
    emit sourceBusyChanged();
    emit sourceErrorChanged();
    emit sourceTextChanged();
}

void OverleafClient::sourceFailed(const QString &message)
{
    const bool uncertain = m_saveSubmitted;
    const bool external = m_openingExternal || !m_fileAction.isEmpty();
    closeProjectSource();
    if (external) {
        m_filesError = message;
        emit filesErrorChanged();
        return;
    }
    m_sourceError = uncertain
            ? message + tr(" The server may have saved the changes; the local draft was kept.")
            : message;
    emit sourceErrorChanged();
}

void OverleafClient::openProjectFileExternally(const QString &projectId, const QString &path)
{
    bool found = false;
    for (const QVariant &entry : m_projectFiles) {
        const QVariantMap file = entry.toMap();
        if (file.value(QStringLiteral("type")).toString() == QStringLiteral("file")
                && file.value(QStringLiteral("path")).toString() == path) {
            found = true;
            break;
        }
    }
    if (projectId != m_filesProject || !found) {
        m_filesError = tr("This file is not available in the current project.");
        emit filesErrorChanged();
        return;
    }
    m_filesError.clear();
    emit filesErrorChanged();
    if (m_offline) {
        QFile input(projectResourcePath(projectId, QStringLiteral("binary"), path));
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString name = QFileInfo(path).fileName();
        if (!input.open(QIODevice::ReadOnly) || cache.isEmpty() || !QDir().mkpath(cache)
                || !validEntityName(name)) {
            m_filesError = tr("This file is not available offline.");
            emit filesErrorChanged();
            return;
        }
        QScopedPointer<QTemporaryFile> copy(new QTemporaryFile(
                QDir(cache).filePath(QStringLiteral("Abedul-XXXXXX-") + name)));
        if (!copy->open()) {
            m_filesError = tr("Could not create a temporary file: %1").arg(copy->errorString());
            emit filesErrorChanged();
            return;
        }
        while (!input.atEnd()) {
            const QByteArray data = input.read(65536);
            if (input.error() != QFileDevice::NoError || copy->write(data) != data.size()) {
                m_filesError = tr("Could not copy the cached file.");
                emit filesErrorChanged();
                return;
            }
        }
        if (!copy->flush()) {
            m_filesError = tr("Could not copy the cached file.");
            emit filesErrorChanged();
            return;
        }
        const QString localPath = copy->fileName();
        copy->close();
        copy->setProperty("accountCachePath", m_projectCachePath);
        copy.take()->setParent(this);
        emit externalFileReady(QUrl::fromLocalFile(localPath));
        return;
    }
    startProjectSource(projectId, path, false, QString(), false, QString(), true);
}

void OverleafClient::openProjectSource(const QString &projectId, const QString &path)
{
    startProjectSource(projectId, path, false, QString(), false, QString());
}

void OverleafClient::openMainProjectSource(const QString &projectId)
{
    startProjectSource(projectId, QStringLiteral("/"), false, QString(), false, QString(),
                       false, QString(), QString(), QString(), QUrl(), true);
}

void OverleafClient::saveProjectSource(const QString &projectId, const QString &path,
                                       const QString &base, bool baseKnown, const QString &text)
{
    startProjectSource(projectId, path, true, base, baseKnown, text);
}

void OverleafClient::completeSourceSave()
{
    const QString path = m_savePath;
    const QString text = m_saveText;
    if (writeCache(projectResourcePath(m_sourceProject, QStringLiteral("source"), path), text.toUtf8()))
        writeCache(projectResourcePath(m_sourceProject, QStringLiteral("last")), path.toUtf8());
    closeProjectSource();
    emit sourceSaved(path, text);
}

void OverleafClient::startProjectSource(const QString &projectId, const QString &path, bool saving,
                                        const QString &base, bool baseKnown, const QString &text,
                                        bool openingExternal, const QString &fileAction,
                                        const QString &fileType, const QString &fileName,
                                        const QUrl &localUrl, bool openingMain,
                                        const QString &overwriteId, const QString &overwriteType)
{
    if (m_sourceBusy)
        return;
    bool found = false;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            found = true;
            break;
        }
    }
    if (!m_authenticated || !found || path.isEmpty()) {
        if (openingExternal || !fileAction.isEmpty()) {
            m_filesError = tr("This project file is not available in the current session.");
            emit filesErrorChanged();
        } else {
            m_sourceError = tr("This project document is not available in the current session.");
            emit sourceErrorChanged();
        }
        return;
    }
    if (m_offline) {
        if (saving || openingExternal || !fileAction.isEmpty()) {
            if (openingExternal || !fileAction.isEmpty()) {
                m_filesError = tr("This operation is not available offline.");
                emit filesErrorChanged();
            } else {
                m_sourceError = tr("This operation is not available offline.");
                emit sourceErrorChanged();
            }
            return;
        }
        QString sourcePath = path;
        if (openingMain) {
            QFile last(projectResourcePath(projectId, QStringLiteral("last")));
            if (last.open(QIODevice::ReadOnly))
                sourcePath = QString::fromUtf8(last.readAll());
            else
                sourcePath.clear();
        }
        QFile file(projectResourcePath(projectId, QStringLiteral("source"), sourcePath));
        if (sourcePath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
            m_sourceError = tr("This document is not available offline.");
            emit sourceErrorChanged();
            return;
        }
        const QByteArray data = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            m_sourceError = tr("Could not read the cached document.");
            emit sourceErrorChanged();
            return;
        }
        closeProjectSource();
        m_sourcePath = sourcePath;
        m_sourceText = QString::fromUtf8(data);
        emit sourceTextChanged();
        return;
    }
    closeProjectSource();
    m_sourceProject = projectId;
    m_openingExternal = openingExternal;
    m_openingMainSource = openingMain;
    m_fileAction = fileAction;
    m_fileType = fileType;
    m_fileName = fileName;
    m_fileLocalUrl = localUrl;
    m_overwriteId = overwriteId;
    m_overwriteType = overwriteType;
    m_savingSource = saving;
    m_savePath = path;
    m_saveBase = base;
    m_saveBaseKnown = baseKnown;
    m_saveText = text;
    m_sourceBusy = true;
    emit sourceBusyChanged();
    const quint64 generation = m_sourceGeneration;
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/project/") + id));
    m_sourceReply = reply;
    QTimer::singleShot(openingExternal || !fileAction.isEmpty() ? 120000 : 30000, this, [this, generation]() {
        if (generation == m_sourceGeneration && m_sourceBusy)
            sourceFailed(m_fileAction.isEmpty() ? tr("Timed out connecting to the project.")
                         : tr("File operation timed out. Refresh to check the server state."));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectId, path]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString html = QString::fromUtf8(reply->readAll());
        const QString networkError = reply->errorString();
        reply->deleteLater();
        if (generation != m_sourceGeneration)
            return;
        m_sourceReply.clear();
        if (!ok) {
            sourceFailed(tr("Could not open the project (HTTP %1): %2").arg(status).arg(networkError));
            return;
        }
        const QString csrf = extractCsrfToken(html);
        if (!csrf.isEmpty())
            m_csrf = csrf.toUtf8();
        const QString wsValue = extractMetaContent(html, QStringLiteral("ol-wsUrl"));
        QUrl endpoint = m_server.resolved(QUrl(wsValue.isEmpty() ? QStringLiteral("/socket.io") : wsValue));
        if (endpoint.scheme() == QStringLiteral("wss"))
            endpoint.setScheme(QStringLiteral("https"));
        if (!endpoint.isValid() || endpoint.scheme() != QStringLiteral("https")
                || endpoint.host() != m_server.host() || endpoint.port(443) != m_server.port(443)
                || !endpoint.userInfo().isEmpty() || endpoint.hasQuery() || endpoint.hasFragment()) {
            sourceFailed(tr("The project uses an unsupported or cross-origin real-time server."));
            return;
        }
        QString resource = endpoint.path();
        if (resource.isEmpty() || resource == QStringLiteral("/"))
            resource = QStringLiteral("/socket.io");
        while (resource.endsWith(QLatin1Char('/')))
            resource.chop(1);
        endpoint.setPath(resource + QStringLiteral("/1/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("projectId"), projectId);
        query.addQueryItem(QStringLiteral("t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
        endpoint.setQuery(query);
        QNetworkRequest handshake(endpoint);
        handshake.setRawHeader("Cookie", m_cookieHeader);
        handshake.setRawHeader("Origin", m_server.toString().toUtf8());
        if (!m_csrf.isEmpty())
            handshake.setRawHeader("X-CSRF-Token", m_csrf);
        QNetworkReply *handshakeReply = m_network.get(handshake);
        m_sourceReply = handshakeReply;
        connect(handshakeReply, &QNetworkReply::finished, this,
                [this, handshakeReply, generation, endpoint, resource, projectId, path]() {
            const int status = handshakeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = handshakeReply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
            const QString handshake = QString::fromUtf8(handshakeReply->readAll()).trimmed();
            const QList<QNetworkCookie> responseCookies =
                    qvariant_cast<QList<QNetworkCookie>>(handshakeReply->header(QNetworkRequest::SetCookieHeader));
            const QString networkError = handshakeReply->errorString();
            handshakeReply->deleteLater();
            if (generation != m_sourceGeneration)
                return;
            m_sourceReply.clear();
            const QStringList parts = handshake.split(QLatin1Char(':'));
            if (!ok) {
                sourceFailed(tr("Real-time handshake failed (HTTP %1): %2").arg(status).arg(networkError));
                return;
            }
            if (parts.size() != 4
                    || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(parts.at(0)).hasMatch()
                    || !parts.at(3).split(QLatin1Char(',')).contains(QStringLiteral("websocket"))) {
                sourceFailed(tr("Unsupported real-time handshake from the server."));
                return;
            }
            QUrl socketUrl = endpoint;
            socketUrl.setScheme(QStringLiteral("wss"));
            socketUrl.setPath(resource + QStringLiteral("/1/websocket/") + parts.at(0));
            QUrlQuery socketQuery;
            socketQuery.addQueryItem(QStringLiteral("projectId"), projectId);
            socketUrl.setQuery(socketQuery);
            RealtimeSocket *socket = new RealtimeSocket(this);
            m_sourceSocket = socket;
            connect(socket, &RealtimeSocket::failed, this, [this, generation](const QString &error) {
                if (generation == m_sourceGeneration && m_sourceBusy)
                    sourceFailed(tr("Real-time connection failed: %1").arg(error));
            });
            connect(socket, &RealtimeSocket::textMessageReceived, this,
                    [this, socket, generation, path, projectId](const QString &message) {
                if (generation != m_sourceGeneration || !m_sourceBusy)
                    return;
                if (message == QStringLiteral("2::")) {
                    socket->sendText(message);
                    return;
                }
                if (message.startsWith(QStringLiteral("5:::"))) {
                    QJsonParseError jsonError;
                    const QJsonObject event = QJsonDocument::fromJson(message.mid(4).toUtf8(), &jsonError).object();
                    if (jsonError.error != QJsonParseError::NoError) {
                        sourceFailed(tr("Invalid real-time message from server."));
                        return;
                    }
                    const QString eventName = event.value(QStringLiteral("name")).toString();
                    if (eventName == QStringLiteral("connectionRejected")) {
                        sourceFailed(tr("The server rejected the project connection."));
                        return;
                    }
                    if (m_savingSource && eventName == QStringLiteral("otUpdateApplied")) {
                        const QJsonArray updateArgs = event.value(QStringLiteral("args")).toArray();
                        const QJsonObject update = updateArgs.isEmpty() ? QJsonObject() : updateArgs.at(0).toObject();
                        if (!m_saveDocId.isEmpty() && update.value(QStringLiteral("doc")).toString() == m_saveDocId) {
                            if (!m_saveSubmitted) {
                                m_saveConcurrent = true;
                            } else if (update.value(QStringLiteral("v")).isDouble()
                                       && update.value(QStringLiteral("v")).toDouble() == static_cast<double>(m_saveVersion)
                                       && !update.contains(QStringLiteral("op"))) {
                                completeSourceSave();
                            } else {
                                sourceFailed(tr("The document changed on the server while saving."));
                            }
                        }
                        return;
                    }
                    if (m_savingSource && eventName == QStringLiteral("otUpdateError")) {
                        sourceFailed(tr("The server rejected the document update."));
                        return;
                    }
                    if (eventName != QStringLiteral("joinProjectResponse"))
                        return;
                    const QJsonArray args = event.value(QStringLiteral("args")).toArray();
                    const QJsonObject response = args.isEmpty() ? QJsonObject() : args.at(0).toObject();
                    const QJsonObject project = response.value(QStringLiteral("project")).toObject();
                    if (validProjectFileTree(project))
                        writeCache(projectResourcePath(projectId, QStringLiteral("tree")),
                                   QJsonDocument(QJsonArray::fromVariantList(projectFileTreeEntries(project)))
                                   .toJson(QJsonDocument::Compact));
                    QString docId;
                    QString error;
                    if (!m_fileAction.isEmpty()) {
                        if (response.value(QStringLiteral("protocolVersion")).toInt() != 2) {
                            sourceFailed(tr("Unsupported real-time project protocol."));
                            return;
                        }
                        m_sourceSocket.clear();
                        disconnect(socket, nullptr, this, nullptr);
                        socket->abort();
                        socket->deleteLater();
                        runFileAction(response.value(QStringLiteral("project")).toObject(),
                                      projectId, path, generation);
                        return;
                    }
                    if (m_openingExternal) {
                        if (response.value(QStringLiteral("protocolVersion")).toInt() != 2
                                || !findProjectFile(response.value(QStringLiteral("project")).toObject(),
                                                    path, &docId, &error)) {
                            sourceFailed(error.isEmpty() ? tr("Unsupported real-time project protocol.") : error);
                            return;
                        }
                        m_sourceSocket.clear();
                        disconnect(socket, nullptr, this, nullptr);
                        socket->abort();
                        socket->deleteLater();
                        downloadExternalFile(projectId, docId, path, generation);
                        return;
                    }
                    if (response.value(QStringLiteral("protocolVersion")).toInt() != 2) {
                        sourceFailed(tr("Unsupported real-time project protocol."));
                        return;
                    }
                    const bool foundDoc = m_openingMainSource
                            ? findProjectMainDoc(project, &m_savePath, &docId, &error)
                            : findProjectDoc(project, path, &docId, &error);
                    if (!foundDoc) {
                        sourceFailed(error.isEmpty() ? tr("Unsupported real-time project protocol.") : error);
                        return;
                    }
                    if (m_savingSource)
                        m_saveDocId = docId;
                    const QJsonArray joinArgs{docId, QJsonObject{
                        {QStringLiteral("encodeRanges"), true},
                        {QStringLiteral("supportsHistoryOT"), false}
                    }};
                    const QJsonObject eventObject{{QStringLiteral("name"), QStringLiteral("joinDoc")},
                                                  {QStringLiteral("args"), joinArgs}};
                    socket->sendText(QStringLiteral("5:1+::")
                                            + QString::fromUtf8(QJsonDocument(eventObject).toJson(QJsonDocument::Compact)));
                } else if (message.startsWith(QStringLiteral("6:::1+"))) {
                    QJsonParseError jsonError;
                    const QJsonDocument doc = QJsonDocument::fromJson(message.mid(6).toUtf8(), &jsonError);
                    QString text;
                    QString error;
                    qint64 version = 0;
                    if (jsonError.error != QJsonParseError::NoError || !doc.isArray()
                            || !decodeJoinedDoc(doc.array(), &text, &error, &version)) {
                        sourceFailed(error.isEmpty() ? tr("Invalid document response from server.") : error);
                        return;
                    }
                    if (m_savingSource) {
                        if (m_saveConcurrent) {
                            sourceFailed(tr("The document changed on the server while opening it. Try again."));
                            return;
                        }
                        if (text == m_saveText) {
                            completeSourceSave();
                            return;
                        }
                        if (!m_saveBaseKnown || text != m_saveBase) {
                            sourceFailed(tr("The server document differs from the original local draft. "
                                            "The local draft was kept; resolve the conflict before saving."));
                            return;
                        }
                        m_saveVersion = version;
                        const QJsonObject update{{QStringLiteral("doc"), m_saveDocId},
                                                 {QStringLiteral("v"), static_cast<double>(version)},
                                                 {QStringLiteral("op"), makeTextOperation(text, m_saveText)}};
                        const QJsonObject requestObject{{QStringLiteral("name"), QStringLiteral("applyOtUpdate")},
                                                        {QStringLiteral("args"), QJsonArray{m_saveDocId, update}}};
                        m_saveSubmitted = true;
                        socket->sendText(QStringLiteral("5:2+::")
                                         + QString::fromUtf8(QJsonDocument(requestObject).toJson(QJsonDocument::Compact)));
                        return;
                    }
                    m_sourcePath = m_savePath;
                    m_sourceText = text;
                    if (writeCache(projectResourcePath(projectId, QStringLiteral("source"), m_sourcePath),
                                   text.toUtf8()))
                        writeCache(projectResourcePath(projectId, QStringLiteral("last")), m_sourcePath.toUtf8());
                    m_sourceBusy = false;
                    emit sourceBusyChanged();
                    emit sourceTextChanged();
                    socket->abort();
                    socket->deleteLater();
                    m_sourceSocket.clear();
                } else if (m_savingSource && message.startsWith(QStringLiteral("6:::2+"))) {
                    QJsonParseError jsonError;
                    const QJsonDocument response = QJsonDocument::fromJson(message.mid(6).toUtf8(), &jsonError);
                    if (jsonError.error != QJsonParseError::NoError || !response.isArray()
                            || (!response.array().isEmpty() && !response.array().at(0).isNull()))
                        sourceFailed(tr("The server rejected the document update."));
                } else if (message.startsWith(QStringLiteral("0::")) || message.startsWith(QStringLiteral("7::"))) {
                    sourceFailed(tr("The server rejected the real-time connection."));
                }
            });
            socket->open(socketUrl, socketCookieHeader(m_cookieHeader, responseCookies, endpoint, socketUrl,
                                                       m_network.cookieJar()),
                         m_server.toString().toUtf8());
        });
    });
}

void OverleafClient::downloadExternalFile(const QString &projectId, const QString &fileId,
                                          const QString &path, quint64 generation)
{
    const QString name = QFileInfo(path).fileName();
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
            || name.contains(QLatin1Char('\\')) || name.contains(QChar::Null)
            || cache.isEmpty() || !QDir().mkpath(cache)) {
        sourceFailed(tr("Could not prepare a temporary file."));
        return;
    }
    m_externalFile.reset(new QTemporaryFile(
        QDir(cache).filePath(QStringLiteral("Abedul-XXXXXX-") + name)));
    if (!m_externalFile->open()) {
        sourceFailed(tr("Could not create a temporary file: %1").arg(m_externalFile->errorString()));
        return;
    }
    const QString project = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    const QString file = QString::fromLatin1(QUrl::toPercentEncoding(fileId));
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/project/") + project
                                                 + QStringLiteral("/file/") + file));
    m_sourceReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, generation]() {
        if (generation != m_sourceGeneration)
            return;
        const QByteArray data = reply->readAll();
        if (m_externalFile->write(data) != data.size())
            sourceFailed(tr("Could not write the file copy: %1").arg(m_externalFile->errorString()));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectId, path]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString detail = reply->errorString();
        const QByteArray remaining = reply->readAll();
        reply->deleteLater();
        if (generation != m_sourceGeneration)
            return;
        m_sourceReply.clear();
        if (!ok) {
            sourceFailed(tr("Could not download the file (HTTP %1): %2").arg(status).arg(detail));
            return;
        }
        if (m_externalFile->write(remaining) != remaining.size() || !m_externalFile->flush()) {
            sourceFailed(tr("Could not write the file copy: %1").arg(m_externalFile->errorString()));
            return;
        }
        const QString localPath = m_externalFile->fileName();
        m_externalFile->close();
        copyToCache(localPath, projectResourcePath(projectId, QStringLiteral("binary"), path));
        m_externalFile->setProperty("accountCachePath", m_projectCachePath);
        m_externalFile.take()->setParent(this);
        closeProjectSource();
        emit externalFileReady(QUrl::fromLocalFile(localPath));
    });
}

void OverleafClient::renameProjectEntity(const QString &projectId, const QString &path,
                                         const QString &type, const QString &name)
{
    if (m_sourceBusy || m_filesBusy)
        return;
    if (projectId != m_filesProject || !validEntityName(name)
            || (type != QStringLiteral("doc") && type != QStringLiteral("file")
                && type != QStringLiteral("folder")) || path == QStringLiteral("/")) {
        m_filesError = tr("Invalid file name or project.");
        emit filesErrorChanged();
        return;
    }
    startProjectSource(projectId, path, false, QString(), false, QString(), false,
                       QStringLiteral("rename"), type, name);
}

void OverleafClient::loadProjectFileTree(const QString &projectId)
{
    if (m_offline)
        return;
    if (m_sourceBusy || m_filesBusy || projectId != m_filesProject)
        return;
    startProjectSource(projectId, QStringLiteral("/"), false, QString(), false, QString(), false,
                       QStringLiteral("list"), QStringLiteral("folder"));
}

void OverleafClient::deleteProjectEntity(const QString &projectId, const QString &path,
                                         const QString &type)
{
    if (m_sourceBusy || m_filesBusy)
        return;
    if (projectId != m_filesProject || path.isEmpty() || path == QStringLiteral("/")
            || (type != QStringLiteral("doc") && type != QStringLiteral("file")
                && type != QStringLiteral("folder"))) {
        m_filesError = tr("Invalid file name or project.");
        emit filesErrorChanged();
        return;
    }
    startProjectSource(projectId, path, false, QString(), false, QString(), false,
                       QStringLiteral("delete"), type);
}

void OverleafClient::uploadProjectFile(const QString &projectId, const QString &folder,
                                       const QUrl &localUrl)
{
    if (m_sourceBusy || m_filesBusy)
        return;
    const QFileInfo info(localUrl.toLocalFile());
    if (projectId != m_filesProject || !localUrl.isLocalFile() || !info.isFile()
            || !validEntityName(info.fileName())) {
        m_filesError = tr("Could not read the selected local file.");
        emit filesErrorChanged();
        return;
    }
    startProjectSource(projectId, folder, false, QString(), false, QString(), false,
                       QStringLiteral("upload"), QStringLiteral("folder"), info.fileName(), localUrl);
}

void OverleafClient::overwriteProjectFile(const QString &projectId, const QString &folder,
                                           const QUrl &localUrl, const QString &id, const QString &type)
{
    if (m_sourceBusy || m_filesBusy)
        return;
    const QFileInfo info(localUrl.toLocalFile());
    if (projectId != m_filesProject || !localUrl.isLocalFile() || !info.isFile()
            || !validEntityName(info.fileName()) || id.isEmpty()
            || (type != QStringLiteral("doc") && type != QStringLiteral("file"))) {
        m_filesError = tr("Could not read the selected local file.");
        emit filesErrorChanged();
        return;
    }
    startProjectSource(projectId, folder, false, QString(), false, QString(), false,
                       QStringLiteral("upload"), QStringLiteral("folder"), info.fileName(),
                       localUrl, false, id, type);
}

void OverleafClient::runFileAction(const QJsonObject &project, const QString &projectId,
                                  const QString &path, quint64 generation)
{
    if (!validProjectFileTree(project)) {
        sourceFailed(tr("The server returned an invalid project file tree."));
        return;
    }
    if (m_fileAction == QStringLiteral("list")) {
        const QVariantList entries = projectFileTreeEntries(project);
        writeCache(projectResourcePath(projectId, QStringLiteral("tree")),
                   QJsonDocument(QJsonArray::fromVariantList(entries)).toJson(QJsonDocument::Compact));
        closeProjectSource();
        m_projectFiles = entries;
        m_filesError.clear();
        emit filesErrorChanged();
        emit projectFilesChanged();
        return;
    }
    if (m_fileType == QStringLiteral("folder") && m_fileAction != QStringLiteral("upload")) {
        QSet<QString> displayed, current;
        const QVariantList tree = projectFileTreeEntries(project);
        for (const QVariant &entry : m_projectFiles) {
            const QVariantMap item = entry.toMap();
            if (item.value(QStringLiteral("type")).toString() == QStringLiteral("doc")) {
                QString name = item.value(QStringLiteral("path")).toString();
                if (!name.startsWith(QLatin1Char('/')))
                    name.prepend(QLatin1Char('/'));
                displayed.insert(name);
            }
        }
        for (const QVariant &entry : tree) {
            const QVariantMap item = entry.toMap();
            if (item.value(QStringLiteral("type")).toString() == QStringLiteral("doc"))
                current.insert(item.value(QStringLiteral("path")).toString());
        }
        if (displayed != current) {
            sourceFailed(tr("The project file list changed. Refresh it before trying again."));
            return;
        }
    }
    if (m_csrf.isEmpty()) {
        sourceFailed(tr("Could not obtain a security token for the file operation."));
        return;
    }
    QString id, error;
    const bool found = m_fileType == QStringLiteral("doc") ? findProjectDoc(project, path, &id, &error)
            : m_fileType == QStringLiteral("file") ? findProjectFile(project, path, &id, &error)
            : findProjectFolder(project, path, &id, &error);
    if (!found) {
        sourceFailed(error);
        return;
    }
    if (m_fileAction == QStringLiteral("upload") || m_fileAction == QStringLiteral("rename")) {
        const QString parent = m_fileAction == QStringLiteral("upload") ? path
                              : path.left(path.lastIndexOf(QLatin1Char('/')));
        const QString target = (parent.isEmpty() || parent == QStringLiteral("/")
                                ? QStringLiteral("/") : parent + QLatin1Char('/')) + m_fileName;
        QString collision, ignored, collisionType;
        if (findProjectDoc(project, target, &collision, &ignored))
            collisionType = QStringLiteral("doc");
        else if (findProjectFile(project, target, &collision, &ignored))
            collisionType = QStringLiteral("file");
        else if (findProjectFolder(project, target, &collision, &ignored))
            collisionType = QStringLiteral("folder");
        if (m_fileAction == QStringLiteral("upload") && !collisionType.isEmpty()
                && collisionType != QStringLiteral("folder")
                && (collision != m_overwriteId || collisionType != m_overwriteType)) {
            closeProjectSource();
            emit projectFileOverwriteRequested(target, collisionType, collision);
            return;
        }
        if (m_fileAction == QStringLiteral("upload") && !m_overwriteId.isEmpty()
                && collisionType.isEmpty()) {
            sourceFailed(tr("The file changed on the server. Refresh and try again."));
            return;
        }
        if (!collisionType.isEmpty() && (m_fileAction != QStringLiteral("upload")
                                        || collisionType == QStringLiteral("folder"))) {
            sourceFailed(tr("An item with this name already exists."));
            return;
        }
    }
    const QString projectPart = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    const QString idPart = QString::fromLatin1(QUrl::toPercentEncoding(id));
    const QString base = QStringLiteral("/project/") + projectPart;
    QNetworkReply *reply = nullptr;
    if (m_fileAction == QStringLiteral("upload")) {
        QFile *file = new QFile(m_fileLocalUrl.toLocalFile());
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            sourceFailed(tr("Could not read the selected local file."));
            return;
        }
        QHttpMultiPart *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        file->setParent(multi);
        QHttpPart part;
        part.setRawHeader("Content-Disposition",
                          QByteArray("form-data; name=\"qqfile\"; filename=\"")
                          + m_fileName.toUtf8() + QByteArray("\""));
        part.setBodyDevice(file);
        multi->append(part);
        QHttpPart name;
        name.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"name\""));
        name.setBody(m_fileName.toUtf8());
        multi->append(name);
        QNetworkRequest req = request(base + QStringLiteral("/upload?folder_id=") + idPart);
        reply = m_network.post(req, multi);
        multi->setParent(reply);
    } else {
        const QString resource = base + QLatin1Char('/') + m_fileType + QLatin1Char('/') + idPart;
        if (m_fileAction == QStringLiteral("delete")) {
            reply = m_network.deleteResource(request(resource));
        } else {
            QNetworkRequest req = request(resource + QStringLiteral("/rename"));
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            reply = m_network.post(req, QJsonDocument(QJsonObject{{QStringLiteral("name"), m_fileName}})
                                   .toJson(QJsonDocument::Compact));
        }
    }
    const bool upload = m_fileAction == QStringLiteral("upload");
    m_sourceReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectId, path, upload]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        reply->deleteLater();
        if (generation != m_sourceGeneration)
            return;
        m_sourceReply.clear();
        if (!ok) {
            sourceFailed(tr("File operation failed (HTTP %1). Refresh to check the server state.").arg(status));
            return;
        }
        if (upload && !QJsonDocument::fromJson(body).object().value(QStringLiteral("success")).toBool()) {
            sourceFailed(tr("The server rejected the upload. Refresh to check the server state."));
            return;
        }
        const QString action = m_fileAction;
        const QString type = m_fileType;
        const QString parent = path.left(path.lastIndexOf(QLatin1Char('/')));
        const QString newPath = action == QStringLiteral("rename")
                ? (parent.isEmpty() ? QStringLiteral("/") : parent + QLatin1Char('/')) + m_fileName
                : QString();
        closeProjectSource();
        emit projectFileOperationFinished(action, path, type, newPath);
        loadProjectFiles(projectId);
    });
}

void OverleafClient::closeProjectFiles()
{
    if (m_openingExternal || !m_fileAction.isEmpty())
        closeProjectSource();
    ++m_filesGeneration;
    if (m_filesReply)
        m_filesReply->abort();
    m_filesReply.clear();
    m_projectFiles.clear();
    m_filesProject.clear();
    m_filesError.clear();
    m_filesBusy = false;
    emit projectFilesChanged();
    emit filesErrorChanged();
    emit filesBusyChanged();
}

void OverleafClient::loadProjectFiles(const QString &projectId)
{
    if (m_filesBusy)
        return;
    bool found = false;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            found = true;
            break;
        }
    }
    if (!m_authenticated || !found) {
        m_filesError = tr("This project is not available in the current session.");
        emit filesErrorChanged();
        return;
    }
    closeProjectFiles();
    if (m_offline) {
        m_filesProject = projectId;
        QFile cache(projectResourcePath(projectId, QStringLiteral("tree")));
        QJsonParseError error;
        if (!cache.open(QIODevice::ReadOnly)) {
            m_filesError = tr("The project file list is not available offline.");
        } else {
            const QJsonDocument doc = QJsonDocument::fromJson(cache.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isArray())
                m_filesError = tr("Could not read the cached project file list.");
            else
                m_projectFiles = doc.array().toVariantList();
        }
        emit projectFilesChanged();
        emit filesErrorChanged();
        return;
    }
    m_filesBusy = true;
    emit filesBusyChanged();
    const quint64 generation = m_filesGeneration;
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    QNetworkReply *reply = m_network.get(request(QStringLiteral("/project/") + id
                                                + QStringLiteral("/entities")));
    m_filesReply = reply;
    QTimer::singleShot(60000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectId]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QByteArray body = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();
        if (generation != m_filesGeneration)
            return;
        m_filesReply.clear();
        m_filesBusy = false;
        emit filesBusyChanged();
        if (!ok) {
            m_filesError = tr("Could not load project files (HTTP %1): %2").arg(status).arg(networkError);
            if (status == 0 && !m_projectCachePath.isEmpty()) {
                setError(m_filesError);
                m_offline = true;
                emit offlineChanged();
                loadProjectFiles(projectId);
                return;
            }
        } else {
            m_filesProject = projectId;
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
            const QJsonValue entities = doc.object().value(QStringLiteral("entities"));
            if (error.error != QJsonParseError::NoError || !doc.isObject() || !entities.isArray()) {
                m_filesError = tr("Invalid project file list from server.");
            } else {
                for (const QJsonValue &value : entities.toArray()) {
                    const QJsonObject item = value.toObject();
                    if (!value.isObject() || !item.value(QStringLiteral("path")).isString()
                            || !item.value(QStringLiteral("type")).isString()) {
                        m_projectFiles.clear();
                        m_filesError = tr("Invalid project file list from server.");
                        break;
                    }
                    m_projectFiles.append(item.toVariantMap());
                }
                if (m_filesError.isEmpty())
                    writeCache(projectResourcePath(projectId, QStringLiteral("tree")),
                               QJsonDocument(QJsonArray::fromVariantList(m_projectFiles)).toJson(QJsonDocument::Compact));
                emit projectFilesChanged();
            }
        }
        emit filesErrorChanged();
    });
}

void OverleafClient::setPdfError(const QString &message)
{
    if (m_pdfError != message) {
        m_pdfError = message;
        emit pdfErrorChanged();
    }
}

void OverleafClient::setPdfBusy(bool busy)
{
    if (m_pdfBusy != busy) {
        m_pdfBusy = busy;
        emit pdfBusyChanged();
    }
}

void OverleafClient::closeProjectPdf()
{
    ++m_pdfGeneration;
    ++m_syncGeneration;
    if (m_syncReply)
        m_syncReply->abort();
    m_syncReply.clear();
    m_syncBuild = SyncTeXBuild();
    m_syncProject.clear();
    m_syncRoot.clear();
    if (m_pdfReply)
        m_pdfReply->abort();
    m_pdfReply.clear();
    if (!m_pdfPath.isEmpty()) {
        m_pdfPath.clear();
        emit pdfPathChanged();
    }
    m_pdfFile.reset();
    setPdfBusy(false);
    setPdfError(QString());
}

QString OverleafClient::projectPdfCachePath(const QString &projectId) const
{
    if (m_projectCachePath.isEmpty() || projectId.isEmpty())
        return QString();
    return m_projectCachePath + QStringLiteral(".pdf-")
            + QString::fromLatin1(QCryptographicHash::hash(projectId.toUtf8(), QCryptographicHash::Sha256).toHex());
}

void OverleafClient::restoreProjectPdf(const QString &projectId)
{
    if (m_pdfBusy || !m_pdfPath.isEmpty())
        return;
    bool found = false;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            found = true;
            break;
        }
    }
    const QString path = projectPdfCachePath(projectId);
    if (!m_authenticated || !found || path.isEmpty())
        return;
    if (!QFile::exists(path) && !m_userId.isEmpty()) {
        const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QByteArray key = server().toUtf8() + '\0' + m_userId.toUtf8();
        const QString oldBase = QDir(cacheDir).filePath(QStringLiteral("projects-")
                + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
                + QStringLiteral(".json"));
        const QString oldPath = oldBase + QStringLiteral(".pdf-")
                + QString::fromLatin1(QCryptographicHash::hash(projectId.toUtf8(), QCryptographicHash::Sha256).toHex());
        if (!cacheDir.isEmpty() && QFile::exists(oldPath))
            copyToCache(oldPath, path);
    }
    if (!QFile::exists(path))
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.read(5) != QByteArray("%PDF-")) {
        setPdfError(tr("Could not read the cached PDF."));
        return;
    }
    m_pdfPath = path;
    emit pdfPathChanged();
}

void OverleafClient::compileProjectPdf(const QString &projectId, const QString &rootPath)
{
    if (m_offline) {
        setPdfError(tr("This operation is not available offline."));
        return;
    }
    if (m_pdfBusy)
        return;
    bool found = false;
    for (const QVariant &entry : m_projects) {
        const QVariantMap project = entry.toMap();
        if (project.value(QStringLiteral("id")).toString() == projectId
                && !project.value(QStringLiteral("trashed")).toBool()) {
            found = true;
            break;
        }
    }
    if (!m_authenticated || !found) {
        setPdfError(tr("This project is not available in the current session."));
        return;
    }
    QByteArray payload;
    QString payloadError;
    const QString editorId = QUuid::createUuid().toString().mid(1, 36);
    if (!compileRequestBody(rootPath, &payload, &payloadError, editorId)) {
        setPdfError(payloadError);
        return;
    }
    closeProjectPdf();
    restoreProjectPdf(projectId);
    const quint64 generation = m_pdfGeneration;
    setPdfBusy(true);
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(projectId));
    const QString projectPath = QStringLiteral("/project/") + id;
    QNetworkReply *reply = m_network.get(request(projectPath));
    m_pdfReply = reply;
    QTimer::singleShot(60000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectPath, projectId, editorId, rootPath, payload]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QByteArray html = reply->readAll();
        reply->deleteLater();
        if (generation != m_pdfGeneration)
            return;
        if (!ok) {
            setPdfError(tr("Could not open project for compilation (HTTP %1): %2")
                        .arg(status).arg(reply->errorString()));
            setPdfBusy(false);
            return;
        }
        const QString token = extractCsrfToken(QString::fromUtf8(html));
        if (token.isEmpty()) {
            setPdfError(tr("Could not obtain the project security token."));
            setPdfBusy(false);
            return;
        }
        m_csrf = token.toUtf8();
        QNetworkRequest req = request(projectPath + QStringLiteral("/compile"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *compileReply = m_network.post(req, payload);
        m_pdfReply = compileReply;
        QTimer::singleShot(120000, compileReply, [compileReply]() {
            if (compileReply->isRunning()) compileReply->abort();
        });
        connect(compileReply, &QNetworkReply::finished, this, [this, compileReply, generation, projectId, editorId, rootPath]() {
            const int status = compileReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = compileReply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
            const QByteArray response = compileReply->readAll();
            const QString error = compileReply->errorString();
            compileReply->deleteLater();
            if (generation != m_pdfGeneration)
                return;
            if (!ok) {
                const QString detail = compileServerError(response);
                setPdfError(tr("Compilation request failed (HTTP %1): %2")
                            .arg(status).arg(detail.isEmpty() ? error : detail));
                setPdfBusy(false);
                return;
            }
            downloadProjectPdf(response, generation, projectId, editorId, rootPath);
        });
    });
}

void OverleafClient::downloadProjectPdf(const QByteArray &response, quint64 generation,
                                        const QString &projectId, const QString &editorId, const QString &rootPath)
{
    QUrl url;
    bool sendCookies = false;
    QString error;
    if (!compilePdfUrl(response, m_server, &url, &sendCookies, &error)) {
        setPdfError(error);
        setPdfBusy(false);
        return;
    }
    SyncTeXBuild syncBuild;
    QString syncBuildError;
    syncTeXBuildFromCompile(response, editorId, &syncBuild, &syncBuildError);
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    if (sendCookies)
        req.setRawHeader("Cookie", m_cookieHeader);
    QNetworkReply *reply = m_network.get(req);
    m_pdfReply = reply;
    QTimer::singleShot(60000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, projectId, syncBuild, rootPath]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QByteArray data = reply->readAll();
        const QString error = reply->errorString();
        reply->deleteLater();
        if (generation != m_pdfGeneration)
            return;
        m_pdfReply.clear();
        if (!ok) {
            setPdfError(tr("PDF download failed (HTTP %1): %2").arg(status).arg(error));
        } else if (!data.startsWith("%PDF-")) {
            setPdfError(tr("The server did not return a PDF document."));
        } else {
            QScopedPointer<QTemporaryFile> file(new QTemporaryFile(
                QDir::tempPath() + QStringLiteral("/abedul-pdf-XXXXXX")));
            if (!file->open() || file->write(data) != data.size() || !file->flush()) {
                setPdfError(tr("Could not save the preview PDF: %1").arg(file->errorString()));
            } else {
                m_pdfPath = file->fileName();
                file->close();
                m_pdfFile.swap(file);
                const QString cachePath = projectPdfCachePath(projectId);
                if (!cachePath.isEmpty()) {
                    QSaveFile cache(cachePath);
                    if (!cache.open(QIODevice::WriteOnly)
                            || !cache.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
                            || cache.write(data) != data.size() || !cache.commit())
                        setPdfError(tr("Could not cache the PDF."));
                }
                m_syncBuild = syncBuild;
                m_syncProject = projectId;
                m_syncRoot = rootPath;
                emit pdfPathChanged();
            }
        }
        setPdfBusy(false);
    });
}

void OverleafClient::syncSourceToPdf(const QString &projectId, const QString &path, int line, int column)
{
    QString file = path;
    if (file.startsWith(QLatin1Char('/')))
        file.remove(0, 1);
    if (projectId != m_syncProject || m_syncBuild.buildId.isEmpty()) {
        emit syncError(tr("SyncTeX is unavailable for this PDF. Compile the project again."));
        return;
    }
    if (file.isEmpty() || file.startsWith(QLatin1Char('/')) || file.contains(QLatin1Char('\\'))
            || file.contains(QChar::Null) || file.split(QLatin1Char('/')).contains(QStringLiteral(".."))
            || file.split(QLatin1Char('/')).contains(QStringLiteral(".")) || line < 1 || column < 0) {
        emit syncError(tr("Invalid source location."));
        return;
    }
    QString root = m_syncRoot;
    if (root.startsWith(QLatin1Char('/')))
        root.remove(0, 1);
    const int separator = root.lastIndexOf(QLatin1Char('/'));
    if (separator > 0) {
        const QString prefix = root.left(separator + 1);
        if (file.startsWith(prefix))
            file.insert(prefix.size(), QStringLiteral("./"));
    }
    syncRequest(syncTeXUrl(m_server, projectId, m_syncBuild, file, line, column), true);
}

void OverleafClient::syncPdfToSource(const QString &projectId, int page, double h, double v)
{
    if (projectId != m_syncProject || m_syncBuild.buildId.isEmpty()) {
        emit syncError(tr("SyncTeX is unavailable for this PDF. Compile the project again."));
        return;
    }
    if (page < 1 || !qIsFinite(h) || !qIsFinite(v)) {
        emit syncError(tr("Invalid PDF location."));
        return;
    }
    syncRequest(syncTeXUrl(m_server, projectId, m_syncBuild, page, h, v), false);
}

void OverleafClient::syncRequest(const QUrl &url, bool toPdf)
{
    ++m_syncGeneration;
    if (m_syncReply)
        m_syncReply->abort();
    const quint64 generation = m_syncGeneration;
    QNetworkReply *reply = m_network.get(request(url.toString(QUrl::FullyEncoded)));
    m_syncReply = reply;
    QTimer::singleShot(60000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, toPdf]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QByteArray response = reply->readAll();
        const QString detail = reply->errorString();
        reply->deleteLater();
        if (generation != m_syncGeneration)
            return;
        m_syncReply.clear();
        if (!ok) {
            emit syncError(tr("SyncTeX request failed (HTTP %1): %2").arg(status).arg(detail));
            return;
        }
        QVariantList positions;
        QString error;
        if (!syncTeXResult(response, toPdf, &positions, &error)) {
            emit syncError(error);
            return;
        }
        if (toPdf)
            emit syncPdfPositions(positions);
        else
            emit syncSourcePositions(positions);
    });
}

void OverleafClient::setError(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void OverleafClient::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

bool OverleafClient::setServer(const QString &address)
{
    if (m_busy)
        return false;
    const QUrl url = QUrl::fromUserInput(address.trimmed());
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
            || url.host().isEmpty() || !url.userInfo().isEmpty()
            || (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
            || url.hasQuery() || url.hasFragment()) {
        setError(tr("Enter an HTTPS server address without a path."));
        return false;
    }
    if (m_archiveReply)
        m_archiveReply->abort();
    ++m_commentsGeneration;
    if (m_commentsReply)
        m_commentsReply->abort();
    m_commentsReply.clear();
    m_commentsAvailable = false;
    emit commentsAvailableChanged();
    m_collaboration->close();
    m_projectCreationError.clear();
    emit projectCreationChanged();
    m_projectActionError.clear();
    emit projectActionChanged();
    closeProjectPdf();
    closeProjectFiles();
    closeProjectSource();
    m_server = url;
    m_server.setPath(QString());
    m_archiveError.clear();
    m_archivePath.clear();
    m_archiveProject.clear();
    emit archiveStatusChanged();
    m_cookieHeader.clear();
    m_pendingSessionCookies.clear();
    m_csrf.clear();
    m_network.setCookieJar(new QNetworkCookieJar(&m_network));
    m_projectCachePath.clear();
    m_userId.clear();
    m_userLabel.clear();
    emit accountChanged();
    if (m_offline) {
        m_offline = false;
        emit offlineChanged();
    }
    m_projectsCached = false;
    emit projectsCachedChanged();
    m_projects.clear();
    if (m_authenticated) {
        m_authenticated = false;
        emit authenticatedChanged();
    }
    emit projectsChanged();
    emit serverChanged();
    setError(QString());
    return true;
}

void OverleafClient::probeProjectComments(const QString &projectId)
{
    ++m_commentsGeneration;
    if (m_commentsReply)
        m_commentsReply->abort();
    m_commentsReply.clear();
    m_commentsAvailable = false;
    emit commentsAvailableChanged();
    if (m_offline || !m_authenticated || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9_-]{1,128}$")).match(projectId).hasMatch())
        return;
    const quint64 generation = m_commentsGeneration;
    QNetworkRequest req = request(QStringLiteral("/project/") + projectId + QStringLiteral("/threads"));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    QNetworkReply *reply = m_network.get(req);
    m_commentsReply = reply;
    QTimer::singleShot(30000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool redirected = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        reply->deleteLater();
        if (generation != m_commentsGeneration)
            return;
        m_commentsReply.clear();
        // A 404 is the server's indication that this endpoint is unavailable.
        // Other failures leave the action visible so the user can see the error.
        m_commentsAvailable = status != 404 || redirected;
        emit commentsAvailableChanged();
    });
}

void OverleafClient::rememberConnectedServer()
{
    const QString server = m_server.toString();
    if (autoRestoreServer() && server == m_rememberedServer && !m_rememberedServers.isEmpty()
            && m_rememberedServers.first() == server)
        return;
    QStringList servers = m_rememberedServers;
    servers.removeAll(server);
    servers.prepend(server);
    QSettings settings;
    settings.setValue(QStringLiteral("lastServer"), server);
    settings.setValue(QStringLiteral("servers"), servers);
    settings.setValue(QStringLiteral("autoRestoreServer"), true);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(tr("Could not remember the server address."));
        return;
    }
    m_rememberedServer = server;
    m_rememberedServers = servers;
    emit rememberedServerChanged();
    emit rememberedServersChanged();
}

bool OverleafClient::isProjectListUrl(const QUrl &url) const
{
    return !m_server.isEmpty() && url.isValid() && url.userInfo().isEmpty()
            && url.scheme() == m_server.scheme()
            && url.host() == m_server.host()
            && url.port(443) == m_server.port(443)
            && (url.path() == QStringLiteral("/project")
                || url.path() == QStringLiteral("/project/"));
}

QNetworkRequest OverleafClient::request(const QString &path) const
{
    QNetworkRequest result(m_server.resolved(QUrl(path)));
    result.setRawHeader("Accept", "application/json, text/html");
    if (!m_cookieHeader.isEmpty())
        result.setRawHeader("Cookie", m_cookieHeader);
    if (!m_csrf.isEmpty())
        result.setRawHeader("X-CSRF-Token", m_csrf);
    return result;
}

bool OverleafClient::checkReply(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        setError(tr("Server request failed (HTTP %1): %2")
                 .arg(status).arg(reply->errorString()));
        setBusy(false);
        return false;
    }
    return true;
}

void OverleafClient::finishBrowserLogin(const QVariantList &cookies)
{
    if (m_busy || m_server.isEmpty())
        return;
    QByteArray header;
    for (const QVariant &entry : cookies) {
        const QVariantMap cookie = entry.toMap();
        const QByteArray name = cookie.value(QStringLiteral("name")).toString().toUtf8();
        const QByteArray value = cookie.value(QStringLiteral("value")).toString().toUtf8();
        if (name.isEmpty() || name.contains(';') || name.contains('=') || name.contains('\n')
                || name.contains('\r') || value.contains(';') || value.contains('\n')
                || value.contains('\r')) {
            setError(tr("The browser returned an invalid session cookie."));
            return;
        }
        if (!header.isEmpty())
            header += "; ";
        header += name + '=' + value;
    }
    if (header.isEmpty()) {
        setError(tr("No session cookies were found. Complete sign-in first."));
        return;
    }
    m_pendingSessionCookies = cookies;
    const bool reconnecting = m_offline;
    if (!reconnecting) {
        m_cookieHeader = header;
        m_csrf.clear();
    }
    setError(QString());
    setBusy(true);
    QNetworkRequest req = request(QStringLiteral("/project"));
    req.setRawHeader("Cookie", header);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    if (reconnecting) {
        req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    }
    QNetworkReply *reply = m_network.get(req);
    QTimer::singleShot(30000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, reconnecting, header]() {
        const bool ok = checkReply(reply);
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!ok)
            return;
        const QString token = extractCsrfToken(QString::fromUtf8(body));
        if (token.isEmpty()) {
            if (!reconnecting)
                m_cookieHeader.clear();
            setError(tr("The server did not return an authenticated project page."));
            setBusy(false);
            return;
        }
        const QString html = QString::fromUtf8(body);
        const QString userMeta = QTextDocumentFragment::fromHtml(
                    extractMetaContent(html, QStringLiteral("ol-user"))).toPlainText();
        const QJsonObject user = QJsonDocument::fromJson(userMeta.toUtf8()).object();
        QString userId = user.value(QStringLiteral("id")).toString();
        if (userId.isEmpty()) userId = user.value(QStringLiteral("_id")).toString();
        if (reconnecting && (userId.isEmpty() || userId != m_userId)) {
            setError(tr("The server session belongs to a different account. Cached projects remain available."));
            setBusy(false);
            return;
        }
        m_cookieHeader = header;
        m_csrf = token.toUtf8();
        if (reconnecting) {
            loadProjects(true);
            return;
        }
        if (QRegularExpression(QStringLiteral("^[a-zA-Z0-9_-]{1,128}$")).match(userId).hasMatch()) {
            const QByteArray key = m_server.toString().toUtf8() + '\0' + userId.toUtf8();
            m_userId = userId;
            m_userLabel = user.value(QStringLiteral("email")).toString();
            if (m_userLabel.isEmpty()) m_userLabel = userId;
            m_projectCachePath = accountCachePath(server(), userId);
            emit accountChanged();
            if (!m_projectCachePath.isEmpty()) {
                const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
                const QString legacyPath = cacheDir.isEmpty() ? QString() : QDir(cacheDir).filePath(
                    QStringLiteral("projects-")
                    + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
                    + QStringLiteral(".json"));
                if (!QFile::exists(m_projectCachePath) && !legacyPath.isEmpty()) {
                    QFile old(legacyPath);
                    QVariantList oldProjects;
                    QString oldError;
                    if (old.open(QIODevice::ReadOnly)) {
                        const QByteArray data = old.readAll();
                        if (parseProjectList(data, &oldProjects, &oldError))
                            writeCache(m_projectCachePath, data);
                    }
                }
                QFile cache(m_projectCachePath);
                QVariantList projects;
                QString cacheError;
                if (cache.exists()) {
                    if (cache.open(QIODevice::ReadOnly) && parseProjectList(cache.readAll(), &projects, &cacheError)) {
                        m_projects = projects;
                        m_projectsCached = true;
                        rememberCachedAccount();
                        emit projectsChanged();
                        emit projectsCachedChanged();
                        m_authenticated = true;
                        emit authenticatedChanged();
                    } else {
                        qWarning() << "Cannot read project cache:" << (cacheError.isEmpty() ? cache.errorString() : cacheError);
                    }
                }
            } else {
                qWarning() << "Offline cache unavailable: server did not provide a valid user ID";
            }
        }
        loadProjects();
    });
}

void OverleafClient::refreshProjects()
{
    if (m_offline) {
        if (!m_busy) {
            setError(QString());
            emit reconnectRequested();
        }
        return;
    }
    if (m_busy || !m_authenticated)
        return;
    m_projectActionError.clear();
    emit projectActionChanged();
    setError(QString());
    setBusy(true);
    loadProjects();
}

void OverleafClient::createProject(const QString &name)
{
    if (m_busy || m_createProjectReply)
        return;
    const QString projectName = name.trimmed();
    if (!m_authenticated || m_csrf.isEmpty() || projectName.isEmpty()) {
        m_projectCreationError = tr("Enter a project name and sign in first.");
        emit projectCreationChanged();
        return;
    }
    m_projectCreationError.clear();
    setBusy(true);
    QNetworkRequest req = request(QStringLiteral("/project/new"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    QNetworkReply *reply = m_network.post(req, QJsonDocument(QJsonObject{
        {QStringLiteral("projectName"), projectName}
    }).toJson(QJsonDocument::Compact));
    m_createProjectReply = reply;
    emit projectCreationChanged();
    QTimer::singleShot(30000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                && !reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        m_createProjectReply.clear();
        if (!ok) {
            m_projectCreationError = tr("Could not create the project (HTTP %1).").arg(status);
            if (status == 0 || status >= 500)
                m_projectCreationError += tr(" The project may have been created; refresh before retrying.");
        } else {
            const QString id = QJsonDocument::fromJson(body).object().value(QStringLiteral("project_id")).toString();
            if (!QRegularExpression(QStringLiteral("^[a-zA-Z0-9_-]{1,128}$")).match(id).hasMatch())
                m_projectCreationError = tr("The server returned an invalid response. Refresh before retrying.");
        }
        setBusy(false);
        emit projectCreationChanged();
        if (m_projectCreationError.isEmpty())
            refreshProjects();
    });
}

void OverleafClient::changeProjectTrash(const QString &projectId, const QString &action)
{
    if (m_busy)
        return;
    int index = -1;
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects.at(i).toMap().value(QStringLiteral("id")).toString() == projectId) {
            index = i;
            break;
        }
    }
    const QVariantMap project = index < 0 ? QVariantMap() : m_projects.at(index).toMap();
    const bool trashed = project.value(QStringLiteral("trashed")).toBool();
    const bool validAction = (action == QStringLiteral("trash") && !trashed)
            || (action == QStringLiteral("restore") && trashed)
            || (action == QStringLiteral("delete") && trashed
                && project.value(QStringLiteral("accessLevel")).toString() == QStringLiteral("owner"));
    if (!m_authenticated || m_projectsCached || m_csrf.isEmpty() || index < 0 || !validAction
            || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9_-]{1,128}$")).match(projectId).hasMatch()) {
        m_projectActionError = tr("This project action is not available. Refresh the list and sign in again.");
        emit projectActionChanged();
        return;
    }
    m_projectActionError.clear();
    emit projectActionChanged();
    setBusy(true);
    QNetworkRequest req = request(QStringLiteral("/project/") + projectId
                                  + (action == QStringLiteral("delete") ? QString() : QStringLiteral("/trash")));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = action == QStringLiteral("trash")
            ? m_network.post(req, QByteArray("{}")) : m_network.deleteResource(req);
    QTimer::singleShot(30000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, projectId, action]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
                && !reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        reply->deleteLater();
        if (!ok) {
            m_projectActionError = tr("Could not update the project (HTTP %1).").arg(status);
            if (status == 0 || status >= 500)
                m_projectActionError += tr(" The action may have succeeded; refresh before retrying.");
            emit projectActionChanged();
            setBusy(false);
            return;
        }
        for (int i = 0; i < m_projects.size(); ++i) {
            QVariantMap project = m_projects.at(i).toMap();
            if (project.value(QStringLiteral("id")).toString() != projectId)
                continue;
            if (action == QStringLiteral("delete")) {
                m_projects.removeAt(i);
            } else {
                project[QStringLiteral("trashed")] = action == QStringLiteral("trash");
                if (action == QStringLiteral("trash"))
                    project[QStringLiteral("archived")] = false;
                m_projects[i] = project;
            }
            break;
        }
        emit projectsChanged();
        writeProjectCache();
        emit projectTrashChanged(projectId, action);
        setBusy(false);
    });
}

void OverleafClient::loadProjects(bool reconnecting)
{
    QNetworkRequest req = request(QStringLiteral("/api/project"));
    if (reconnecting) {
        req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    }
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QJsonObject payload{
        {QStringLiteral("filters"), QJsonObject()},
        {QStringLiteral("page"), QJsonObject{{QStringLiteral("size"), 10000}}},
        {QStringLiteral("sort"), QJsonObject{{QStringLiteral("by"), QStringLiteral("lastUpdated")},
                                             {QStringLiteral("order"), QStringLiteral("desc")}}}
    };
    QNetworkReply *reply = m_network.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QTimer::singleShot(30000, reply, [reply]() { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, reconnecting]() {
        const bool ok = checkReply(reply);
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!ok) {
            if (reconnecting) {
                m_cookieHeader.clear();
                m_csrf.clear();
            }
            return;
        }
        QVariantList projects;
        QString parseError;
        if (!parseProjectList(body, &projects, &parseError)) {
            if (reconnecting) {
                m_cookieHeader.clear();
                m_csrf.clear();
            }
            setError(parseError);
            setBusy(false);
            return;
        }
        m_projects = projects;
        m_projectsCached = false;
        emit projectsChanged();
        emit projectsCachedChanged();
        writeProjectCache();
        rememberCachedAccount();
        if (!m_userId.isEmpty() && !m_pendingSessionCookies.isEmpty()) {
            QString sessionError;
            if (!saveSession(&sessionError))
                setError(sessionError);
        }
        if (!m_authenticated) {
            m_authenticated = true;
            emit authenticatedChanged();
        }
        rememberConnectedServer();
        setBusy(false);
        if (reconnecting) {
            m_offline = false;
            emit offlineChanged();
            emit reconnected();
        }
    });
}

void OverleafClient::writeProjectCache()
{
    if (m_projectCachePath.isEmpty())
        return;
    const QByteArray data = QJsonDocument(QJsonArray::fromVariantList(m_projects)).toJson(QJsonDocument::Compact);
    writeCache(m_projectCachePath, data);
}
