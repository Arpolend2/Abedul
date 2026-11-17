#pragma once

#include <QObject>
#include <QSslSocket>
#include <QUrl>

class RealtimeSocket : public QObject
{
    Q_OBJECT
public:
    explicit RealtimeSocket(QObject *parent = nullptr);
    void open(const QUrl &url, const QByteArray &cookie, const QByteArray &origin);
    void sendText(const QString &message);
    void abort();

signals:
    void textMessageReceived(const QString &message);
    void failed(const QString &message);

private:
    void readData();
    void sendFrame(quint8 opcode, const QByteArray &data);
    void fail(const QString &message);
    QByteArray randomBytes(int count);

    QSslSocket m_socket;
    QByteArray m_buffer;
    QByteArray m_fragments;
    QByteArray m_accept;
    bool m_upgraded = false;
    bool m_fragmented = false;
    bool m_failed = false;
};
