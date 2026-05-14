#include "ProxyConnection.h"

#include <QRegularExpression>
#include <QStringDecoder>

namespace
{
constexpr qsizetype MaxHeaderBytes = 64 * 1024;

QByteArray reasonBody(const QString& detail)
{
    const QByteArray safeDetail = detail.toHtmlEscaped().toUtf8();
    return QByteArray("<!doctype html><html><body><h1>vrpProxy</h1><p>")
        + safeDetail
        + QByteArray("</p></body></html>");
}

QByteArray withHttpError(const QByteArray& statusLine, const QString& detail)
{
    const QByteArray body = reasonBody(detail);
    return statusLine + QByteArray("\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ")
        + QByteArray::number(body.size())
        + QByteArray("\r\nConnection: close\r\n\r\n")
        + body;
}

QByteArray localStatusResponse()
{
    const QByteArray body =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>vrpProxy</title>"
        "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:40px;background:#111;color:#eee}"
        "code{background:#222;padding:2px 6px;border-radius:4px}</style></head>"
        "<body><h1>vrpProxy is reachable</h1>"
        "<p>The headset can reach this PC proxy port.</p>"
        "<p>Now set the same host and port as the Wi-Fi proxy.</p></body></html>";

    return QByteArray("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ")
        + QByteArray::number(body.size())
        + QByteArray("\r\nConnection: close\r\n\r\n")
        + body;
}

QByteArray headerValue(const QByteArray& line)
{
    const int colon = line.indexOf(':');
    if (colon < 0) {
        return {};
    }
    return line.mid(colon + 1).trimmed();
}
}

ProxyConnection::ProxyConnection(QTcpSocket* clientSocket, QHostAddress outboundAddress, QObject* parent)
    : QObject(parent)
    , m_client(clientSocket)
    , m_outboundAddress(outboundAddress)
{
    m_client->setParent(this);
}

void ProxyConnection::start()
{
    connect(m_client, &QTcpSocket::readyRead, this, &ProxyConnection::readClientHeader);
    connect(m_client, &QTcpSocket::disconnected, this, &ProxyConnection::closeBoth);
    connect(m_client, &QTcpSocket::errorOccurred, this, &ProxyConnection::closeBoth);

    readClientHeader();
}

void ProxyConnection::readClientHeader()
{
    if (m_mode != Mode::ReadingHeader) {
        relayClientToRemote();
        return;
    }

    m_headerBuffer += m_client->readAll();
    if (!m_headerBuffer.isEmpty() && static_cast<quint8>(m_headerBuffer.at(0)) == 0x05) {
        processSocks5Data();
        return;
    }

    if (m_headerBuffer.size() > MaxHeaderBytes) {
        failClient("HTTP/1.1 431 Request Header Fields Too Large", QStringLiteral("Слишком большой HTTP-заголовок."));
        return;
    }

    const int end = m_headerBuffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return;
    }

    const QByteArray packet = m_headerBuffer.left(end + 4);
    const QByteArray pending = m_headerBuffer.mid(end + 4);
    m_headerBuffer.clear();

    if (serveLocalStatusIfNeeded(packet)) {
        return;
    }

    ParsedRequest request = parseHeader(packet);
    request.pendingBody = pending;

    if (!request.valid) {
        failClient("HTTP/1.1 400 Bad Request", QStringLiteral("Не удалось разобрать запрос прокси."));
        return;
    }

    m_connectTunnel = request.isConnect;
    m_pendingRemoteBytes = request.isConnect ? request.pendingBody : request.rewrittenHeader + request.pendingBody;

    emit requestStarted(QStringLiteral("%1 %2:%3")
                            .arg(request.method, request.host)
                            .arg(request.port));
    connectRemote(request);
}

