#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantList>

bool parseProjectList(const QByteArray &json, QVariantList *projects, QString *error);
