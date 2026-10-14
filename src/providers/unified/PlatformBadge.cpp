// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/unified/PlatformBadge.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Settings.hpp"

#include <array>
#include <mutex>

namespace {

using namespace chatterino;
using namespace chatterino::unified;

// Icons in resources/unified/ are 36x36; rendered at half size (18px) like
// the other badges.
constexpr qreal ICON_SCALE = 0.5;

EmotePtr makeBadge(ChatPlatform platform)
{
    const auto &res = getResources().unified;
    const QPixmap *pixmap = nullptr;
    QString homePage;
    switch (platform)
    {
        case ChatPlatform::Twitch:
            pixmap = &res.twitch;
            homePage = "https://www.twitch.tv/";
            break;
        case ChatPlatform::Kick:
            pixmap = &res.kick;
            homePage = "https://kick.com/";
            break;
        case ChatPlatform::YouTube:
            pixmap = &res.youtube;
            homePage = "https://www.youtube.com/";
            break;
    }

    return std::make_shared<const Emote>(Emote{
        .name = EmoteName{},
        .images = ImageSet{Image::fromResourcePixmap(*pixmap, ICON_SCALE)},
        .tooltip = Tooltip{platformName(platform)},
        .homePage = Url{homePage},
    });
}

}  // namespace

namespace chatterino::unified {

EmotePtr platformBadge(ChatPlatform platform)
{
    static std::array<EmotePtr, 3> cache;
    static std::mutex mutex;

    std::lock_guard guard(mutex);
    auto &slot = cache.at(static_cast<size_t>(platform));
    if (!slot)
    {
        slot = makeBadge(platform);
    }
    return slot;
}

QString targetTwitchChannelName()
{
    auto name =
        getSettings()->unifiedChatTargetTwitchChannel.getValue().trimmed();
    if (name.startsWith('#'))
    {
        name.remove(0, 1);
    }
    if (name.isEmpty())
    {
        auto *app = tryGetApp();
        if (app == nullptr)
        {
            return {};
        }
        auto user = app->getAccounts()->twitch.getCurrent();
        if (!user || user->isAnon())
        {
            return {};
        }
        name = user->getUserName();
    }
    return name.toLower();
}

bool isUnifiedTwitchChannel(const QString &channelName)
{
    auto *settings = getSettings();
    if (!settings->unifiedChatEnabled)
    {
        return false;
    }
    if (!settings->unifiedChatKickEnabled &&
        !settings->unifiedChatYouTubeEnabled)
    {
        return false;
    }
    auto target = targetTwitchChannelName();
    return !target.isEmpty() &&
           channelName.compare(target, Qt::CaseInsensitive) == 0;
}

bool showTwitchPlatformBadge(const QString &channelName)
{
    return getSettings()->unifiedChatShowPlatformBadges &&
           isUnifiedTwitchChannel(channelName);
}

}  // namespace chatterino::unified
