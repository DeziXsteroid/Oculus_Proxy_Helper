#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>
#include <QUrl>

class ProxyConnection final : public QObject
{
    Q_OBJECT

public:
    ProxyConnection(QTcpSocket* clientSocket, QHostAddress outboundAddress, QObject* parent = nullptr);

    void start();

signals:
    void uploadBytes(quint64 bytes);
    void downloadBytes(quint64 bytes);
    void requestStarted(const QString& summary);
    void diagnosticMessage(const QString& message);
    void finished();

private:
    enum class Mode
    {
        ReadingHeader,
        Connecting,
        Relaying,
        Closed
    };

    struct ParsedRequest
    {
        bool valid = false;
        bool isConnect = false;
        QString method;
        QString host;
        quint16 port = 0;
        QByteArray firstLine;
        QByteArray rewrittenHeader;
        QByteArray pendingBody;
    };

    void readClientHeader();
    void processSocks5Data();
    void failSocks5(quint8 replyCode, const QString& detail);
    ParsedRequest parseHeader(const QByteArray& packet) const;
    bool parseHostHeader(const QList<QByteArray>& headerLines, QString* host, quint16* port) const;
    bool serveLocalStatusIfNeeded(const QByteArray& packet);
    void connectRemote(const ParsedRequest& request);
    void flushPendingAfterConnect();
    void relayClientToRemote();
    void relayRemoteToClient();
    void failClient(const QByteArray& statusLine, const QString& detail);
    void closeBoth();
    void finishOnce();

    QTcpSocket* m_client = nullptr;
    QTcpSocket* m_remote = nullptr;
    QHostAddress m_outboundAddress;
    QByteArray m_headerBuffer;
    QByteArray m_pendingRemoteBytes;
    bool m_connectTunnel = false;
    bool m_socks5Tunnel = false;
    bool m_socks5HandshakeDone = false;
    bool m_finished = false;
    Mode m_mode = Mode::ReadingHeader;
};