void ProxyConnection::processSocks5Data()
{
    if (!m_socks5HandshakeDone) {
        if (m_headerBuffer.size() < 2) {
            return;
        }

        const int methodCount = static_cast<quint8>(m_headerBuffer.at(1));
        if (m_headerBuffer.size() < 2 + methodCount) {
            return;
        }

        bool noAuthSupported = false;
        for (int i = 0; i < methodCount; ++i) {
            if (static_cast<quint8>(m_headerBuffer.at(2 + i)) == 0x00) {
                noAuthSupported = true;
                break;
            }
        }

        if (!noAuthSupported) {
            m_client->write(QByteArray::fromHex("05ff"));
            m_client->flush();
            m_client->waitForBytesWritten(1200);
            closeBoth();
            return;
        }

        m_client->write(QByteArray::fromHex("0500"));
        m_headerBuffer.remove(0, 2 + methodCount);
        m_socks5HandshakeDone = true;
    }

    if (m_headerBuffer.isEmpty()) {
        return;
    }
    if (m_headerBuffer.size() < 4) {
        return;
    }

    const quint8 version = static_cast<quint8>(m_headerBuffer.at(0));
    const quint8 command = static_cast<quint8>(m_headerBuffer.at(1));
    const quint8 atyp = static_cast<quint8>(m_headerBuffer.at(3));
    if (version != 0x05) {
        failSocks5(0x01, QStringLiteral("Некорректная SOCKS5 версия."));
        return;
    }
    if (command != 0x01) {
        failSocks5(0x07, QStringLiteral("SOCKS5 поддерживает только CONNECT."));
        return;
    }

    int offset = 4;
    QString host;
    if (atyp == 0x01) {
        if (m_headerBuffer.size() < offset + 4 + 2) {
            return;
        }
        const auto a = reinterpret_cast<const uchar*>(m_headerBuffer.constData() + offset);
        host = QStringLiteral("%1.%2.%3.%4").arg(a[0]).arg(a[1]).arg(a[2]).arg(a[3]);
        offset += 4;
    } else if (atyp == 0x03) {
        if (m_headerBuffer.size() < offset + 1) {
            return;
        }
        const int length = static_cast<quint8>(m_headerBuffer.at(offset));
        ++offset;
        if (m_headerBuffer.size() < offset + length + 2) {
            return;
        }
        host = QString::fromUtf8(m_headerBuffer.mid(offset, length));
        offset += length;
    } else if (atyp == 0x04) {
        if (m_headerBuffer.size() < offset + 16 + 2) {
            return;
        }
        Q_IPV6ADDR ipv6 {};
        memcpy(ipv6.c, m_headerBuffer.constData() + offset, 16);
        host = QHostAddress(ipv6).toString();
        offset += 16;
    } else {
        failSocks5(0x08, QStringLiteral("SOCKS5 address type не поддерживается."));
        return;
    }

    const auto portBytes = reinterpret_cast<const uchar*>(m_headerBuffer.constData() + offset);
    const quint16 port = static_cast<quint16>((portBytes[0] << 8) | portBytes[1]);
    offset += 2;

    ParsedRequest request;
    request.valid = !host.isEmpty() && port > 0;
    request.isConnect = true;
    request.method = QStringLiteral("SOCKS5");
    request.host = host;
    request.port = port;
    if (!request.valid) {
        failSocks5(0x04, QStringLiteral("Некорректный SOCKS5 адрес."));
        return;
    }

    m_socks5Tunnel = true;
    m_connectTunnel = false;
    m_pendingRemoteBytes = m_headerBuffer.mid(offset);
    m_headerBuffer.clear();

    emit requestStarted(QStringLiteral("SOCKS5 %1:%2").arg(host).arg(port));
    connectRemote(request);
}

void ProxyConnection::failSocks5(quint8 replyCode, const QString& detail)
{
    if (m_mode == Mode::Closed) {
        return;
    }

    emit diagnosticMessage(detail);
    QByteArray response;
    response.append(char(0x05));
    response.append(char(replyCode));
    response.append(char(0x00));
    response.append(char(0x01));
    response.append(QByteArray::fromHex("000000000000"));
    m_mode = Mode::Closed;
    m_client->write(response);
    m_client->flush();
    m_client->waitForBytesWritten(1200);
    m_client->disconnectFromHost();
    if (m_remote != nullptr) {
        m_remote->disconnectFromHost();
    }
    finishOnce();
}

