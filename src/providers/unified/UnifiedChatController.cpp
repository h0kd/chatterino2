// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/unified/UnifiedChatController.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "common/Channel.hpp"
#include "common/Literals.hpp"
#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "messages/Link.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "providers/kick/KickChatClient.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "providers/unified/PlatformBadge.hpp"
#include "providers/youtube/YouTubeChatClient.hpp"
#include "providers/youtube/YouTubeChatParser.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"
#include "util/PostToThread.hpp"

#include <IrcTagsRef>
#include <QStringBuilder>
#include <QVariantMap>

namespace {

using namespace chatterino;
using namespace chatterino::unified;
using namespace chatterino::literals;

const auto &LOG = chatterinoUnifiedChat;

constexpr size_t MAX_SEEN_IDS = 500;

/// Kick emotes are served as 70x70 "fullsize" images; YouTube emoji
/// thumbnails are smaller. Scale so that they render at roughly the same
/// height as Twitch emotes (28px at 1x).
constexpr qreal KICK_EMOTE_SCALE = 70.0 / 28.0;
/// Largest YouTube custom emoji thumbnail is 48px; show it emoji-sized.
constexpr qreal YOUTUBE_EMOTE_SCALE = 48.0 / 24.0;

QString platformIdPrefix(ChatPlatform platform)
{
    switch (platform)
    {
        case ChatPlatform::Twitch:
            return u"twitch:"_s;
        case ChatPlatform::Kick:
            return u"kick:"_s;
        case ChatPlatform::YouTube:
            return u"youtube:"_s;
    }
    return {};
}

EmotePtr makeExternalEmote(ChatPlatform platform,
                           const ExternalChatFragment &fragment)
{
    qreal scale =
        platform == ChatPlatform::Kick ? KICK_EMOTE_SCALE : YOUTUBE_EMOTE_SCALE;
    return std::make_shared<const Emote>(Emote{
        .name = EmoteName{fragment.text},
        .images = ImageSet{Image::fromUrl(Url{fragment.emoteUrl}, scale)},
        .tooltip = Tooltip{fragment.text % u"<br>" % platformName(platform) %
                           u" Emote"},
        .homePage = Url{fragment.emoteUrl},
    });
}

}  // namespace

