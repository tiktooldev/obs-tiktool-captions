#include "trial-tracker.hpp"
#include "settings.hpp"

#include <QJsonObject>

namespace tiktool {

TrialTracker::TrialTracker(ApiClient *api, QObject *parent)
    : QObject(parent), api_(api), poll_(new QTimer(this)) {
    poll_->setInterval(60000); // 60s minimum cadence; WS pushes minutes too
    connect(poll_, &QTimer::timeout, this, &TrialTracker::refresh);
    poll_->start();
}

void TrialTracker::refresh() {
    const QString jwt = Settings::instance().jwt();
    if (jwt.isEmpty()) {
        api_->checkTrial(Settings::instance().deviceId(),
            [this](bool ok, const QJsonObject &body, int) {
                if (!ok) return;
                applyServerPayload(body);
            });
    } else {
        api_->resolveCredits([this](bool ok, const QJsonObject &body, int) {
            if (!ok) return;
            applyServerPayload(body);
        });
    }
}

void TrialTracker::noteMinutesFromServer(int minutesLeft) {
    minutesLeft_ = minutesLeft;
    Settings::instance().setCachedMinutesRemaining(minutesLeft);
    emit changed();
}

void TrialTracker::linkDevice(const QString &linkToken) {
    api_->claimDeviceWithToken(Settings::instance().deviceId(), linkToken,
        [this](bool ok, const QJsonObject &body, int) {
            if (!ok) return;
            if (body.contains("jwt")) {
                Settings::instance().setJwt(body.value("jwt").toString());
                api_->setJwt(body.value("jwt").toString());
            }
            if (body.contains("apiKey")) {
                Settings::instance().setApiKey(body.value("apiKey").toString());
                api_->setApiKey(body.value("apiKey").toString());
            }
            if (body.contains("email"))
                Settings::instance().setAccountEmail(body.value("email").toString());
            applyServerPayload(body);
        });
}

void TrialTracker::applyServerPayload(const QJsonObject &b) {
    if (b.contains("minutesLeft")) minutesLeft_ = b.value("minutesLeft").toInt(minutesLeft_);
    else if (b.contains("remaining")) minutesLeft_ = b.value("remaining").toInt(minutesLeft_);
    Settings::instance().setCachedMinutesRemaining(minutesLeft_);

    const QString s = b.value("state").toString();
    if      (s == "paid")               state_ = State::Paid;
    else if (s == "account_trial")      state_ = State::AccountTrial;
    else if (s == "account_expired")    state_ = State::AccountExpired;
    else if (s == "anonymous_trial")    state_ = State::AnonymousTrial;
    else if (s == "anonymous_expired")  state_ = State::AnonymousExpired;
    else if (minutesLeft_ <= 0)         state_ = Settings::instance().jwt().isEmpty()
                                                  ? State::AnonymousExpired
                                                  : State::AccountExpired;
    else                                state_ = Settings::instance().jwt().isEmpty()
                                                  ? State::AnonymousTrial
                                                  : State::AccountTrial;
    emit changed();
}

} // namespace tiktool
