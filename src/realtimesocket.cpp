#include "realtimesocket.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTextCodec>
#include <QRegularExpression>

namespace {
const quint64 maxMessageSize = 16 * 1024 * 1024;
}

RealtimeSocket::RealtimeSocket(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QSslSocket::readyRead, this, &RealtimeSocket::readData);
    connect(&m_socket, static_cast<void (QSslSocket::*)(QAbstractSocket::SocketError)>(&QSslSocket::error),
            this, [this](QAbstractSocket::SocketError) { fail(m_socket.errorString()); });
    connect(&m_socket, &QSslSocket::disconnected, this, [this]() { fail(tr("Connection closed.")); });
}

QByteArray RealtimeSocket::randomBytes(int count)
{
    QFile random(QStringLiteral("/dev/urandom"));
    if (!random.open(QIODevice::ReadOnly))
        return QByteArray();
    return random.read(count);
}

void RealtimeSocket::open(const QUrl &url, const QByteArray &cookie, const QByteArray &origin)
{
    const QByteArray nonce = randomBytes(16);
    if (nonce.size() != 16 || cookie.contains('\r') || cookie.contains('\n')
            || origin.contains('\r') || origin.contains('\n')) {
        fail(tr("Cannot prepare the real-time connection."));
        return;
    }
    const QByteArray key = nonce.toBase64();
    m_accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                       QCryptographicHash::Sha1).toBase64();
    connect(&m_socket, &QSslSocket::encrypted, this, [this, url, key, cookie, origin]() {
        QByteArray host = url.host().toUtf8();
        if (host.contains(':'))
            host = '[' + host + ']';
        if (url.port(443) != 443)
            host += ':' + QByteArray::number(url.port());
        QByteArray resource = url.path(QUrl::FullyEncoded).toUtf8();
        if (url.hasQuery())
            resource += '?' + url.query(QUrl::FullyEncoded).toUtf8();
        QByteArray headers = "GET " + resource + " HTTP/1.1\r\nHost: " + host
                + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                + "Sec-WebSocket-Key: " + key + "\r\nOrigin: " + origin + "\r\n";
        if (!cookie.isEmpty())
            headers += "Cookie: " + cookie + "\r\n";
        m_socket.write(headers + "\r\n");
    });
    m_socket.connectToHostEncrypted(url.host(), static_cast<quint16>(url.port(443)));
}

void RealtimeSocket::fail(const QString &message)
{
    if (m_failed)
        return;
    m_failed = true;
    m_socket.abort();
    emit failed(message);
}

void RealtimeSocket::abort()
{
    m_failed = true;
    m_socket.abort();
}

void RealtimeSocket::sendText(const QString &message)
{
    sendFrame(1, message.toUtf8());
}

void RealtimeSocket::sendFrame(quint8 opcode, const QByteArray &data)
{
    if (!m_upgraded || m_failed || static_cast<quint64>(data.size()) > maxMessageSize) {
        fail(tr("Cannot send real-time message."));
        return;
    }
    const QByteArray mask = randomBytes(4);
    if (mask.size() != 4) {
        fail(tr("Cannot generate a real-time message mask."));
        return;
    }
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));
    const quint64 size = static_cast<quint64>(data.size());
    if (size < 126) {
        frame.append(static_cast<char>(0x80 | size));
    } else if (size <= 65535) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((size >> 8) & 0xff));
        frame.append(static_cast<char>(size & 0xff));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.append(static_cast<char>((size >> shift) & 0xff));
    }
    frame += mask;
    for (int i = 0; i < data.size(); ++i)
        frame.append(data.at(i) ^ mask.at(i % 4));
    m_socket.write(frame);
}

