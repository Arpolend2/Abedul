#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

class QNetworkReply;
class QJsonArray;
class QUrl;
class QTimer;

class OpenRouterClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString authorizationUrl READ authorizationUrl NOTIFY authorizationUrlChanged)
    Q_PROPERTY(bool aiEnabled READ aiEnabled WRITE setAiEnabled NOTIFY aiEnabledChanged)

public:
    explicit OpenRouterClient(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
    bool authenticated() const { return !m_key.isEmpty(); }
    bool busy() const { return !m_reply.isNull() || !m_retryTimer.isNull(); }
    QString error() const { return m_error; }
    QString authorizationUrl() const { return m_authorizationUrl; }
    bool aiEnabled() const;
    void setAiEnabled(bool enabled);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void beginSignIn();
    Q_INVOKABLE void finishSignIn(const QString &code);
    Q_INVOKABLE bool handleAuthorizationUrl(const QUrl &url);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void request(const QString &instruction, const QString &source);
    Q_INVOKABLE void requestCompletion(const QString &before, const QString &after);
    Q_INVOKABLE void requestEdit(const QString &instruction, const QString &source);

signals:
    void authenticatedChanged();
    void busyChanged();
    void errorChanged();
    void authorizationUrlChanged();
    void aiEnabledChanged();
    void resultReady(const QString &text);

private:
    void setError(const QString &error);
    bool saveKey(const QByteArray &key, QString *error);
    void loadKey();
    void checkQuota(const QString &priorError, const QJsonArray &messages, bool retry, bool completion,
                    int completionAttempt, int rateLimitCount, int retryDelay);
    void startRequest(QNetworkReply *reply, bool authentication, const QJsonArray &messages, bool retry,
                      bool completion = false, bool quotaCheck = false, int completionAttempt = 0,
                      const QString &priorError = QString(), int rateLimitCount = 0, int retryDelay = 0);
    void postMessages(const QJsonArray &messages, bool retry = true, bool completion = false,
                      int completionAttempt = 0, int rateLimitCount = 0);

    QNetworkAccessManager m_ownedNetwork;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QTimer> m_retryTimer;
    QByteArray m_key;
    QByteArray m_verifier;
    QByteArray m_state;
    QString m_error;
    QString m_authorizationUrl;
};
