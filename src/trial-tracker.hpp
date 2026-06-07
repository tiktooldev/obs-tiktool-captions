#pragma once

#include "api-client.hpp"

#include <QObject>
#include <QTimer>

namespace tiktok {

// Account-gated trial. There is no anonymous path: the streamer signs in with
// their tik.tools account on first launch and the server hands back a JWT
// bound to (userId, deviceId). The server is the single source of truth for
// minutesLeft and watermark; the plugin caches what it was last told.
//
// Trial policy:
//   - 60 free minutes per account, lifetime (not per-device).
//   - Watermark on every transcript line + overlay during trial.
//   - At 0 minutes: API closes the WS, overlay renders a paywall card,
//     dock shows the upgrade CTA. Re-opening only succeeds for a paid sub.
class TrialTracker : public QObject {
    Q_OBJECT
public:
    enum class State {
        Unknown,
        SignInRequired,    // no JWT, plugin must not stream audio
        DuplicateAccount,  // anti-abuse blocked the trial for this account
        AccountTrial,      // signed-in, within 60-min trial, watermark on
        AccountExpired,    // signed-in, trial used up, needs paid sub
        Paid               // paid subscription or topup credits
    };
    Q_ENUM(State)

    explicit TrialTracker(ApiClient *api, QObject *parent = nullptr);

    void refresh();                                // pulls latest credit / trial status
    void noteMinutesFromServer(int minutesLeft);   // called from WsClient minutes event
    void noteWatermarkFromServer(bool wm);         // called from WsClient hello / minutes event
    void linkDevice(const QString &linkToken);     // claim trial after OAuth

    State   state()         const { return state_; }
    int     minutesLeft()   const { return minutesLeft_; }
    bool    translating()   const { return translating_; }
    bool    watermark()     const { return watermark_; }
    QString duplicateOriginalEmail() const { return duplicateOriginalEmail_; }
    void    setTranslating(bool t) { translating_ = t; emit changed(); }

signals:
    void changed();

private:
    void applyServerPayload(const QJsonObject &);

    ApiClient *api_;
    QTimer    *poll_;
    State      state_       = State::Unknown;
    int        minutesLeft_ = 0;
    bool       translating_ = false;
    bool       watermark_   = true; // default ON until server says otherwise
    QString    duplicateOriginalEmail_;
};

} // namespace tiktok
