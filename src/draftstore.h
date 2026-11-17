#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

class DraftStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY existsChanged)
    Q_PROPERTY(QString baseText READ baseText NOTIFY textChanged)
    Q_PROPERTY(bool baseKnown READ baseKnown NOTIFY textChanged)
public:
    explicit DraftStore(const QString &directory = QString(), QObject *parent = nullptr);
    QString text() const;
    QString error() const;
    bool exists() const;
    QString baseText() const;
    bool baseKnown() const;
    Q_INVOKABLE bool load(const QString &server, const QString &projectId);
    Q_INVOKABLE bool save(const QString &server, const QString &projectId, const QString &text);
    Q_INVOKABLE bool loadFile(const QString &server, const QString &projectId, const QString &path);
    Q_INVOKABLE bool saveFile(const QString &server, const QString &projectId, const QString &path, const QString &text);
    Q_INVOKABLE bool saveFileWithBase(const QString &server, const QString &projectId, const QString &path,
                                      const QString &text, const QString &base, bool baseKnown);
    Q_INVOKABLE QString loadCompileRoot(const QString &server, const QString &projectId);
    Q_INVOKABLE bool saveCompileRoot(const QString &server, const QString &projectId, const QString &root);
    Q_INVOKABLE QString loadLastFile(const QString &server, const QString &projectId);
    Q_INVOKABLE bool saveLastFile(const QString &server, const QString &projectId, const QString &path);
    Q_INVOKABLE QString loadLastProject(const QString &server);
    Q_INVOKABLE bool saveLastProject(const QString &server, const QString &projectId);

signals:
    void textChanged();
    void errorChanged();
    void existsChanged();

private:
    QString filePath(const QString &server, const QString &projectId) const;
    void setError(const QString &error);
    bool saveRecord(const QString &server, const QString &projectId, const QJsonObject &record);
    QString m_directory;
    QString m_text;
    QString m_baseText;
    bool m_baseKnown = false;
    QString m_error;
    bool m_exists = false;
};