ProxyConnection::ParsedRequest ProxyConnection::parseHeader(const QByteArray& packet) const
{
    ParsedRequest request;
    const QList<QByteArray> lines = packet.split('\n');
    if (lines.isEmpty()) {
        return request;
    }

    QByteArray firstLine = lines.first().trimmed();
    const QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 3) {
        return request;
    }

    request.method = QString::fromLatin1(parts.at(0)).toUpper();
    request.firstLine = firstLine;

    if (request.method == QStringLiteral("CONNECT")) {
        const QByteArray authority = parts.at(1).trimmed();
        const int colon = authority.lastIndexOf(':');
        if (colon <= 0) {
            return request;
        }

        request.host = QString::fromLatin1(authority.left(colon));
        bool ok = false;
        const int parsedPort = authority.mid(colon + 1).toInt(&ok);
        if (!ok || parsedPort <= 0 || parsedPort > 65535) {
            return request;
        }

        request.port = static_cast<quint16>(parsedPort);
        request.isConnect = true;
        request.valid = true;
        return request;
    }

    QUrl url(QString::fromUtf8(parts.at(1)));
    QByteArray path;
    if (url.isValid() && !url.host().isEmpty()) {
        request.host = url.host();
        request.port = static_cast<quint16>(url.port(url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ? 443 : 80));
        path = url.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority);
        if (path.isEmpty()) {
            path = "/";
        }
    } else {
        if (!parseHostHeader(lines, &request.host, &request.port)) {
            return request;
        }
        path = parts.at(1);
    }

    QByteArray rewritten;
    rewritten += parts.at(0);
    rewritten += ' ';
    rewritten += path;
    rewritten += ' ';
    rewritten += parts.at(2);
    rewritten += "\r\n";

    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines.at(i);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }

        const QByteArray lower = line.toLower();
        if (lower.startsWith("proxy-connection:")) {
            continue;
        }
        if (lower.startsWith("connection:")) {
            continue;
        }
        rewritten += line;
        rewritten += "\r\n";
    }

    rewritten += "Connection: close\r\n\r\n";
    request.rewrittenHeader = rewritten;
    request.isConnect = false;
    request.valid = !request.host.isEmpty() && request.port > 0;
    return request;
}

bool ProxyConnection::parseHostHeader(const QList<QByteArray>& headerLines, QString* host, quint16* port) const
{
    for (const QByteArray& rawLine : headerLines) {
        QByteArray line = rawLine;
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (!line.toLower().startsWith("host:")) {
            continue;
        }

        QByteArray value = headerValue(line);
        if (value.isEmpty()) {
            return false;
        }

        quint16 parsedPort = 80;
        if (value.startsWith('[')) {
            const int closing = value.indexOf(']');
            if (closing <= 0) {
                return false;
            }
            *host = QString::fromLatin1(value.mid(1, closing - 1));
            if (value.mid(closing + 1).startsWith(':')) {
                bool ok = false;
                const int p = value.mid(closing + 2).toInt(&ok);
                if (!ok || p <= 0 || p > 65535) {
                    return false;
                }
                parsedPort = static_cast<quint16>(p);
            }
        } else {
            const int colon = value.lastIndexOf(':');
            if (colon > 0) {
                bool ok = false;
                const int p = value.mid(colon + 1).toInt(&ok);
                if (ok && p > 0 && p <= 65535) {
                    parsedPort = static_cast<quint16>(p);
                    value = value.left(colon);
                }
            }
            *host = QString::fromLatin1(value);
        }

        *port = parsedPort;
        return !host->isEmpty();
    }

    return false;
}

bool ProxyConnection::serveLocalStatusIfNeeded(const QByteArray& packet)
{
    const QList<QByteArray> lines = packet.split('\n');
    if (lines.isEmpty()) {
        return false;
    }

    const QList<QByteArray> parts = lines.first().trimmed().split(' ');
    if (parts.size() < 2) {
        return false;
    }

    const QByteArray method = parts.at(0).toUpper();
    const QByteArray target = parts.at(1).trimmed();
    if (method == "CONNECT" || !target.startsWith('/')) {
        return false;
    }

    emit requestStarted(QStringLiteral("LOCAL status page"));
    const QByteArray response = localStatusResponse();
    m_mode = Mode::Closed;
    m_client->write(response);
    emit downloadBytes(static_cast<quint64>(response.size()));
    m_client->flush();
    m_client->waitForBytesWritten(1200);
    m_client->disconnectFromHost();
    finishOnce();
    return true;
}

