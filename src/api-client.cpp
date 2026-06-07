#include "api-client.hpp"
#include "version.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace tiktok {

ApiClient::ApiClient(QObject *parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

void ApiClient::send(const QString &method, const QString &path,
                     const QJsonObject &body, JsonCb cb, bool authed) {
    QNetworkRequest req(QUrl(QString::fromLatin1(TIKTOOL_API_BASE) + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("User-Agent", TIKTOOL_PLUGIN_UA);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    if (authed && !jwtCached_.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + jwtCached_).toUtf8());
    if (authed && !apiKey_.isEmpty())
        req.setRawHeader("x-api-key", apiKey_.toUtf8());

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = nullptr;
    if (method == "GET")
        reply = nam_->get(req);
    else if (method == "POST")
        reply = nam_->post(req, payload);
    else if (method == "PUT")
        reply = nam_->put(req, payload);
    else
        reply = nam_->sendCustomRequest(req, method.toUtf8(), payload);

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray data = reply->readAll();
        reply->deleteLater();
        QJsonParseError err{};
        auto doc = QJsonDocument::fromJson(data, &err);
        QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject{};
        bool ok = code >= 200 && code < 300 && err.error == QJsonParseError::NoError;
        if (cb) cb(ok, obj, code);
    });
}

void ApiClient::postJson(const QString &p, const QJsonObject &b, JsonCb c, bool a) { send("POST", p, b, c, a); }
void ApiClient::getJson(const QString &p, JsonCb c, bool a) { send("GET", p, {}, c, a); }

void ApiClient::checkTrial(const QString &deviceId, JsonCb cb) {
    // Server uses fingerprint_ to dedup duplicate accounts spawned on the
    // same machine across IP rotations. Real Cloudflare-side IP is read off
    // CF-Connecting-IP server-side; we do not send IP ourselves.
    QString q = "/captions/trial/check?deviceId=" + deviceId;
    if (!fingerprint_.isEmpty()) q += "&fp=" + fingerprint_;
    getJson(q, cb, /*authed=*/false);
}

void ApiClient::claimDeviceWithToken(const QString &deviceId, const QString &linkToken, JsonCb cb) {
    QJsonObject body{{"deviceId", deviceId}, {"linkToken", linkToken}};
    if (!fingerprint_.isEmpty()) body["fp"] = fingerprint_;
    postJson("/captions/trial/claim-device", body, cb, false);
}

void ApiClient::resolveCredits(JsonCb cb) {
    getJson("/captions/credits", cb, true);
}

void ApiClient::mintOverlayToken(JsonCb cb) {
    postJson("/captions/overlay/mint", {}, cb, true);
}

void ApiClient::fetchSetups(JsonCb cb) {
    getJson("/captions/overlay/setups", cb, true);
}

void ApiClient::saveSetup(const QString &id, const QJsonObject &payload, JsonCb cb) {
    QJsonObject body = payload;
    if (!id.isEmpty()) body["id"] = id;
    postJson("/captions/overlay/setups", body, cb, true);
}

} // namespace tiktok
