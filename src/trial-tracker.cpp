#include "trial-tracker.hpp"
#include "settings.hpp"

#include <QJsonObject>

namespace tiktool {

TrialTracker::TrialTracker(ApiClient *api, QObject *parent)
    : QObject(parent), api_(api), poll_(new QTimer(this)) {
    // Fast tick while we have no JWT (3 s) so a fresh sign-in handoff lands
    // within a few seconds of the streamer hitting the link. Once we have a
    // JWT we slow down to 60 s in applyServerPayload.
    poll_->setInterval(3000);
    connect(poll_, &QTimer::timeout, this, &TrialTracker::refresh);
    poll_->start();
}

void TrialTracker::refresh() {
    const QString jwt = Settings::instance().jwt();
    // Self-adjust poll cadence: 3 s while we are still waiting for a sign-in
    // handoff, 60 s once we are authed. Anti-abuse can clear our JWT in
    // applyServerPayload, which restores the fast cadence on the next tick.
    poll_->setInterval(jwt.isEmpty() ? 3000 : 60000);
    if (jwt.isEmpty()) {
        // No JWT means user has not signed in. Anonymous mode is disabled
        // server-side as well; we just surface the gate.
        state_ = State::SignInRequired;
        minutesLeft_ = 0;
        watermark_ = true;
        emit changed();
        // Still poll the trial endpoint so a sign-in handoff (linkToken) is
        // picked up promptly.
        api_->checkTrial(Settings::instance().deviceId(),
            [this](bool ok, const QJsonObject &body, int) {
                if (!ok) return;
                // /captions/trial/check returns jwt/apiKey only after the
                // user signs in on the web. When it lands, save it AND
                // persist to disk immediately - the next read of the same
                // Dragonfly key returns null (single-read), so a missed
                // persist means the streamer has to sign in again.
                if (body.contains("jwt")) {
                    const QString jwt = body.value("jwt").toString();
                    if (jwt.isEmpty()) { applyServerPayload(body); return; }
                    Settings::instance().setJwt(jwt);
                    api_->setJwt(jwt);
                    if (body.contains("apiKey")) {
                        Settings::instance().setApiKey(body.value("apiKey").toString());
                        api_->setApiKey(body.value("apiKey").toString());
                    }
                    if (body.contains("email"))
                        Settings::instance().setAccountEmail(body.value("email").toString());
                    Settings::instance().save();
                    // Slow the poll back down now that we are authed.
                    poll_->setInterval(60000);
                    applyServerPayload(body);
                } else {
                    applyServerPayload(body);
                }
            });
        return;
    }
    api_->resolveCredits([this](bool ok, const QJsonObject &body, int code) {
        if (!ok) {
            // 401 from /captions/credits is most often a transient apiKey
            // sync hiccup (key just rotated, Directus replication lag), NOT
            // a dead JWT. Earlier builds nuked the JWT on every 401 which
            // caused the dock to flap between "Free trial - 60 min left" and
            // "Sign in to start" once every 3 seconds. Keep the cached state
            // and let the next tick try again. The handoff stays usable for
            // another 24 h via the JWT TTL.
            return;
        }
        applyServerPayload(body);
    });
}

void TrialTracker::noteMinutesFromServer(int minutesLeft) {
    minutesLeft_ = minutesLeft;
    Settings::instance().setCachedMinutesRemaining(minutesLeft);
    // If the server tells us we're out, flip into AccountExpired so the
    // dock surfaces the paywall immediately even before the next poll.
    if (minutesLeft_ <= 0 && !Settings::instance().jwt().isEmpty()
        && state_ != State::Paid) {
        state_ = State::AccountExpired;
    }
    emit changed();
}

void TrialTracker::noteWatermarkFromServer(bool wm) {
    if (watermark_ == wm) return;
    watermark_ = wm;
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
    // The streamer's wallet has two compartments and the dock shows the sum:
    //   * minutesLeft = free trial bank from /captions/trial/check (60 on
    //     first sign-in, decremented by the captions hub).
    //   * totalCredits = paid caption credits summed across every API key
    //     the user owns, computed by /api/captions/plugin-link.
    // Previously totalCredits OVERRODE minutesLeft, so a new streamer with
    // no paid topups saw "Free trial - 0 min left" instead of the 60 free
    // minutes the trial ledger just seeded for them. Sum both.
    int freeBank   = 0;
    int paidWallet = 0;
    if (b.contains("minutesLeft")) freeBank = b.value("minutesLeft").toInt(0);
    else if (b.contains("remaining")) freeBank = b.value("remaining").toInt(0);
    if (b.contains("totalCredits")) paidWallet = b.value("totalCredits").toInt(0);
    minutesLeft_ = freeBank + paidWallet;
    Settings::instance().setCachedMinutesRemaining(minutesLeft_);

    if (b.contains("watermark"))    watermark_ = b.value("watermark").toBool(true);
    else if (b.contains("trial"))   watermark_ = b.value("trial").toBool(true);
    else if (minutesLeft_ <= 0)     watermark_ = true;

    const QString s = b.value("state").toString();
    if (b.value("duplicate").toBool()) {
        state_ = State::DuplicateAccount;
        duplicateOriginalEmail_ = b.value("originalEmail").toString();
        // Hard-revoke the JWT locally so the streamer cannot stream audio.
        Settings::instance().setJwt("");
        Settings::instance().setApiKey("");
    }
    else if (s == "duplicate_account") {
        state_ = State::DuplicateAccount;
        duplicateOriginalEmail_ = b.value("originalEmail").toString();
        Settings::instance().setJwt("");
        Settings::instance().setApiKey("");
    }
    else if (s == "paid")            state_ = State::Paid;
    else if (s == "account_trial")   state_ = (paidWallet > 0 ? State::Paid : State::AccountTrial);
    else if (s == "account_expired") state_ = State::AccountExpired;
    else if (s == "sign_in_required" || Settings::instance().jwt().isEmpty())
        state_ = State::SignInRequired;
    else if (minutesLeft_ <= 0)      state_ = State::AccountExpired;
    else                             state_ = State::AccountTrial;
    emit changed();
}

} // namespace tiktool
