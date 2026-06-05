#pragma once

#include "api-client.hpp"

#include <QObject>
#include <QTimer>

namespace tiktool {

// Free-trial accounting. Source of truth is the server (IP + device id +
// optional account id). The plugin only caches what it was last told and
// drives the UX states: anonymous / authed / out-of-minutes / paid.
class TrialTracker : public QObject {
    Q_OBJECT
public:
    enum class State {
        Unknown,
        AnonymousTrial,   // IP-based 10 min, 5 if translating
        AnonymousExpired, // signed-in get another 10 min
        AccountTrial,     // signed-in user, still within free 10 min
        AccountExpired,   // signed-in user, needs topup or subscription
        Paid              // subscription or topup credits available
    };
    Q_ENUM(State)

    explicit TrialTracker(ApiClient *api, QObject *parent = nullptr);

    void refresh();                // pulls latest credit/trial status
    void noteMinutesFromServer(int minutesLeft); // called from WsClient minutes event
    void linkDevice(const QString &linkToken);   // claim trial to account after OAuth

    State   state()         const { return state_; }
    int     minutesLeft()   const { return minutesLeft_; }
    bool    translating()   const { return translating_; }
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
};

} // namespace tiktool
