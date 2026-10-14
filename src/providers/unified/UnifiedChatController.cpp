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

UnifiedChatController::~UnifiedChatController()
{
    this->stopKick();
    this->stopYouTube();
}

void UnifiedChatController::initialize(Settings &settings)
{
    assertInGuiThread();

    auto onChange = [this] {
        this->applySettings();
    };
    settings.unifiedChatEnabled.connect(onChange, this->signalHolder_, false);
    settings.unifiedChatKickEnabled.connect(onChange, this->signalHolder_,
                                            false);
    settings.unifiedChatKickChannelSlug.connect(onChange, this->signalHolder_,
                                                false);
    settings.unifiedChatKickChatroomId.connect(onChange, this->signalHolder_,
                                               false);
    settings.unifiedChatYouTubeEnabled.connect(onChange, this->signalHolder_,
                                               false);
    settings.unifiedChatYouTubeSource.connect(onChange, this->signalHolder_,
                                              false);
    settings.unifiedChatYouTubeApiKey.connect(onChange, this->signalHolder_,
                                              false);

    this->applySettings();
}

void UnifiedChatController::applySettings()
{
    assertInGuiThread();
    const auto &s = *getSettings();

    const bool enabled = s.unifiedChatEnabled;
    qCDebug(LOG) << "applySettings enabled=" << enabled
                 << "kick=" << s.unifiedChatKickEnabled.getValue()
                 << "slug=" << s.unifiedChatKickChannelSlug.getValue()
                 << "youtube=" << s.unifiedChatYouTubeEnabled.getValue()
                 << "source=" << s.unifiedChatYouTubeSource.getValue();

    // Kick
    QString kickSlug;
    if (enabled && s.unifiedChatKickEnabled)
    {
        kickSlug = s.unifiedChatKickChannelSlug.getValue().trimmed().toLower();
    }
    if (kickSlug != this->runningKickSlug_)
    {
        this->stopKick();
        if (!kickSlug.isEmpty())
        {
            this->startKick(kickSlug);
        }
    }

    // YouTube
    QString ytSource;
    if (enabled && s.unifiedChatYouTubeEnabled)
    {
        ytSource = s.unifiedChatYouTubeSource.getValue().trimmed();
    }
    if (ytSource != this->runningYouTubeSource_)
    {
        this->stopYouTube();
        if (!ytSource.isEmpty())
        {
            this->startYouTube(ytSource);
        }
    }

    if (!enabled)
    {
        qCDebug(LOG) << "Unified chat disabled";
    }
}

void UnifiedChatController::startKick(const QString &slug)
{
    // Fase 5 creates the Kick client here.
    qCInfo(LOG) << "Kick: would connect to" << slug;
    this->runningKickSlug_ = slug;
}

void UnifiedChatController::stopKick()
{
    if (this->runningKickSlug_.isEmpty())
    {
        return;
    }
    qCInfo(LOG) << "Kick: disconnecting from" << this->runningKickSlug_;
    this->runningKickSlug_.clear();
}

void UnifiedChatController::startYouTube(const QString &source)
{
    // Fase 6 creates the YouTube client here.
    qCInfo(LOG) << "YouTube: would connect to" << source;
    this->runningYouTubeSource_ = source;
}

void UnifiedChatController::stopYouTube()
{
    if (this->runningYouTubeSource_.isEmpty())
    {
        return;
    }
    qCInfo(LOG) << "YouTube: disconnecting from" << this->runningYouTubeSource_;
    this->runningYouTubeSource_.clear();
}

void UnifiedChatController::enqueue(ExternalChatMessage message)
{
    // Fase 5/6 build and inject the message; for now it is only logged.
    qCDebug(LOG) << "Dropping message from" << platformName(message.platform)
                 << "by" << message.authorName;
}

}  // namespace chatterino::unified
