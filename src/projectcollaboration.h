#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

class CollaborationEntriesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit CollaborationEntriesModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantList entries() const { return m_entries; }
    void reset(const QVariantList &entries);
    void update(int index, const QVariantMap &entry);

private:
    QVariantList m_entries;
};

class ProjectCollaboration : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* entriesModel READ entriesModel CONSTANT)
public:
    ProjectCollaboration(QNetworkAccessManager *network, const QUrl *server,
                         const QByteArray *cookie, QObject *parent = nullptr);
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    QVariantList entries() const { return m_entriesModel.entries(); }
    QAbstractItemModel *entriesModel() { return &m_entriesModel; }

    Q_INVOKABLE void open(const QString &projectId, const QString &mode);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void submit(const QString &text, const QString &target, bool readOnly);
    Q_INVOKABLE void changeAccess(const QString &id, bool readOnly);
    Q_INVOKABLE void removeMember(const QString &id);
    Q_INVOKABLE void revokeInvite(const QString &id);
    Q_INVOKABLE void resendInvite(const QString &id);
    Q_INVOKABLE void close();
signals:
    void changed();
    void submitted();
private:
    typedef std::function<void(const QByteArray &)> Handler;
    void request(const QString &path, const QByteArray &body, const Handler &handler,
                 const QByteArray &csrf = QByteArray(), const QByteArray &method = QByteArray());
    void manageSharing(const QString &id, const QString &kind, const QByteArray &method,
                       const QByteArray &body = QByteArray());
    void fail(const QString &message);
    bool validSession() const;
    bool validId(const QString &id) const;
    void parseEntries(const QByteArray &data);
    void fetchNextHistoryDiff();
    void cancelHistoryDiffs();

    QNetworkAccessManager *m_network;
    const QUrl *m_server;
    const QByteArray *m_cookie;
    QUrl m_openServer;
    QByteArray m_openCookie;
    QString m_project, m_mode, m_error, m_notice;
    CollaborationEntriesModel m_entriesModel;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_historyReply;
    QVariantList m_historyQueue;
    quint64 m_historyGeneration = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
};
