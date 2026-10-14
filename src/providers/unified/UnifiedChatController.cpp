// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/unified/UnifiedChatController.hpp"

#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "singletons/Settings.hpp"

namespace {

const auto &LOG = chatterinoUnifiedChat;

}  // namespace

namespace chatterino::unified {

UnifiedChatController::UnifiedChatController() = default;

UnifiedChatController::~UnifiedChatController() = default;

void UnifiedChatController::initialize(Settings &settings)
{
    assertInGuiThread();
    (void)settings;
    qCDebug(LOG) << "Controller initialized (no platforms enabled)";
}

void UnifiedChatController::enqueue(ExternalChatMessage message)
{
    // Fase 2 skeleton: message building and injection arrive in later phases.
    qCDebug(LOG) << "Dropping message from" << platformName(message.platform)
                 << "by" << message.authorName;
}

}  // namespace chatterino::unified
