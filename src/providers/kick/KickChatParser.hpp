// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: pure parsing of Kick chat payloads (no network, no UI).
#pragma once

#include "providers/unified/ExternalChatMessage.hpp"

#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino::kick {

/// Splits Kick message content into text and emote fragments. Emotes come
/// inline as `[emote:{id}:{name}]`.
std::vector<unified::ExternalChatFragment> parseContent(const QString &content);

/// Image URL for a Kick emote id.
QString emoteUrl(const QString &emoteId);

/// Parses the (already JSON-decoded) `data` object of an
/// `App\Events\ChatMessageEvent`. Returns nullopt if required fields are
/// missing.
std::optional<unified::ExternalChatMessage> parseChatMessageEvent(
    const QJsonObject &data);

}  // namespace chatterino::kick
