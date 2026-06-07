#include "ws-client.hpp"
#include "version.h"

#include <obs-module.h>

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSslConfiguration>
#include <QUrl>
#include <QUrlQuery>

#include <cstring>

namespace tiktok {

static constexpr quint8 OP_CONT   = 0x0;
static constexpr quint8 OP_TEXT   = 0x1;
static constexpr quint8 OP_BINARY = 0x2;
static constexpr quint8 OP_CLOSE  = 0x8;
static constexpr quint8 OP_PING   = 0x9;
static constexpr quint8 OP_PONG   = 0xA;

// Hard cap on a single WS frame payload + the running rx buffer. Without
// these a malicious / buggy upstream sending a 64-bit length header could
// drive the QByteArray into OOM territory. Captions transcripts are <2 KB.
static constexpr qint64 MAX_PAYLOAD_BYTES   = 1 * 1024 * 1024; // 1 MB
static constexpr qint64 MAX_RX_BUFFER_BYTES = 4 * 1024 * 1024; // 4 MB
static constexpr qint64 MAX_FRAGMENT_BYTES  = 1 * 1024 * 1024; // 1 MB

WsClient::WsClient(QObject *parent)
    : QObject(parent),
      sock_(new QSslSocket(this)),
      reconnectTimer_(new QTimer(this)),
      pingTimer_(new QTimer(this))
{
    reconnectTimer_->setSingleShot(true);
    pingTimer_->setInterval(20000);

    sock_->setProtocol(QSsl::SecureProtocols);
    QSslConfiguration cfg = sock_->sslConfiguration();
    cfg.setProtocol(QSsl::SecureProtocols);
    sock_->setSslConfiguration(cfg);

    connect(sock_, &QSslSocket::connected,    this, &WsClient::onConnected);
    connect(sock_, &QSslSocket::encrypted,    this, &WsClient::onEncrypted);
    connect(sock_, &QSslSocket::readyRead,    this, &WsClient::onReadyRead);
    connect(sock_, &QSslSocket::disconnected, this, &WsClient::onDisconnected);
    connect(sock_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &WsClient::onSocketError);

    connect(reconnectTimer_, &QTimer::timeout, this, &WsClient::onReconnectTick);
    connect(pingTimer_,      &QTimer::timeout, this, &WsClient::onPing);
}

void WsClient::connectWith(const QString &apiKey, const QString &deviceId,
                           const QString &uniqueId,
                           const QString &mode, const QString &sourceLanguage,
                           const QString &translateTo) {
    want_        = true;
    jwt_         = apiKey;
    deviceId_    = deviceId;
    uniqueId_    = uniqueId;
    mode_        = mode;
    srcLang_     = sourceLanguage;
    translateTo_ = translateTo;

    // /captions/plugin is the dedicated bidi WS for the native OBS plugin.
    // The legacy /captions endpoint binds to a TikTok channel and resolves
    // its FLV stream - which the plugin does not need because it ships the
    // streamer's mic PCM directly. Using /captions caused the server to open
    // a STT connection for the bound channel and then close it 1000 in a
    // loop while ignoring the plugin's mic frames.
    QUrl url(QString::fromLatin1(TIKTOOL_WS_BASE) + "/captions/plugin");
    QUrlQuery q;
    // captions.ts auth accepts both `apiKey=` query param and `jwtKey=`. We
    // send whichever the plugin actually has. On a local-machine OBS plugin
    // there is no shared-browser log surface to leak the key, so url-param
    // auth is fine here.
    if (!apiKey.isEmpty())         q.addQueryItem("apiKey", apiKey);
    if (!deviceId.isEmpty())       q.addQueryItem("deviceId", deviceId);
    // uniqueId is the streamer's TikTok handle. The captions hub keys on it
    // so multiple subscribers (dock + overlay) share one STT session per
    // (uniqueId, language, translate). Without it, captions.ts rejects the
    // connection ("Missing uniqueId parameter").
    const QString id = uniqueId.isEmpty()
        ? QStringLiteral("obs-plugin-") + deviceId
        : uniqueId;
    if (!id.isEmpty())             q.addQueryItem("uniqueId", id);
    if (!sourceLanguage.isEmpty()) q.addQueryItem("language", sourceLanguage);
    if (!translateTo.isEmpty())    q.addQueryItem("translate", translateTo);
    q.addQueryItem("mode", "client"); // tell captions.ts we send PCM ourselves
    q.addQueryItem("source", "obs-plugin");
    q.addQueryItem("version", TIKTOK_CAPTIONS_VERSION_STR);
    url.setQuery(q);

    host_ = url.host();
    resourcePath_ = url.path(QUrl::FullyEncoded);
    if (resourcePath_.isEmpty()) resourcePath_ = "/";
    if (url.hasQuery()) resourcePath_ += "?" + url.query(QUrl::FullyEncoded);

    teardown();
    state_ = State::Connecting;

    const quint16 port = (url.port() > 0) ? quint16(url.port()) : 443;
    sock_->connectToHostEncrypted(host_, port);
}

void WsClient::disconnectWs() {
    want_ = false;
    reconnectTimer_->stop();
    pingTimer_->stop();
    if (state_ == State::Open) sendClose();
    teardown();
    bufferedFrames_.clear();
}

void WsClient::teardown() {
    if (sock_->state() != QAbstractSocket::UnconnectedState)
        sock_->abort();
    rxBuf_.clear();
    fragmentBuf_.clear();
    fragmentOpcode_ = 0;
    bool wasOpen = (state_ == State::Open);
    state_ = State::Idle;
    if (wasOpen) emit connectedChanged(false);
}

void WsClient::sendAudio(const QByteArray &pcm) {
    if (pcm.isEmpty()) return;
    if (state_ == State::Open) {
        sendBinary(pcm);
    } else {
        if ((int)bufferedFrames_.size() >= MAX_BUFFER_FRAMES)
            bufferedFrames_.pop_front();
        bufferedFrames_.push_back(pcm);
    }
}

void WsClient::sendControl(const QJsonObject &message) {
    if (state_ != State::Open) return;
    sendText(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void WsClient::onConnected() {
    // TCP open. Handshake fires once TLS finishes (onEncrypted).
}

void WsClient::onEncrypted() {
    writeHttpHandshake();
}

void WsClient::writeHttpHandshake() {
    // Generate 16-byte random key + compute expected accept up-front so we can
    // verify the server's response.
    QByteArray rnd(16, 0);
    quint64 a = QRandomGenerator::system()->generate64();
    quint64 b = QRandomGenerator::system()->generate64();
    std::memcpy(rnd.data(),     &a, 8);
    std::memcpy(rnd.data() + 8, &b, 8);
    secKey_ = rnd.toBase64();

    QByteArray accept = secKey_ + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    expectedAccept_ = QCryptographicHash::hash(accept, QCryptographicHash::Sha1).toBase64();

    QByteArray req;
    req += "GET " + resourcePath_.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: " + host_.toUtf8() + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + secKey_ + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "User-Agent: " TIKTOOL_PLUGIN_UA "\r\n";
    req += "Origin: https://tik.tools\r\n";
    req += "\r\n";
    sock_->write(req);
    state_ = State::HandshakeSent;
}

bool WsClient::parseHttpResponse() {
    int sep = rxBuf_.indexOf("\r\n\r\n");
    if (sep < 0) return false;
    QByteArray header = rxBuf_.left(sep);
    rxBuf_.remove(0, sep + 4);

    QList<QByteArray> lines = header.split('\n');
    if (lines.isEmpty() || !lines[0].contains(" 101 ")) {
        emit errorOccurred("WebSocket upgrade rejected: " + QString::fromUtf8(lines.value(0)));
        teardown();
        if (want_) scheduleReconnect();
        return false;
    }
    bool acceptOk = false;
    for (auto &raw : lines) {
        QByteArray line = raw.trimmed();
        int colon = line.indexOf(':');
        if (colon < 0) continue;
        QByteArray name  = line.left(colon).trimmed().toLower();
        QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "sec-websocket-accept" && value == expectedAccept_)
            acceptOk = true;
    }
    if (!acceptOk) {
        emit errorOccurred("WebSocket accept token mismatch");
        teardown();
        if (want_) scheduleReconnect();
        return false;
    }
    state_     = State::Open;
    backoffMs_ = 1000;
    pingTimer_->start();
    emit connectedChanged(true);
    blog(LOG_INFO, "[tiktok-captions] WS handshake complete");
    drainBuffer();
    return true;
}

void WsClient::onReadyRead() {
    rxBuf_ += sock_->readAll();
    if (rxBuf_.size() > MAX_RX_BUFFER_BYTES) {
        emit errorOccurred("WS rx buffer overflow - server sent more data than expected");
        teardown();
        if (want_) scheduleReconnect();
        return;
    }
    if (state_ == State::HandshakeSent) {
        if (!parseHttpResponse()) return;
    }
    if (state_ == State::Open) parseFrames();
}

void WsClient::parseFrames() {
    while (true) {
        if (rxBuf_.size() < 2) return;
        const quint8 b0 = quint8(rxBuf_[0]);
        const quint8 b1 = quint8(rxBuf_[1]);
        bool fin    = (b0 & 0x80) != 0;
        quint8 op   = (b0 & 0x0F);
        bool masked = (b1 & 0x80) != 0;
        quint64 payloadLen = (b1 & 0x7F);
        int idx = 2;
        if (payloadLen == 126) {
            if (rxBuf_.size() < idx + 2) return;
            payloadLen = (quint16(quint8(rxBuf_[idx])) << 8) | quint8(rxBuf_[idx + 1]);
            idx += 2;
        } else if (payloadLen == 127) {
            if (rxBuf_.size() < idx + 8) return;
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) payloadLen = (payloadLen << 8) | quint8(rxBuf_[idx + i]);
            idx += 8;
        }
        if (masked) {
            // Server-to-client frames MUST NOT be masked per RFC 6455. Treat as protocol error.
            emit errorOccurred("Masked frame from server");
            teardown();
            if (want_) scheduleReconnect();
            return;
        }
        if (payloadLen > quint64(MAX_PAYLOAD_BYTES)) {
            emit errorOccurred(QString("WS frame too large: %1").arg(payloadLen));
            teardown();
            if (want_) scheduleReconnect();
            return;
        }
        if (rxBuf_.size() < idx + qsizetype(payloadLen)) return;
        QByteArray payload = rxBuf_.mid(idx, qsizetype(payloadLen));
        rxBuf_.remove(0, idx + qsizetype(payloadLen));

        if (op == OP_CONT) {
            if (fragmentBuf_.size() + payload.size() > MAX_FRAGMENT_BYTES) {
                emit errorOccurred("WS fragment chain too long");
                teardown();
                if (want_) scheduleReconnect();
                return;
            }
            fragmentBuf_ += payload;
            if (fin) {
                if (fragmentOpcode_ == OP_TEXT) onTextPayload(fragmentBuf_);
                fragmentBuf_.clear();
                fragmentOpcode_ = 0;
            }
        } else if (op == OP_TEXT) {
            if (fin) onTextPayload(payload);
            else { fragmentBuf_ = payload; fragmentOpcode_ = OP_TEXT; }
        } else if (op == OP_BINARY) {
            // Server does not push binary today; ignore.
            if (!fin) { fragmentBuf_ = payload; fragmentOpcode_ = OP_BINARY; }
        } else if (op == OP_PING) {
            sendFrame(OP_PONG, payload);
        } else if (op == OP_PONG) {
            // Liveness probe ack; nothing to do.
        } else if (op == OP_CLOSE) {
            // RFC 6455: first 2 bytes of close payload = status code,
            // remaining = UTF-8 reason. Surface to the dock so the streamer
            // sees the real rejection ("Missing uniqueId", "No credits", etc).
            quint16 code = 0;
            QString reason;
            if (payload.size() >= 2) {
                code = (quint16(quint8(payload[0])) << 8) | quint8(payload[1]);
                if (payload.size() > 2) reason = QString::fromUtf8(payload.mid(2));
            }
            blog(LOG_WARNING, "[tiktok-captions] WS close code=%d reason=%s",
                 int(code), reason.toUtf8().constData());
            emit errorOccurred(reason.isEmpty()
                ? QString("Server closed WS (code %1)").arg(code)
                : QString("WS rejected: %1 (code %2)").arg(reason).arg(code));
            sendClose();
            teardown();
            if (want_) scheduleReconnect();
            return;
        }
    }
}

void WsClient::onTextPayload(const QByteArray &payload) {
    auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;
    auto obj = doc.object();
    const QString type = obj.value("type").toString();
    if (type == "hello" || type == "ready") {
        emit serverHello(obj);
    } else if (type == "partial") {
        emit partial(obj.value("text").toString(), obj.value("language").toString());
    } else if (type == "final" || type == "transcript") {
        emit finalLine(obj.value("text").toString(),
                       obj.value("language").toString(),
                       obj.value("translation").toString(),
                       obj.value("translationLanguage").toString());
    } else if (type == "minutes" || type == "credits") {
        emit minutesUpdate(obj.value("minutesLeft").toInt(
                           obj.value("remaining").toInt(0)));
    } else if (type == "error") {
        emit errorOccurred(obj.value("message").toString("server error"));
    } else if (type == "status") {
        // captions.ts emits 'connecting' / 'waiting' / 'live' / 'transcribing'.
        // 'waiting' fires when the streamer is not currently live - the WS
        // stays open polling and resumes once they go live. Without surfacing
        // this, the dock pill stays on "Connecting..." indefinitely and the
        // streamer cannot tell whether the plugin is broken or just waiting.
        emit serverStatus(obj.value("status").toString());
    }
}

void WsClient::sendFrame(quint8 opcode, const QByteArray &payload) {
    if (state_ != State::Open && opcode != OP_CLOSE) return;

    QByteArray header;
    header.append(char(0x80 | (opcode & 0x0F)));   // FIN + opcode
    qint64 len = payload.size();
    if (len < 126) {
        header.append(char(0x80 | quint8(len)));
    } else if (len < 65536) {
        header.append(char(0x80 | 126));
        header.append(char((len >> 8) & 0xFF));
        header.append(char(len & 0xFF));
    } else {
        header.append(char(0x80 | 127));
        for (int i = 7; i >= 0; --i) header.append(char((len >> (i * 8)) & 0xFF));
    }
    quint32 maskKey = QRandomGenerator::system()->generate();
    char mask[4] = {
        char((maskKey >> 24) & 0xFF),
        char((maskKey >> 16) & 0xFF),
        char((maskKey >> 8)  & 0xFF),
        char( maskKey        & 0xFF),
    };
    header.append(mask, 4);

    QByteArray masked = payload;
    for (qint64 i = 0; i < masked.size(); ++i)
        masked[i] = char(quint8(masked[i]) ^ quint8(mask[i & 3]));

    sock_->write(header);
    sock_->write(masked);
}

void WsClient::sendText(const QString &text)        { sendFrame(OP_TEXT,   text.toUtf8()); }
void WsClient::sendBinary(const QByteArray &data)   { sendFrame(OP_BINARY, data); }
void WsClient::sendClose()                          { sendFrame(OP_CLOSE,  {}); }

void WsClient::onDisconnected() {
    bool wasOpen = state_ == State::Open;
    teardown();
    if (wasOpen) blog(LOG_INFO, "[tiktok-captions] WS disconnected");
    if (want_) scheduleReconnect();
}

void WsClient::onSocketError(QAbstractSocket::SocketError) {
    emit errorOccurred(sock_->errorString());
    teardown();
    if (want_) scheduleReconnect();
}

void WsClient::scheduleReconnect() {
    if (reconnectTimer_->isActive()) return;
    int delay = backoffMs_;
    backoffMs_ = std::min(60000, backoffMs_ * 2);
    reconnectTimer_->start(delay);
}

void WsClient::onReconnectTick() {
    if (!want_) return;
    connectWith(jwt_, deviceId_, uniqueId_, mode_, srcLang_, translateTo_);
}

void WsClient::onPing() {
    if (state_ == State::Open) sendFrame(OP_PING, {});
}

void WsClient::drainBuffer() {
    while (state_ == State::Open && !bufferedFrames_.empty()) {
        sendBinary(bufferedFrames_.front());
        bufferedFrames_.pop_front();
    }
}

} // namespace tiktok
