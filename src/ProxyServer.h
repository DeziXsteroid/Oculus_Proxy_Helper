#pragma once

#include <QHostAddress>
#include <QObject>
#include <QTcpServer>

class ProxyConnection;

class ProxyServer final : public QObject
{
    Q_OBJECT

public:
    struct Settings
    {
        QHostAddress listenAddress = QHostAddress::AnyIPv4;
        quint16 port = 8080;
        QHostAddress outboundAddress;
    };

    explicit ProxyServer(QObject* parent = nullptr);

    bool start(const Settings& settings, QString* errorMessage = nullptr);
    void stop();

    bool isRunning() const;
    Settings settings() const;
    QString lastError() const;

    quint64 totalUploadBytes() const;
    quint64 totalDownloadBytes() const;
    int activeConnections() const;
    quint64 handledRequests() const;

signals:
    void runningChanged(bool running);
    void statsChanged();
    void logMessage(const QString& message);

private slots:
    void acceptPendingConnections();

private:
    void trackConnection(ProxyConnection* connection);

    QTcpServer m_server;
    Settings m_settings;
    QString m_lastError;
    quint64 m_totalUploadBytes = 0;
    quint64 m_totalDownloadBytes = 0;
    quint64 m_handledRequests = 0;
    int m_activeConnections = 0;
};
