#include "ProxyServer.h"

#include "ProxyConnection.h"

#include <QTcpSocket>

ProxyServer::ProxyServer(QObject* parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &ProxyServer::acceptPendingConnections);
}

bool ProxyServer::start(const Settings& settings, QString* errorMessage)
{
    if (m_server.isListening()) {
        stop();
    }

    m_settings = settings;
    m_lastError.clear();

    if (!m_server.listen(settings.listenAddress, settings.port)) {
        m_lastError = m_server.errorString();
        if (errorMessage != nullptr) {
            *errorMessage = m_lastError;
        }
        emit logMessage(QStringLiteral("Не удалось запустить прокси: %1").arg(m_lastError));
        emit runningChanged(false);
        return false;
    }

    emit logMessage(QStringLiteral("Прокси слушает %1:%2")
                        .arg(m_server.serverAddress().toString())
                        .arg(m_server.serverPort()));
    emit runningChanged(true);
    return true;
}

void ProxyServer::stop()
{
    if (!m_server.isListening()) {
        return;
    }

    m_server.close();
    emit logMessage(QStringLiteral("Прокси остановлен"));
    emit runningChanged(false);
}

bool ProxyServer::isRunning() const
{
    return m_server.isListening();
}

ProxyServer::Settings ProxyServer::settings() const
{
    return m_settings;
}

QString ProxyServer::lastError() const
{
    return m_lastError;
}

quint64 ProxyServer::totalUploadBytes() const
{
    return m_totalUploadBytes;
}

quint64 ProxyServer::totalDownloadBytes() const
{
    return m_totalDownloadBytes;
}

int ProxyServer::activeConnections() const
{
    return m_activeConnections;
}

quint64 ProxyServer::handledRequests() const
{
    return m_handledRequests;
}

void ProxyServer::acceptPendingConnections()
{
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        trackConnection(new ProxyConnection(socket, m_settings.outboundAddress, this));
    }
}

void ProxyServer::trackConnection(ProxyConnection* connection)
{
    ++m_activeConnections;
    emit statsChanged();

    connect(connection, &ProxyConnection::uploadBytes, this, [this](quint64 bytes) {
        m_totalUploadBytes += bytes;
        emit statsChanged();
    });

    connect(connection, &ProxyConnection::downloadBytes, this, [this](quint64 bytes) {
        m_totalDownloadBytes += bytes;
        emit statsChanged();
    });

    connect(connection, &ProxyConnection::requestStarted, this, [this](const QString& summary) {
        ++m_handledRequests;
        emit logMessage(summary);
        emit statsChanged();
    });

    connect(connection, &ProxyConnection::diagnosticMessage, this, [this](const QString& message) {
        emit logMessage(message);
    });

    connect(connection, &ProxyConnection::finished, this, [this, connection]() {
        m_activeConnections = qMax(0, m_activeConnections - 1);
        connection->deleteLater();
        emit statsChanged();
    });

    connection->start();
}
