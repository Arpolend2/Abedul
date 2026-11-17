#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QVariantList>

class PdfGeometry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString error READ error NOTIFY readyChanged)
public:
    explicit PdfGeometry(QObject *parent = nullptr);
    QString path() const;
    void setPath(const QString &path);
    bool ready() const;
    QString error() const;
    Q_INVOKABLE QVariantMap page(int index) const;

signals:
    void pathChanged();
    void readyChanged();

private:
    QString m_path;
    QString m_error;
    QVariantList m_pages;
    quint64 m_generation = 0;
};