void ProxyConnection::connectRemote(const ParsedRequest& request)
{
    m_mode = Mode::Connecting;
    m_remote = new QTcpSocket(this);

    connect(m_remote, &QTcpSocket::connected, this, &ProxyConnection::flushPendingAfterConnect);
    connect(m_remote, &QTcpSocket::readyRead, this, &ProxyConnection::relayRemoteToClient);
    connect(m_remote, &QTcpSocket::disconnected, this, &ProxyConnection::closeBoth);
    connect(m_remote, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_mode == Mode::Connecting) {
            if (m_socks5Tunnel) {
                failSocks5(0x05, m_remote->errorString());
                return;
            }
            failClient("HTTP/1.1 502 Bad Gateway", m_remote->errorString());
            return;
        }
        closeBoth();
    });

    if (!m_outboundAddress.isNull() && !m_remote->bind(m_outboundAddress, 0)) {
        failClient("HTTP/1.1 502 Bad Gateway",
                   QStringLiteral("Не удалось привязать исходящее соединение к %1: %2")
                       .arg(m_outboundAddress.toString(), m_remote->errorString()));
        return;
    }

    m_remote->connectToHost(request.host, request.port);
}

void ProxyConnection::flushPendingAfterConnect()
{
    if (m_mode == Mode::Closed) {
        return;
    }

    disconnect(m_client, &QTcpSocket::readyRead, this, &ProxyConnection::readClientHeader);
    connect(m_client, &QTcpSocket::readyRead, this, &ProxyConnection::relayClientToRemote);

    if (m_connectTunnel) {
        static constexpr char Connected[] = "HTTP/1.1 200 Connection Established\r\nProxy-Agent: vrpProxy\r\n\r\n";
        m_client->write(Connected);
    } else if (m_socks5Tunnel) {
        m_client->write(QByteArray::fromHex("05000001000000000000"));
    }

    if (!m_pendingRemoteBytes.isEmpty()) {
        const qint64 written = m_remote->write(m_pendingRemoteBytes);
        if (written > 0) {
            emit uploadBytes(static_cast<quint64>(written));
        }
        m_pendingRemoteBytes.clear();
    }

    m_mode = Mode::Relaying;
    relayClientToRemote();
    relayRemoteToClient();
}

void ProxyConnection::relayClientToRemote()
{
    if (m_mode != Mode::Relaying || m_remote == nullptr) {
        return;
    }

    const QByteArray data = m_client->readAll();
    if (data.isEmpty()) {
        return;
    }

    const qint64 written = m_remote->write(data);
    if (written > 0) {
        emit uploadBytes(static_cast<quint64>(written));
    }
}

void ProxyConnection::relayRemoteToClient()
{
    if (m_remote == nullptr) {
        return;
    }

    const QByteArray data = m_remote->readAll();
    if (data.isEmpty()) {
        return;
    }

    const qint64 written = m_client->write(data);
    if (written > 0) {
        emit downloadBytes(static_cast<quint64>(written));
    }
}

void ProxyConnection::failClient(const QByteArray& statusLine, const QString& detail)
{
    if (m_mode == Mode::Closed) {
        return;
    }

    m_mode = Mode::Closed;
    emit diagnosticMessage(detail);
    m_client->write(withHttpError(statusLine, detail));
    m_client->flush();
    m_client->waitForBytesWritten(1200);
    m_client->disconnectFromHost();
    if (m_remote != nullptr) {
        m_remote->disconnectFromHost();
    }
    finishOnce();
}

void ProxyConnection::closeBoth()
{
    if (m_mode == Mode::Closed) {
        return;
    }

    m_mode = Mode::Closed;
    if (m_client != nullptr && m_client->state() != QAbstractSocket::UnconnectedState) {
        m_client->disconnectFromHost();
    }
    if (m_remote != nullptr && m_remote->state() != QAbstractSocket::UnconnectedState) {
        m_remote->disconnectFromHost();
    }
    finishOnce();
}

void ProxyConnection::finishOnce()
{
    if (m_finished) {
        return;
    }

    m_finished = true;
    emit finished();
}
