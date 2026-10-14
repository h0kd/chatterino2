// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/unified/PlatformBadge.hpp"

#include "common/Aliases.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"

namespace chatterino::unified {

EmotePtr platformBadge(ChatPlatform platform)
{
    // Fase 3 replaces this with real icons loaded from resources/.
    static const auto EMPTY = std::make_shared<const Emote>(Emote{
        .name = EmoteName{},
        .images = ImageSet{Image::getEmpty()},
        .tooltip = Tooltip{platformName(platform)},
        .homePage = Url{},
    });
    return EMPTY;
}

}  // namespace chatterino::unified
