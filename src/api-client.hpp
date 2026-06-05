#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace tiktool {

// Thin Qt-network wrapper over the public TikTool REST surface used by the
// plugin. All requests are asynchronous; the callback fires on the GUI thread.
class ApiClient : public QObject {
    Q_OBJECT
public:
    using JsonCb = std::function<void(bool ok, const QJsonObject &body, int httpStatus)>;

    explicit ApiClient(QObject *parent = nullptr);

    // Trial / credits
    void checkTrial(const QString &deviceId, JsonCb cb);
    void claimDeviceWithToken(const QString &deviceId, const QString &linkToken, JsonCb cb);
    void resolveCredits(JsonCb cb);

    // Captions overlay surfaces (setups + JWT mint)
    void mintOverlayToken(JsonCb cb);
    void fetchSetups(JsonCb cb);
    void saveSetup(const QString &id, const QJsonObject &payload, JsonCb cb);

    // Auth state
    QString jwtCached() const { return jwtCached_; }
    void    setJwt(const QString &j) { jwtCached_ = j; }
    QString apiKeyCached() const { return apiKey_; }
    void    setApiKey(const QString &k) { apiKey_ = k; }

private:
    void postJson(const QString &path, const QJsonObject &body, JsonCb cb, bool authed);
    void getJson(const QString &path, JsonCb cb, bool authed);
    void send(const QString &method, const QString &path,
              const QJsonObject &body, JsonCb cb, bool authed);

    QNetworkAccessManager *nam_;
    QString jwtCached_;
    QString apiKey_;
};

} // namespace tiktool