namespace chatterino::unified {

UnifiedChatController::UnifiedChatController() = default;

UnifiedChatController::~UnifiedChatController()
{
    *this->alive_ = false;
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

    // Kick: the "running" key includes the manual chatroom id so that
    // changing it restarts the client.
    QString kickKey;
    if (enabled && s.unifiedChatKickEnabled)
    {
        auto slug = s.unifiedChatKickChannelSlug.getValue().trimmed().toLower();
        if (!slug.isEmpty())
        {
            kickKey =
                slug % u"|" % s.unifiedChatKickChatroomId.getValue().trimmed();
        }
    }
    if (kickKey != this->runningKickSlug_)
    {
        this->stopKick();
        if (!kickKey.isEmpty())
        {
            this->startKick(kickKey);
        }
    }

    // YouTube: the "running" key includes the API key so that changing it
    // restarts the client.
    QString ytKey;
    if (enabled && s.unifiedChatYouTubeEnabled)
    {
        auto source = s.unifiedChatYouTubeSource.getValue().trimmed();
        if (!source.isEmpty())
        {
            ytKey =
                source % u"|" % s.unifiedChatYouTubeApiKey.getValue().trimmed();
        }
    }
    if (ytKey != this->runningYouTubeSource_)
    {
        this->stopYouTube();
        if (!ytKey.isEmpty())
        {
            this->startYouTube(ytKey);
        }
    }

    if (!enabled)
    {
        qCDebug(LOG) << "Unified chat disabled";
    }
}

void UnifiedChatController::startKick(const QString &key)
{
    assertInGuiThread();
    auto slug = key.section(u'|', 0, 0);
    auto manualId = key.section(u'|', 1, 1);
    qCInfo(LOG) << "Kick: starting client for" << slug;

    std::weak_ptr<bool> alive = this->alive_;
    this->kick_ = std::make_shared<kick::KickChatClient>(
        slug, manualId,
        [this, alive](ExternalChatMessage message) {
            if (alive.lock())
            {
                this->enqueue(std::move(message));
            }
        },
        [this, alive](const QString &text) {
            if (alive.lock())
            {
                this->addSystemMessage(text);
            }
        });
    this->kick_->start();
    this->runningKickSlug_ = key;
}

void UnifiedChatController::stopKick()
{
    if (this->runningKickSlug_.isEmpty())
    {
        return;
    }
    qCInfo(LOG) << "Kick: stopping client for" << this->runningKickSlug_;
    if (this->kick_)
    {
        this->kick_->stop();
        this->kick_.reset();
    }
    this->addSystemMessage(u"Kick desconectado."_s);
    this->runningKickSlug_.clear();
}

void UnifiedChatController::startYouTube(const QString &key)
{
    assertInGuiThread();
    auto sourceText = key.section(u'|', 0, 0);
    auto apiKey = key.section(u'|', 1, 1);
    auto source = youtube::parseSource(sourceText);
    if (!source)
    {
        this->addSystemMessage(u"YouTube: no entiendo \"" % sourceText %
                               u"\". Usa @handle, URL del canal/video o "
                               u"video ID."_s);
        this->runningYouTubeSource_ = key;
        return;
    }
    qCInfo(LOG) << "YouTube: starting client for" << sourceText;

    std::weak_ptr<bool> alive = this->alive_;
    this->youtube_ = std::make_shared<youtube::YouTubeChatClient>(
        *source, apiKey,
        [this, alive](ExternalChatMessage message) {
            if (alive.lock())
            {
                this->enqueue(std::move(message));
            }
        },
        [this, alive](const QString &text) {
            if (alive.lock())
            {
                this->addSystemMessage(text);
            }
        });
    this->youtube_->start();
    this->runningYouTubeSource_ = key;
}

void UnifiedChatController::stopYouTube()
{
    if (this->runningYouTubeSource_.isEmpty())
    {
        return;
    }
    qCInfo(LOG) << "YouTube: stopping client";
    if (this->youtube_)
    {
        this->youtube_->stop();
        this->youtube_.reset();
        this->addSystemMessage(u"YouTube desconectado."_s);
    }
    this->runningYouTubeSource_.clear();
}

void UnifiedChatController::enqueue(ExternalChatMessage message)
{
    std::weak_ptr<bool> alive = this->alive_;
    runInGuiThread([this, alive, message = std::move(message)] {
        if (!alive.lock())
        {
            return;
        }
        this->inject(message);
    });
}

ChannelPtr UnifiedChatController::targetChannel() const
{
    auto name = targetTwitchChannelName();
    if (name.isEmpty())
    {
        return nullptr;
    }
    auto channel = getApp()->getTwitch()->getChannelOrEmpty(name);
    if (!channel || channel->isEmpty())
    {
        return nullptr;
    }
    return channel;
}

void UnifiedChatController::addSystemMessage(const QString &text)
{
    assertInGuiThread();
    qCInfo(LOG) << text;
    auto channel = this->targetChannel();
    if (channel)
    {
        channel->addSystemMessage(text);
    }
}

bool UnifiedChatController::markSeen(ChatPlatform platform, const QString &id)
{
    auto &seen = this->seen_.at(static_cast<size_t>(platform));
    if (seen.set.contains(id))
    {
        return false;
    }
    seen.set.insert(id);
    seen.order.push_back(id);
    while (seen.order.size() > MAX_SEEN_IDS)
    {
        seen.set.remove(seen.order.front());
        seen.order.pop_front();
    }
    return true;
}

void UnifiedChatController::inject(const ExternalChatMessage &message)
{
    assertInGuiThread();
    if (!message.id.isEmpty() && !this->markSeen(message.platform, message.id))
    {
        return;
    }

    qCDebug(LOG) << platformName(message.platform) << "message from"
                 << message.authorName << ":" << message.plainText();

    auto channel = this->targetChannel();
    if (!channel)
    {
        qCDebug(LOG) << "Dropping" << platformName(message.platform)
                     << "message: target Twitch channel is not open";
        return;
    }

    auto [built, alert] =
        buildMessage(message, channel->getName(),
                     dynamic_cast<TwitchChannel *>(channel.get()));
    if (!built)
    {
        return;
    }

    MessageBuilder::triggerHighlights(channel.get(), alert);
    channel->addMessage(built, MessageContext::Original);
    if (built->flags.has(MessageFlag::ShowInMentions))
    {
        getApp()->getTwitch()->getMentionsChannel()->addMessage(
            built, MessageContext::Original);
    }
}

std::pair<MessagePtr, HighlightAlert> UnifiedChatController::buildMessage(
    const ExternalChatMessage &message, const QString &channelName,
    TwitchChannel *twitchChannel)
{
    assertInGuiThread();
    const auto &s = *getSettings();

    MessageBuilder builder;
    builder->id = platformIdPrefix(message.platform) % message.id;
    builder->serverReceivedTime = message.timestamp.isValid()
                                      ? message.timestamp
                                      : QDateTime::currentDateTime();
    builder->channelName = channelName;
    builder->loginName = message.authorName.toLower();
    builder->displayName = message.authorName;
    builder->usernameColor =
        message.authorColor.value_or(getRandomColor(message.authorName));
    builder->flags.set(MessageFlag::Collapsed);

    builder.emplace<TimestampElement>(builder->serverReceivedTime.time());

    if (s.unifiedChatShowPlatformBadges)
    {
        builder.emplace<BadgeElement>(platformBadge(message.platform),
                                      MessageElementFlag::BadgePlatform);
    }

    // Username. Links to the author's profile on the origin platform instead
    // of opening the Twitch user popup.
    auto *username = builder.emplace<TextElement>(
        message.authorName % u":", MessageElementFlag::Username,
        MessageColor(builder->usernameColor), FontStyle::ChatMediumBold);
    if (!message.authorUrl.isEmpty())
    {
        username->setLink({Link::Url, message.authorUrl});
    }

    for (const auto &fragment : message.fragments)
    {
        if (fragment.isEmote())
        {
            builder.emplace<EmoteElement>(
                makeExternalEmote(message.platform, fragment),
                MessageElementFlag::Emote, MessageColor::Text);
        }
        else
        {
            // Word by word so that FFZ/BTTV/7TV emotes of the target Twitch
            // channel (and the global ones) also apply to external messages.
            for (const auto &word :
                 fragment.text.split(u' ', Qt::SkipEmptyParts))
            {
                if (builder.tryAppendThirdPartyEmote(twitchChannel,
                                                     EmoteNameView{word}))
                {
                    continue;
                }
                builder.emplace<TextElement>(word, MessageElementFlag::Text,
                                             MessageColor::Text);
            }
        }
    }

    auto plain = message.plainText();
    builder->messageText = plain;
    builder->searchText = message.authorName % u": " % plain;

    QVariantMap tagsMap;
    Communi::TagsRef tags(tagsMap);
    auto alert = builder.parseHighlights(tags, plain, {});

    return {builder.release(), alert};
}

}  // namespace chatterino::unified
