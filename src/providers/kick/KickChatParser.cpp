// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickChatParser.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringBuilder>

namespace chatterino::kick {

using unified::ChatPlatform;
using unified::ExternalChatFragment;
using unified::ExternalChatMessage;

std::vector<ExternalChatFragment> parseContent(const QString &content)
{
    static const QRegularExpression EMOTE_RE(R"(\[emote:(\d+):([^\]]*)\])");

    std::vector<ExternalChatFragment> out;
    qsizetype pos = 0;
    auto it = EMOTE_RE.globalMatch(content);
    while (it.hasNext())
    {
        auto match = it.next();
        if (match.capturedStart() > pos)
        {
            out.push_back(ExternalChatFragment::makeText(
                content.mid(pos, match.capturedStart() - pos)));
        }
        auto id = match.captured(1);
        auto name = match.captured(2);
        if (name.isEmpty())
        {
            name = id;
        }
        out.push_back(ExternalChatFragment::makeEmote(name, emoteUrl(id)));
        pos = match.capturedEnd();
    }
    if (pos < content.size())
    {
        out.push_back(ExternalChatFragment::makeText(content.mid(pos)));
    }
    return out;
}

QString emoteUrl(const QString &emoteId)
{
    return u"https://files.kick.com/emotes/" % emoteId % u"/fullsize";
}

std::optional<ExternalChatMessage> parseChatMessageEvent(
    const QJsonObject &data)
{
    auto id = data.value("id").toString();
    auto content = data.value("content").toString();
    auto sender = data.value("sender").toObject();
    auto username = sender.value("username").toString();
    if (id.isEmpty() || username.isEmpty())
    {
        return std::nullopt;
    }

    ExternalChatMessage msg;
    msg.platform = ChatPlatform::Kick;
    msg.id = id;
    msg.authorName = username;

    auto slug = sender.value("slug").toString();
    if (slug.isEmpty())
    {
        slug = username.toLower();
    }
    msg.authorUrl = u"https://kick.com/" % slug;

    auto identity = sender.value("identity").toObject();
    auto colorString = identity.value("color").toString();
    if (!colorString.isEmpty())
    {
        QColor color(colorString);
        if (color.isValid())
        {
            msg.authorColor = color;
        }
    }
    for (const auto &badgeValue : identity.value("badges").toArray())
    {
        auto type = badgeValue.toObject().value("type").toString();
        if (type == u"broadcaster")
        {
            msg.isOwner = true;
        }
        else if (type == u"moderator")
        {
            msg.isMod = true;
        }
        else if (type == u"subscriber" || type == u"founder" || type == u"og" ||
                 type == u"vip" || type == u"sub_gifter")
        {
            msg.isMember = true;
        }
    }

    auto createdAt = data.value("created_at").toString();
    msg.timestamp = QDateTime::fromString(createdAt, Qt::ISODate);
    if (!msg.timestamp.isValid())
    {
        msg.timestamp = QDateTime::currentDateTime();
    }
    msg.timestamp = msg.timestamp.toLocalTime();

    msg.fragments = parseContent(content);
    return msg;
}

}  // namespace chatterino::kick
