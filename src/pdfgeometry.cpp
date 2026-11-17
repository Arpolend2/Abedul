#include "pdfgeometry.h"

#include <QFutureInterface>
#include <QVariantMap>
#include <pdftaskqueue.h>
#include <pdftask.h>
#include <pdfium/fpdfview.h>
#include <pdfium/fpdf_edit.h>
#include <pdfium/fpdf_transformpage.h>

class PageGeometryTask : public PdfTask
{
public:
    PageGeometryTask(const QString &path, const QFutureInterface<QVariantMap> &result)
        : m_path(path), m_result(result) {}

    void run() override
    {
        QVariantMap result;
        QVariantList pages;
        FPDF_DOCUMENT doc = FPDF_LoadDocument(qUtf8Printable(m_path), nullptr);
        if (!doc) {
            result.insert(QStringLiteral("error"), QStringLiteral("Could not read the PDF page geometry."));
        } else {
            const int count = FPDF_GetPageCount(doc);
            for (int i = 0; i < count; ++i) {
                FS_SIZEF size;
                FPDF_PAGE page = FPDF_LoadPage(doc, i);
                if (!page || !FPDF_GetPageSizeByIndexF(doc, i, &size)
                        || size.width <= 0 || size.height <= 0) {
                    if (page)
                        FPDF_ClosePage(page);
                    result.insert(QStringLiteral("error"), QStringLiteral("Could not read the PDF page geometry."));
                    break;
                }
                const int rotation = FPDFPage_GetRotation(page);
                float left = 0, bottom = 0, right = 0, top = 0;
                float cropLeft = 0, cropBottom = 0, cropRight = 0, cropTop = 0;
                const bool hasMedia = FPDFPage_GetMediaBox(page, &left, &bottom, &right, &top);
                const bool hasCrop = FPDFPage_GetCropBox(page, &cropLeft, &cropBottom, &cropRight, &cropTop);
                const bool supported = rotation >= 0 && rotation <= 3
                    && (!hasMedia || (left == 0 && bottom == 0))
                    && (!hasCrop || (hasMedia && cropLeft == left && cropBottom == bottom
                                     && cropRight == right && cropTop == top));
                FPDF_ClosePage(page);
                pages.append(QVariantMap{{QStringLiteral("width"), size.width},
                                         {QStringLiteral("height"), size.height},
                                         {QStringLiteral("rotation"), rotation},
                                         {QStringLiteral("supported"), supported}});
            }
            FPDF_CloseDocument(doc);
            if (count < 1)
                result.insert(QStringLiteral("error"), QStringLiteral("The PDF has no pages."));
        }
        result.insert(QStringLiteral("pages"), pages);
        m_result.reportFinished(&result);
    }

    void cancel() override {}
    int id() const override { return -1; }

private:
    QString m_path;
    QFutureInterface<QVariantMap> m_result;
};

PdfGeometry::PdfGeometry(QObject *parent) : QObject(parent) {}
QString PdfGeometry::path() const { return m_path; }
bool PdfGeometry::ready() const { return !m_pages.isEmpty() && m_error.isEmpty(); }
QString PdfGeometry::error() const { return m_error; }
QVariantMap PdfGeometry::page(int index) const
{
    return index >= 0 && index < m_pages.size() ? m_pages.at(index).toMap() : QVariantMap();
}

void PdfGeometry::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    const quint64 generation = ++m_generation;
    m_pages.clear();
    m_error.clear();
    emit pathChanged();
    emit readyChanged();
    if (path.isEmpty())
        return;
    QFutureInterface<QVariantMap> result;
    result.reportStarted();
    auto watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, generation]() {
        const QVariantMap result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_error = result.value(QStringLiteral("error")).toString();
        m_pages = result.value(QStringLiteral("pages")).toList();
        emit readyChanged();
    });
    watcher->setFuture(result.future());
    if (!PdfTaskQueue::instance().addTask(QSharedPointer<PdfTask>(new PageGeometryTask(path, result)),
                                           PdfTaskQueue::TaskPriority::High)) {
        watcher->deleteLater();
        m_error = tr("Could not schedule PDF geometry loading.");
        emit readyChanged();
    }
}
