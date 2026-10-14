// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: read-only YouTube live chat client.
//
// Backend A (default, no quota): the internal Innertube endpoint used by the
// live chat popout page. Backend B (fallback, needs an API key and consumes
// daily quota): YouTube Data API v3. B is used only when A cannot load the
// chat for a live video and an API key is configured.
//
// Everything runs on the GUI thread (NetworkRequest callbacks and QTimer).
#pragma once

#include "providers/unified/ExternalChatMessage.hpp"
#include "providers/youtube/YouTubeChatParser.hpp"
#include "util/ExponentialBackoff.hpp"

#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

namespace chatterino::youtube {

class YouTubeChatClient : public std::enable_shared_from_this<YouTubeChatClient>
{
public:
    using MessageCallback =
        std::function<void(unified::ExternalChatMessage message)>;
    using StatusCallback = std::function<void(const QString &text)>;

    YouTubeChatClient(Source source, QString apiKey, MessageCallback onMessage,
                      StatusCallback onStatus);
    ~YouTubeChatClient();

    YouTubeChatClient(const YouTubeChatClient &) = delete;
    YouTubeChatClient(YouTubeChatClient &&) = delete;
    YouTubeChatClient &operator=(const YouTubeChatClient &) = delete;
    YouTubeChatClient &operator=(YouTubeChatClient &&) = delete;

    /// Must be called on a shared_ptr-owned instance, on the GUI thread.
    void start();
    void stop();

private:
    // Resolution
    void resolveVideo();
    void waitForLive(const QString &reason);
    // Backend A
    void loadChatPage();
    void poll();
    void scheduleNext(int delayMs, void (YouTubeChatClient::*fn)());
    // Backend B
    void startDataApi();
    void pollDataApi();

    void deliver(std::vector<unified::ExternalChatMessage> messages);
    void status(const QString &text);

    Source source_;
    QString apiKey_;
    MessageCallback onMessage_;
    StatusCallback onStatus_;

    bool stopped_{false};
    QString lastStatus_;  // last text passed to status(), for de-duplication
    QString videoId_;
    QTimer timer_;  // single-shot driver for all delayed steps

    // Backend A state
    InnertubeSession session_;
    int consecutiveErrors_{0};
    ExponentialBackoff<5> backoff_{std::chrono::milliseconds(2000)};
    int chatPageFailures_{0};

    // Backend B state
    bool usingDataApi_{false};
    QString liveChatId_;
    QString pageToken_;
};

}  // namespace chatterino::youtube
