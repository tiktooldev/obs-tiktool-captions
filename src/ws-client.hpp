#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>
#include <QJsonObject>
#include <QSslSocket>
#include <deque>

namespace tiktool {

// Hand-rolled RFC 6455 WebSocket client over QSslSocket. Avoids Qt6WebSockets
// because installed OBS Studio binaries do not ship that module and shipping
// our own Qt6WebSockets.dll alongside the plugin would race the OBS-loaded
// Qt6Core at runtime. QSslSocket comes for free with the Qt6Network that OBS
// already loads, so this client costs us nothing at link time.
//
// Talks to api.tik.tools captions WS:
//   wss://api.tik.tools/captions/ws?jwtKey=<jwt>&deviceId=<id>&...
// Sends binary PCM16 16k mono frames, receives JSON text events.
class WsClient : public QObject {
    Q_OBJECT
public:
    explicit WsClient(QObject *parent = nullptr);

    void connectWith(const QString &jwt, const QString &deviceId,
                     const QString &mode, const QString &sourceLanguage,
                     const QString &translateTo);
    void disconnectWs();

    void sendAudio(const QByteArray &pcm16le);
    void sendControl(const QJsonObject &message);

    bool connected() const { return state_ == State::Open; }

signals:
    void connectedChanged(bool);
    void partial(const QString &text, const QString &lang);
    void finalLine(const QString &text, const QString &lang,
                   const QString &translation, const QString &translationLang);
    void minutesUpdate(int minutesLeft);
    void errorOccurred(const QString &reason);
    void serverHello(const QJsonObject &hello);

private slots:
    void onConnected();
    void onEncrypted();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError);
    void onPing();
    void onReconnectTick();

private:
    enum class State { Idle, Connecting, HandshakeSent, Open };

    void teardown();
    void scheduleReconnect();
    void writeHttpHandshake();
    bool parseHttpResponse();
    void parseFrames();
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void sendText(const QString &text);
    void sendBinary(const QByteArray &data);
    void sendClose();
    void onTextPayload(const QByteArray &payload);
    void drainBuffer();

    QSslSocket *sock_;
    QTimer     *reconnectTimer_;
    QTimer     *pingTimer_;

    QString jwt_;
    QString deviceId_;
    QString mode_;
    QString srcLang_;
    QString translateTo_;
    QString host_;
    QString resourcePath_;
    QByteArray expectedAccept_;
    QByteArray secKey_;

    State   state_      = State::Idle;
    bool    want_       = false;
    int     backoffMs_  = 1000;
    QByteArray rxBuf_;

    // Fragment reassembly across continuation frames.
    QByteArray fragmentBuf_;
    quint8     fragmentOpcode_ = 0;

    static constexpr int MAX_BUFFER_FRAMES = 200; // ~12 s @ 60 ms
    std::deque<QByteArray> bufferedFrames_;
};

} // namespace tiktool
