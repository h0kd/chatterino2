// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/unified/ExternalChatMessage.hpp"

namespace chatterino::unified {

QString platformName(ChatPlatform platform)
{
    switch (platform)
    {
        case ChatPlatform::Twitch:
            return QStringLiteral("Twitch");
        case ChatPlatform::Kick:
            return QStringLiteral("Kick");
        case ChatPlatform::YouTube:
            return QStringLiteral("YouTube");
    }
    return {};
}

QString ExternalChatMessage::plainText() const
{
    QString out;
    for (const auto &fragment : this->fragments)
    {
        if (!out.isEmpty() && fragment.isEmote())
        {
            out += ' ';
        }
        out += fragment.text;
        if (fragment.isEmote())
        {
            out += ' ';
        }
    }
    return out.trimmed();
}

}  // namespace chatterino::unified
