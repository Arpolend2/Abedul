#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QNetworkCookie>
#include <QUrl>

class QNetworkCookieJar;
bool findProjectDoc(const QJsonObject &project, const QString &path, QString *id, QString *error);
bool findProjectFile(const QJsonObject &project, const QString &path, QString *id, QString *error);
bool decodeJoinedDoc(const QJsonArray &args, QString *text, QString *error, qint64 *version = nullptr);
QJsonArray makeTextOperation(const QString &before, const QString &after);
QByteArray socketCookieHeader(const QByteArray &browserCookies, const QList<QNetworkCookie> &responseCookies,
                              const QUrl &handshakeUrl, const QUrl &socketUrl,
                              const QNetworkCookieJar *networkCookies = nullptr);
