#pragma once

#include <QString>

QString extractCsrfToken(const QString &html);
QString extractMetaContent(const QString &html, const QString &name);
