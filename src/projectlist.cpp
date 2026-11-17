#include "projectlist.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVariantMap>

bool parseProjectList(const QByteArray &json, QVariantList *projects, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("Invalid project list JSON: %1").arg(parseError.errorString());
        return false;
    }

    QJsonArray records;
    if (document.isArray()) {
        records = document.array();
    } else if (document.isObject() && document.object().value(QStringLiteral("projects")).isArray()) {
        records = document.object().value(QStringLiteral("projects")).toArray();
    } else {
        *error = QStringLiteral("Unexpected project list response");
        return false;
    }

    QVariantList result;
    for (const QJsonValue &entry : records) {
        if (!entry.isObject()) {
            *error = QStringLiteral("Invalid project entry");
            return false;
        }
        const QJsonObject project = entry.toObject();
        QString id = project.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            id = project.value(QStringLiteral("_id")).toString();
        if (id.isEmpty())
            id = project.value(QStringLiteral("project_id")).toString();
        QString name = project.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = project.value(QStringLiteral("projectName")).toString();
        if (id.isEmpty() || name.isEmpty()) {
            *error = QStringLiteral("Project is missing its ID or name");
            return false;
        }
        QVariantMap item;
        item.insert(QStringLiteral("id"), id);
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("archived"), project.value(QStringLiteral("archived")).toBool());
        item.insert(QStringLiteral("trashed"), project.value(QStringLiteral("trashed")).toBool());
        result.append(item);
    }
    *projects = result;
    error->clear();
    return true;
}
