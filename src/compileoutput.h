#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

bool compilePdfUrl(const QByteArray &response, const QUrl &server,
                   QUrl *url, bool *sendCookies, QString *error);
bool compileRequestBody(const QString &rootPath, QByteArray *body, QString *error,
                        const QString &editorId = QString());
QString compileServerError(const QByteArray &response);
