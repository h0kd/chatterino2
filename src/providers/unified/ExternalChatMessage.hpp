// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: normalized chat message coming from an external platform
// (Kick, YouTube) before it is turned into a Chatterino Message.
#pragma once

#include <QColor>
#include <QDateTime>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino::unified {

enum class ChatPlatform : std::uint8_t {
    Twitch,
    Kick,
    YouTube,
};

/// Human readable platform name, e.g. "Kick". Used for tooltips and logs.
QString platformName(ChatPlatform platform);

/// One piece of a message: either plain text or an emote.
struct ExternalChatFragment {
    /// Text content. For an emote fragment this is the emote's name.
    QString text;
    /// Image URL of the emote. Empty for plain text.
    QString emoteUrl;

    [[nodiscard]] bool isEmote() const
    {
        return !this->emoteUrl.isEmpty();
    }

    static ExternalChatFragment makeText(QString text)
    {
        return {.text = std::move(text), .emoteUrl = {}};
    }

    static ExternalChatFragment makeEmote(QString name, QString url)
    {
        return {.text = std::move(name), .emoteUrl = std::move(url)};
    }
};

struct ExternalChatMessage {
    ChatPlatform platform{ChatPlatform::Kick};
    /// Message id on the origin platform, used for deduplication.
    QString id;
    QString authorName;
    /// Optional URL opened when the author name is clicked.
    QString authorUrl;
    std::optional<QColor> authorColor;
    QDateTime timestamp;
    std::vector<ExternalChatFragment> fragments;

    bool isMod{false};
    bool isOwner{false};
    bool isMember{false};

    /// Plain-text rendering of all fragments (emotes by name).
    [[nodiscard]] QString plainText() const;
};

}  // namespace chatterino::unified
