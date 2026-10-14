// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: small platform icon shown before the username.
#pragma once

#include "messages/Emote.hpp"
#include "providers/unified/ExternalChatMessage.hpp"

namespace chatterino::unified {

/// Returns the badge emote (icon + tooltip) for a platform. The emote is
/// created once per platform and cached. Must be called from the GUI thread
/// the first time (it reads resources).
EmotePtr platformBadge(ChatPlatform platform);

}  // namespace chatterino::unified
