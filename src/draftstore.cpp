#include "draftstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

DraftStore::DraftStore(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory)
{
    if (m_directory.isEmpty()) {
        const QString appDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!appDirectory.isEmpty())
            m_directory = QDir(appDirectory).filePath(QStringLiteral("drafts"));
    }
}

QString DraftStore::text() const { return m_text; }
QString DraftStore::error() const { return m_error; }
bool DraftStore::exists() const { return m_exists; }
QString DraftStore::baseText() const { return m_baseText; }
bool DraftStore::baseKnown() const { return m_baseKnown; }

bool DraftStore::loadFile(const QString &server, const QString &projectId, const QString &path)
{
    if (projectId.isEmpty() || path.isEmpty() || projectId.contains(QChar::Null) || path.contains(QChar::Null)) {
        setError(tr("Invalid project file for local draft."));
        return false;
    }
    return load(server, projectId + QChar::Null + path);
}

bool DraftStore::saveFile(const QString &server, const QString &projectId, const QString &path, const QString &text)
{
    if (projectId.isEmpty() || path.isEmpty() || projectId.contains(QChar::Null) || path.contains(QChar::Null)) {
        setError(tr("Invalid project file for local draft."));
        return false;
    }
    return save(server, projectId + QChar::Null + path, text);
}

bool DraftStore::saveFileWithBase(const QString &server, const QString &projectId, const QString &path,
                                  const QString &text, const QString &base, bool baseKnown)
{
    if (projectId.isEmpty() || path.isEmpty() || projectId.contains(QChar::Null) || path.contains(QChar::Null)) {
        setError(tr("Invalid project file for local draft."));
        return false;
    }
    QJsonObject record{{QStringLiteral("text"), text}};
    if (baseKnown)
        record.insert(QStringLiteral("base"), base);
    return saveRecord(server, projectId + QChar::Null + path, record);
}

QString DraftStore::loadCompileRoot(const QString &server, const QString &projectId)
{
    if (projectId.isEmpty() || projectId.contains(QChar::Null)) {
        setError(tr("Invalid project for main document."));
        return QString();
    }
    if (!load(server, projectId + QChar::Null + QChar::Null + QStringLiteral("compileRoot")))
        return QString();
    return m_text;
}

bool DraftStore::saveCompileRoot(const QString &server, const QString &projectId, const QString &root)
{
    if (projectId.isEmpty() || projectId.contains(QChar::Null)) {
        setError(tr("Invalid project for main document."));
        return false;
    }
    return save(server, projectId + QChar::Null + QChar::Null + QStringLiteral("compileRoot"), root);
}

QString DraftStore::loadLastFile(const QString &server, const QString &projectId)
{
    if (projectId.isEmpty() || projectId.contains(QChar::Null)) {
        setError(tr("Invalid project for last opened file."));
        return QString();
    }
    if (!load(server, projectId + QChar::Null + QChar::Null + QStringLiteral("lastFile")))
        return QString();
    return m_text;
}

bool DraftStore::saveLastFile(const QString &server, const QString &projectId, const QString &path)
{
    if (projectId.isEmpty() || projectId.contains(QChar::Null) || path.isEmpty() || path.contains(QChar::Null)) {
        setError(tr("Invalid project for last opened file."));
        return false;
    }
    return save(server, projectId + QChar::Null + QChar::Null + QStringLiteral("lastFile"), path);
}

QString DraftStore::loadLastProject(const QString &server)
{
    if (!load(server, QString(QChar::Null) + QStringLiteral("lastProject")))
        return QString();
    return m_text;
}

bool DraftStore::saveLastProject(const QString &server, const QString &projectId)
{
    if (projectId.isEmpty() || projectId.contains(QChar::Null)) {
        setError(tr("Invalid last opened project."));
        return false;
    }
    return save(server, QString(QChar::Null) + QStringLiteral("lastProject"), projectId);
}

void DraftStore::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
}

QString DraftStore::filePath(const QString &server, const QString &projectId) const
{
    if (server.isEmpty() || projectId.isEmpty())
        return QString();
    QByteArray key = server.toUtf8();
    key.append('\0');
    key.append(projectId.toUtf8());
    const QByteArray hash = QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex();
    return QDir(m_directory).filePath(QString::fromLatin1(hash) + QStringLiteral(".json"));
}

bool DraftStore::load(const QString &server, const QString &projectId)
{
    if (m_directory.isEmpty()) {
        setError(tr("Local draft storage is unavailable."));
        return false;
    }
    const QString path = filePath(server, projectId);
    if (path.isEmpty()) {
        setError(tr("Invalid project for local draft."));
        return false;
    }
    QString text;
    QString base;
    bool baseKnown = false;
    const bool exists = QFileInfo::exists(path);
    if (exists) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            setError(tr("Cannot read local draft: %1").arg(file.errorString()));
            return false;
        }
        const QByteArray data = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            setError(tr("Cannot read local draft: %1").arg(file.errorString()));
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
                || !document.object().value(QStringLiteral("text")).isString()
                || (document.object().contains(QStringLiteral("base"))
                    && !document.object().value(QStringLiteral("base")).isString())) {
            setError(tr("Local draft is damaged and cannot be opened."));
            return false;
        }
        text = document.object().value(QStringLiteral("text")).toString();
        baseKnown = document.object().contains(QStringLiteral("base"));
        base = document.object().value(QStringLiteral("base")).toString();
    }
    m_text = text;
    m_baseText = base;
    m_baseKnown = baseKnown;
    emit textChanged();
    if (m_exists != exists) {
        m_exists = exists;
        emit existsChanged();
    }
    setError(QString());
    return true;
}

bool DraftStore::save(const QString &server, const QString &projectId, const QString &text)
{
    return saveRecord(server, projectId, QJsonObject{{QStringLiteral("text"), text}});
}

bool DraftStore::saveRecord(const QString &server, const QString &projectId, const QJsonObject &record)
{
    if (m_directory.isEmpty()) {
        setError(tr("Local draft storage is unavailable."));
        return false;
    }
    const QString path = filePath(server, projectId);
    if (path.isEmpty()) {
        setError(tr("Invalid project for local draft."));
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        setError(tr("Cannot create the local drafts directory."));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("Cannot save local draft: %1").arg(file.errorString()));
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        setError(tr("Cannot protect local draft: %1").arg(file.errorString()));
        return false;
    }
    const QByteArray data = QJsonDocument(record).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size()) {
        setError(tr("Cannot save local draft: %1").arg(file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(tr("Cannot save local draft: %1").arg(file.errorString()));
        return false;
    }
    setError(QString());
    return true;
}