void RealtimeSocket::readData()
{
    m_buffer += m_socket.readAll();
    if (!m_upgraded) {
        const int end = m_buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (m_buffer.size() > 16384)
                fail(tr("Invalid real-time handshake."));
            return;
        }
        if (end > 16384) {
            fail(tr("Invalid real-time handshake."));
            return;
        }
        const QList<QByteArray> headers = m_buffer.left(end).split('\n');
        m_buffer.remove(0, end + 4);
        bool upgrade = false, connection = false, accept = false;
        const QString status = headers.isEmpty() ? QString() : QString::fromLatin1(headers.first().trimmed());
        const QRegularExpressionMatch statusMatch = QRegularExpression(
                    QStringLiteral("^HTTP/1\\.[01] ([0-9]{3})(?: .*)?$")).match(status);
        if (!statusMatch.hasMatch() || statusMatch.captured(1) != QStringLiteral("101")) {
            fail(statusMatch.hasMatch()
                 ? tr("Real-time upgrade was rejected (HTTP %1).").arg(statusMatch.captured(1))
                 : tr("Invalid real-time upgrade response."));
            return;
        }
        for (int i = 1; i < headers.size(); ++i) {
            const QByteArray header = headers.at(i).trimmed();
            const int colon = header.indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray name = header.left(colon).trimmed().toLower();
            const QByteArray value = header.mid(colon + 1).trimmed();
            if (name == "upgrade")
                upgrade = value.toLower() == "websocket";
            else if (name == "connection") {
                for (const QByteArray &token : value.toLower().split(','))
                    connection = connection || token.trimmed() == "upgrade";
            } else if (name == "sec-websocket-accept")
                accept = value == m_accept;
        }
        if (!upgrade || !connection || !accept) {
            fail(tr("Invalid real-time upgrade response."));
            return;
        }
        m_upgraded = true;
    }
    while (!m_failed && m_buffer.size() >= 2) {
        const quint8 first = static_cast<quint8>(m_buffer.at(0));
        const quint8 second = static_cast<quint8>(m_buffer.at(1));
        const quint8 opcode = first & 0x0f;
        const bool final = first & 0x80;
        if ((first & 0x70) || (second & 0x80)) {
            fail(tr("Invalid real-time frame."));
            return;
        }
        quint64 size = second & 0x7f;
        int headerSize = 2;
        if (size == 126 || size == 127) {
            const int lengthBytes = size == 126 ? 2 : 8;
            if (m_buffer.size() < 2 + lengthBytes)
                return;
            size = 0;
            for (int i = 0; i < lengthBytes; ++i)
                size = (size << 8) | static_cast<quint8>(m_buffer.at(2 + i));
            headerSize += lengthBytes;
        }
        if (size > maxMessageSize || (opcode >= 8 && (!final || size > 125))
                || size + static_cast<quint64>(m_fragments.size()) > maxMessageSize) {
            fail(tr("Real-time message is too large or invalid."));
            return;
        }
        if (static_cast<quint64>(m_buffer.size() - headerSize) < size)
            return;
        const QByteArray payload = m_buffer.mid(headerSize, static_cast<int>(size));
        m_buffer.remove(0, headerSize + static_cast<int>(size));
        if (opcode == 8) {
            fail(tr("Real-time connection was closed by the server."));
            return;
        }
        if (opcode == 9) {
            sendFrame(10, payload);
            continue;
        }
        if (opcode == 10)
            continue;
        if ((opcode == 1 && m_fragmented) || (opcode == 0 && !m_fragmented)
                || (opcode != 0 && opcode != 1)) {
            fail(tr("Unsupported real-time frame."));
            return;
        }
        m_fragments += payload;
        m_fragmented = !final;
        if (final) {
            QTextCodec::ConverterState state;
            const QString text = QTextCodec::codecForName("UTF-8")->toUnicode(
                        m_fragments.constData(), m_fragments.size(), &state);
            m_fragments.clear();
            if (state.invalidChars || state.remainingChars) {
                fail(tr("Invalid real-time message encoding."));
                return;
            }
            emit textMessageReceived(text);
        }
    }
}
