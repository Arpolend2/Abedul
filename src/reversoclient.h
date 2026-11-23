#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>

class QNetworkReply;

class ReversoClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList translations READ translations NOTIFY translationsChanged)
    Q_PROPERTY(QVariantList examples READ examples NOTIFY examplesChanged)

public:
    explicit ReversoClient(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);

    bool busy() const { return !m_reply.isNull(); }
    QString error() const { return m_error; }
    QStringList translations() const { return m_translations; }
    QVariantList examples() const { return m_examples; }

    Q_INVOKABLE void request(const QString &text, const QString &source, const QString &target);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString wordAtCursor(const QString &text, int position) const;
    Q_INVOKABLE QString plainTextAtCursor(const QString &text, int position,
                                           int selectionStart, int selectionEnd) const;

signals:
    void busyChanged();
    void errorChanged();
    void translationsChanged();
    void examplesChanged();

private:
    void setError(const QString &error);
    void setTranslations(const QStringList &translations);
    void setExamples(const QVariantList &examples);
    void startRequest(const QString &input, const QString &source, const QString &target,
                      bool sentence, bool contextOnly);

    QNetworkAccessManager m_ownedNetwork;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_error;
    QStringList m_translations;
    QVariantList m_examples;
};
