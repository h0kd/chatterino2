// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: owns the Kick and YouTube read-only chat clients and injects
// their messages into the configured Twitch channel.
#pragma once

#include "providers/unified/ExternalChatMessage.hpp"

#include <pajlada/signals/signalholder.hpp>

#include <memory>

namespace chatterino {

class Settings;

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

private:
    /// Re-reads the settings and starts/stops/restarts the platform clients
    /// so that they match. GUI thread only.
    void applySettings();

    void startKick(const QString &slug);
    void stopKick();
    void startYouTube(const QString &source);
    void stopYouTube();

    pajlada::Signals::SignalHolder signalHolder_;

    // Currently running configuration, empty when the client is stopped.
    QString runningKickSlug_;
    QString runningYouTubeSource_;
};

}  // namespace chatterino::unified
