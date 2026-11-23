#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

class QNetworkReply;

class WolframAlphaClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appId READ appId WRITE setAppId NOTIFY appIdChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY appIdChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)

public:
    explicit WolframAlphaClient(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);

    QString appId() const { return m_appId; }
    bool configured() const { return !m_appId.isEmpty(); }
    bool busy() const { return !m_reply.isNull(); }
    QString error() const { return m_error; }
    QString result() const { return m_result; }

    void setAppId(const QString &appId);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void request(const QString &equation);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString equationAtCursor(const QString &source, int cursorPosition) const;

signals:
    void appIdChanged();
    void busyChanged();
    void errorChanged();
    void resultChanged();
    void resultReady(const QString &result);

private:
    void loadAppId();
    bool saveAppId(const QString &appId, QString *error);
    void setError(const QString &error);
    void setResult(const QString &result);

    QNetworkAccessManager m_ownedNetwork;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_appId;
    QString m_error;
    QString m_result;
};
