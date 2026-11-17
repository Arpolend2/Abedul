#include "overleafhtml.h"

#include <QRegularExpression>

QString extractCsrfToken(const QString &html)
{
    return extractMetaContent(html, QStringLiteral("ol-csrfToken"));
}

QString extractMetaContent(const QString &html, const QString &name)
{
    const QRegularExpression tagExpression(
        QStringLiteral("<meta\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression nameExpression(
        QStringLiteral("\\bname\\s*=\\s*[\"']%1[\"']").arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression contentExpression(
        QStringLiteral("\\bcontent\\s*=\\s*([\"'])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator tags = tagExpression.globalMatch(html);
    while (tags.hasNext()) {
        const QString tag = tags.next().captured();
        if (!nameExpression.match(tag).hasMatch())
            continue;
        const QRegularExpressionMatch content = contentExpression.match(tag);
        if (content.hasMatch())
            return content.captured(2);
    }
    return QString();
}
