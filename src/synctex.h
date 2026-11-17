#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVariantList>

struct SyncTeXBuild
{
    QString buildId;
    QString editorId;
    QString clsiServerId;
};

bool syncTeXBuildFromCompile(const QByteArray &response, const QString &editorId,
                             SyncTeXBuild *build, QString *error);
bool syncTeXResult(const QByteArray &response, bool toPdf, QVariantList *positions, QString *error);
QUrl syncTeXUrl(const QUrl &server, const QString &projectId, const SyncTeXBuild &build,
                const QString &file, int line, int column);
QUrl syncTeXUrl(const QUrl &server, const QString &projectId, const SyncTeXBuild &build,
                int page, double h, double v);
