// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: owns the Kick and YouTube read-only chat clients and injects
// their messages into the configured Twitch channel.
#pragma once

#include "messages/MessageBuilder.hpp"
#include "providers/unified/ExternalChatMessage.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QSet>
#include <QString>

#include <array>
#include <deque>
#include <memory>

namespace chatterino {

class Settings;
class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

namespace kick {
class KickChatClient;
}  // namespace kick

namespace youtube {
class YouTubeChatClient;
}  // namespace youtube

}  // namespace chatterino

namespace chatterino::unified {

class UnifiedChatController
{
public:
    UnifiedChatController();
    ~UnifiedChatController();

    UnifiedChatController(const UnifiedChatController &) = delete;
    UnifiedChatController(UnifiedChatController &&) = delete;
    UnifiedChatController &operator=(const UnifiedChatController &) = delete;
    UnifiedChatController &operator=(UnifiedChatController &&) = delete;

    /// Called once from Application::initialize, on the GUI thread. Hooks up
    /// settings and starts whichever platform clients are enabled.
    void initialize(Settings &settings);

    /// Thread-safe entry point used by platform clients. Builds a Chatterino
    /// message and adds it to the target channel on the GUI thread.
    void enqueue(ExternalChatMessage message);

    /// Builds the Chatterino message for an external message. Exposed for
    /// tests. GUI thread (reads settings and resources). `twitchChannel` may
    /// be null; when set, its FFZ/BTTV/7TV emotes (plus the global ones) are
    /// applied to the text of the external message.
    static std::pair<MessagePtr, HighlightAlert> buildMessage(
        const ExternalChatMessage &message, const QString &channelName,
        TwitchChannel *twitchChannel);

private:
    /// Re-reads the settings and starts/stops/restarts the platform clients
    /// so that they match. GUI thread only.
    void applySettings();

    void startKick(const QString &slug);
    void stopKick();
    void startYouTube(const QString &source);
    void stopYouTube();

    /// GUI thread: dedup, build and add the message to the target channel.
    void inject(const ExternalChatMessage &message);
    /// Adds a system message to the target channel if it is open.
    void addSystemMessage(const QString &text);
    /// Returns true if `id` was not seen before for `platform` (and records it).
    bool markSeen(ChatPlatform platform, const QString &id);
    ChannelPtr targetChannel() const;

    pajlada::Signals::SignalHolder signalHolder_;
    /// Expires when the controller is destroyed; posted callbacks check it.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::shared_ptr<kick::KickChatClient> kick_;
    std::shared_ptr<youtube::YouTubeChatClient> youtube_;

    // Currently running configuration, empty when the client is stopped.
    QString runningKickSlug_;
    QString runningYouTubeSource_;

    struct SeenIds {
        std::deque<QString> order;
        QSet<QString> set;
    };
    std::array<SeenIds, 3> seen_;
};

}  // namespace chatterino::unified
