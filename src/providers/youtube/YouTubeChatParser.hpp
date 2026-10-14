// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: pure parsing of YouTube live chat pages and Innertube
// responses (no network, no UI). YouTube changes this format without notice;
// everything that depends on it lives in this file.
#pragma once

#include "providers/unified/ExternalChatMessage.hpp"

#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino::youtube {

/// What the user typed in settings, normalized.
struct Source {
    /// Fixed video id (11 chars) if the source was a video id or video URL.
    QString videoId;
    /// Otherwise, the URL whose `/live` page resolves the current stream,
    /// e.g. https://www.youtube.com/@handle/live
    QString liveUrl;
};

/// Accepts a video id, a watch/youtu.be/live URL, a handle (`@x` or `x`),
/// or a channel URL (`youtube.com/@x`, `youtube.com/channel/UC...`).
std::optional<Source> parseSource(const QString &input);

/// Video id of the live stream from a `/live` page, if the channel is live.
std::optional<QString> extractLiveVideoId(const QString &html);

struct InnertubeSession {
    QString apiKey;
    QString clientVersion;
    QString continuation;
};

/// Extracts the API key, client version and initial continuation from the
/// popout live chat page. nullopt if the chat is not available (stream
/// ended, chat disabled, or the page format changed).
std::optional<InnertubeSession> parseLiveChatPage(const QString &html);

struct PollResult {
    std::vector<unified::ExternalChatMessage> messages;
    /// Empty when the stream ended.
    QString continuation;
    int timeoutMs{5000};
};

/// Parses a `youtubei/v1/live_chat/get_live_chat` response.
PollResult parseGetLiveChat(const QJsonObject &root);

/// Parses the `actions` array shared by the page and the poll responses.
std::vector<unified::ExternalChatMessage> parseActions(
    const QJsonArray &actions);

/// Parses one `liveChatTextMessageRenderer` / `liveChatPaidMessageRenderer`
/// / `liveChatPaidStickerRenderer` / `liveChatMembershipItemRenderer`.
std::optional<unified::ExternalChatMessage> parseItem(const QJsonObject &item);

/// --- Backend B: YouTube Data API v3 ---

/// Parses a `liveChatMessages.list` response.
struct DataApiPollResult {
    std::vector<unified::ExternalChatMessage> messages;
    QString nextPageToken;
    int pollingIntervalMs{5000};
    bool ended{false};
};
DataApiPollResult parseDataApiMessages(const QJsonObject &root);

}  // namespace chatterino::youtube
