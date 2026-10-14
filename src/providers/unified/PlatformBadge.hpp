// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: small platform icon shown before the username.
#pragma once

#include "messages/Emote.hpp"
#include "providers/unified/ExternalChatMessage.hpp"

#include <QString>

namespace chatterino::unified {

/// Returns the badge emote (icon + tooltip + home page) for a platform.
/// Created once per platform and cached; safe to call from any thread after
/// resources are initialized.
EmotePtr platformBadge(ChatPlatform platform);

/// Name of the Twitch channel that receives Kick/YouTube messages:
/// `unifiedChatTargetTwitchChannel`, or the logged-in user's name if empty.
QString targetTwitchChannelName();

/// Whether messages in `channelName` are part of the unified chat right now,
/// i.e. unified chat is enabled, at least one external platform is enabled and
/// `channelName` is the target channel.
bool isUnifiedTwitchChannel(const QString &channelName);

/// Whether Twitch messages in `channelName` should carry the Twitch icon.
bool showTwitchPlatformBadge(const QString &channelName);

}  // namespace chatterino::unified
