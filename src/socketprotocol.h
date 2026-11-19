#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QNetworkCookie>
#include <QUrl>
#include <QVariantList>

class QNetworkCookieJar;
bool findProjectDoc(const QJsonObject &project, const QString &path, QString *id, QString *error);
bool findProjectMainDoc(const QJsonObject &project, QString *path, QString *id, QString *error);
bool findProjectFile(const QJsonObject &project, const QString &path, QString *id, QString *error);
bool findProjectFolder(const QJsonObject &project, const QString &path, QString *id, QString *error);
bool validProjectFileTree(const QJsonObject &project);
QVariantList projectFileTreeEntries(const QJsonObject &project);
bool decodeJoinedDoc(const QJsonArray &args, QString *text, QString *error, qint64 *version = nullptr);
QJsonArray makeTextOperation(const QString &before, const QString &after);
QByteArray socketCookieHeader(const QByteArray &browserCookies, const QList<QNetworkCookie> &responseCookies,
                              const QUrl &handshakeUrl, const QUrl &socketUrl,
                              const QNetworkCookieJar *networkCookies = nullptr);
