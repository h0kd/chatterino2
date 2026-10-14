// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/youtube/YouTubeChatParser.hpp"

#include "common/Literals.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringBuilder>
#include <QUrl>
#include <QUrlQuery>

namespace {

using namespace chatterino;
using namespace chatterino::literals;
using namespace chatterino::youtube;
using unified::ChatPlatform;
using unified::ExternalChatFragment;
using unified::ExternalChatMessage;

const QRegularExpression VIDEO_ID_RE(R"(^[A-Za-z0-9_-]{11}$)");

QString simpleText(const QJsonValue &value)
{
    auto obj = value.toObject();
    if (obj.contains("simpleText"))
    {
        return obj.value("simpleText").toString();
    }
    QString out;
    for (const auto &run : obj.value("runs").toArray())
    {
        out += run.toObject().value("text").toString();
    }
    return out;
}

/// Finds a JSON object assigned in a script, e.g. `ytInitialData = {...};`
std::optional<QJsonObject> extractAssignedJson(const QString &html,
                                               const QString &needle)
{
    auto start = html.indexOf(needle);
    if (start < 0)
    {
        return std::nullopt;
    }
    start = html.indexOf(u'{', start);
    if (start < 0)
    {
        return std::nullopt;
    }
    auto end = html.indexOf(u"};</script>"_s, start);
    if (end < 0)
    {
        return std::nullopt;
    }
    auto doc =
        QJsonDocument::fromJson(html.mid(start, end - start + 1).toUtf8());
    if (!doc.isObject())
    {
        return std::nullopt;
    }
    return doc.object();
}

QString firstContinuation(const QJsonArray &continuations)
{
    for (const auto &value : continuations)
    {
        auto obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            auto cont = it.value().toObject().value("continuation").toString();
            if (!cont.isEmpty())
            {
                return cont;
            }
        }
    }
    return {};
}

void appendRuns(ExternalChatMessage &msg, const QJsonArray &runs)
{
    for (const auto &runValue : runs)
    {
        auto run = runValue.toObject();
        if (run.contains("emoji"))
        {
            auto emoji = run.value("emoji").toObject();
            auto thumbnails =
                emoji.value("image").toObject().value("thumbnails").toArray();
            auto shortcuts = emoji.value("shortcuts").toArray();
            QString name = shortcuts.isEmpty()
                               ? emoji.value("emojiId").toString()
                               : shortcuts.first().toString();
            if (emoji.value("isCustomEmoji").toBool() && !thumbnails.isEmpty())
            {
                // Use the largest thumbnail.
                auto url = thumbnails.last().toObject().value("url").toString();
                msg.fragments.push_back(
                    ExternalChatFragment::makeEmote(name, url));
            }
            else
            {
                // Standard unicode emoji: emojiId is the character itself.
                msg.fragments.push_back(ExternalChatFragment::makeText(
                    emoji.value("emojiId").toString()));
            }
        }
        else
        {
            auto text = run.value("text").toString();
            if (!text.isEmpty())
            {
                msg.fragments.push_back(ExternalChatFragment::makeText(text));
            }
        }
    }
}

void fillCommon(ExternalChatMessage &msg, const QJsonObject &r)
{
    msg.platform = ChatPlatform::YouTube;
    msg.id = r.value("id").toString();
    msg.authorName = simpleText(r.value("authorName"));
    auto channelId = r.value("authorExternalChannelId").toString();
    if (!channelId.isEmpty())
    {
        msg.authorUrl = u"https://www.youtube.com/channel/" % channelId;
    }
    auto usec = r.value("timestampUsec").toString().toLongLong();
    if (usec > 0)
    {
        msg.timestamp = QDateTime::fromMSecsSinceEpoch(usec / 1000);
    }
    else
    {
        msg.timestamp = QDateTime::currentDateTime();
    }
    for (const auto &badgeValue : r.value("authorBadges").toArray())
    {
        auto badge = badgeValue.toObject()
                         .value("liveChatAuthorBadgeRenderer")
                         .toObject();
        auto iconType =
            badge.value("icon").toObject().value("iconType").toString();
        if (iconType == u"OWNER")
        {
            msg.isOwner = true;
        }
        else if (iconType == u"MODERATOR")
        {
            msg.isMod = true;
        }
        else if (badge.contains("customThumbnail"))
        {
            msg.isMember = true;
        }
    }
}

}  // namespace

namespace chatterino::youtube {

std::optional<Source> parseSource(const QString &rawInput)
{
    auto input = rawInput.trimmed();
    if (input.isEmpty())
    {
        return std::nullopt;
    }
    if (VIDEO_ID_RE.match(input).hasMatch())
    {
        return Source{.videoId = input, .liveUrl = {}};
    }

    auto withScheme = input;
    if (!withScheme.contains(u"://"))
    {
        if (withScheme.startsWith(u'@'))
        {
            return Source{
                .videoId = {},
                .liveUrl = u"https://www.youtube.com/" % withScheme % u"/live"};
        }
        if (!withScheme.contains(u'/') && !withScheme.contains(u'.'))
        {
            // bare handle without @
            return Source{.videoId = {},
                          .liveUrl = u"https://www.youtube.com/@" % withScheme %
                                     u"/live"};
        }
        withScheme = u"https://" % withScheme;
    }

    QUrl url(withScheme);
    if (!url.isValid())
    {
        return std::nullopt;
    }
    auto host = url.host().toLower();
    auto path = url.path();
    if (host == u"youtu.be")
    {
        auto id = path.mid(1).section(u'/', 0, 0);
        if (VIDEO_ID_RE.match(id).hasMatch())
        {
            return Source{.videoId = id, .liveUrl = {}};
        }
        return std::nullopt;
    }
    if (!host.endsWith(u"youtube.com"))
    {
        return std::nullopt;
    }
    QUrlQuery query(url);
    auto v = query.queryItemValue("v");
    if (VIDEO_ID_RE.match(v).hasMatch())
    {
        return Source{.videoId = v, .liveUrl = {}};
    }
    if (path.startsWith(u"/live/") || path.startsWith(u"/shorts/"))
    {
        auto id = path.section(u'/', 2, 2);
        if (VIDEO_ID_RE.match(id).hasMatch())
        {
            return Source{.videoId = id, .liveUrl = {}};
        }
    }
    // Channel page: /@handle[/...], /channel/UC...[/...], /c/name, /user/name
    auto first = path.section(u'/', 1, 1);
    if (first.startsWith(u'@'))
    {
        return Source{
            .videoId = {},
            .liveUrl = u"https://www.youtube.com/" % first % u"/live"};
    }
    if (first == u"channel" || first == u"c" || first == u"user")
    {
        auto second = path.section(u'/', 2, 2);
        if (!second.isEmpty())
        {
            return Source{.videoId = {},
                          .liveUrl = u"https://www.youtube.com/" % first %
                                     u"/" % second % u"/live"};
        }
    }
    return std::nullopt;
}

std::optional<QString> extractLiveVideoId(const QString &html)
{
    static const QRegularExpression CANONICAL(
        R"re(<link rel="canonical" href="https://www\.youtube\.com/watch\?v=([A-Za-z0-9_-]{11})")re");
    auto match = CANONICAL.match(html);
    if (match.hasMatch())
    {
        return match.captured(1);
    }
    return std::nullopt;
}

std::optional<InnertubeSession> parseLiveChatPage(const QString &html)
{
    static const QRegularExpression API_KEY(
        R"re("INNERTUBE_API_KEY":"([^"]+)")re");
    static const QRegularExpression CLIENT_VERSION(
        R"re("INNERTUBE_CLIENT_VERSION":"([^"]+)")re");

    InnertubeSession session;
    auto keyMatch = API_KEY.match(html);
    auto versionMatch = CLIENT_VERSION.match(html);
    if (!keyMatch.hasMatch() || !versionMatch.hasMatch())
    {
        return std::nullopt;
    }
    session.apiKey = keyMatch.captured(1);
    session.clientVersion = versionMatch.captured(1);

    auto initial = extractAssignedJson(html, u"ytInitialData"_s);
    if (initial)
    {
        auto renderer = initial->value("contents")
                            .toObject()
                            .value("liveChatRenderer")
                            .toObject();
        session.continuation =
            firstContinuation(renderer.value("continuations").toArray());
    }
    if (session.continuation.isEmpty())
    {
        static const QRegularExpression CONTINUATION(
            R"re("continuation":"([^"]+)")re");
        auto contMatch = CONTINUATION.match(html);
        if (contMatch.hasMatch())
        {
            session.continuation = contMatch.captured(1);
        }
    }
    if (session.continuation.isEmpty())
    {
        return std::nullopt;
    }
    return session;
}

std::optional<ExternalChatMessage> parseItem(const QJsonObject &item)
{
    if (item.contains("liveChatTextMessageRenderer"))
    {
        auto r = item.value("liveChatTextMessageRenderer").toObject();
        ExternalChatMessage msg;
        fillCommon(msg, r);
        appendRuns(msg, r.value("message").toObject().value("runs").toArray());
        return msg;
    }
    if (item.contains("liveChatPaidMessageRenderer"))
    {
        auto r = item.value("liveChatPaidMessageRenderer").toObject();
        ExternalChatMessage msg;
        fillCommon(msg, r);
        auto amount = simpleText(r.value("purchaseAmountText"));
        msg.fragments.push_back(
            ExternalChatFragment::makeText(u"[Super Chat " % amount % u"]"));
        appendRuns(msg, r.value("message").toObject().value("runs").toArray());
        return msg;
    }
    if (item.contains("liveChatPaidStickerRenderer"))
    {
        auto r = item.value("liveChatPaidStickerRenderer").toObject();
        ExternalChatMessage msg;
        fillCommon(msg, r);
        auto amount = simpleText(r.value("purchaseAmountText"));
        msg.fragments.push_back(
            ExternalChatFragment::makeText(u"[Super Sticker " % amount % u"]"));
        return msg;
    }
    if (item.contains("liveChatMembershipItemRenderer"))
    {
        auto r = item.value("liveChatMembershipItemRenderer").toObject();
        ExternalChatMessage msg;
        fillCommon(msg, r);
        msg.isMember = true;
        auto header = simpleText(r.value("headerSubtext"));
        if (header.isEmpty())
        {
            header = simpleText(r.value("headerPrimaryText"));
        }
        msg.fragments.push_back(
            ExternalChatFragment::makeText(u"[Membership] " % header));
        appendRuns(msg, r.value("message").toObject().value("runs").toArray());
        return msg;
    }
    return std::nullopt;
}

std::vector<ExternalChatMessage> parseActions(const QJsonArray &actions)
{
    std::vector<ExternalChatMessage> out;
    for (const auto &actionValue : actions)
    {
        auto item = actionValue.toObject()
                        .value("addChatItemAction")
                        .toObject()
                        .value("item")
                        .toObject();
        if (item.isEmpty())
        {
            continue;
        }
        auto msg = parseItem(item);
        if (msg && !msg->id.isEmpty())
        {
            out.push_back(std::move(*msg));
        }
    }
    return out;
}

PollResult parseGetLiveChat(const QJsonObject &root)
{
    PollResult result;
    auto continuationContents = root.value("continuationContents").toObject();
    auto live = continuationContents.value("liveChatContinuation").toObject();
    if (live.isEmpty())
    {
        // No continuationContents: the stream ended or the chat was closed.
        return result;
    }
    result.messages = parseActions(live.value("actions").toArray());

    for (const auto &value : live.value("continuations").toArray())
    {
        auto obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            auto data = it.value().toObject();
            auto cont = data.value("continuation").toString();
            if (cont.isEmpty())
            {
                continue;
            }
            result.continuation = cont;
            auto timeout = data.value("timeoutMs").toInt(0);
            if (timeout > 0)
            {
                result.timeoutMs = timeout;
            }
            break;
        }
        if (!result.continuation.isEmpty())
        {
            break;
        }
    }
    return result;
}

DataApiPollResult parseDataApiMessages(const QJsonObject &root)
{
    DataApiPollResult result;
    result.nextPageToken = root.value("nextPageToken").toString();
    result.pollingIntervalMs = root.value("pollingIntervalMillis").toInt(5000);
    result.ended = root.value("offlineAt").isString();
    for (const auto &itemValue : root.value("items").toArray())
    {
        auto item = itemValue.toObject();
        auto snippet = item.value("snippet").toObject();
        auto author = item.value("authorDetails").toObject();
        ExternalChatMessage msg;
        msg.platform = ChatPlatform::YouTube;
        msg.id = item.value("id").toString();
        msg.authorName = author.value("displayName").toString();
        msg.authorUrl = author.value("channelUrl").toString();
        msg.isOwner = author.value("isChatOwner").toBool();
        msg.isMod = author.value("isChatModerator").toBool();
        msg.isMember = author.value("isChatSponsor").toBool();
        msg.timestamp = QDateTime::fromString(
            snippet.value("publishedAt").toString(), Qt::ISODateWithMs);
        if (!msg.timestamp.isValid())
        {
            msg.timestamp = QDateTime::currentDateTime();
        }
        auto type = snippet.value("type").toString();
        if (type == u"superChatEvent")
        {
            auto details = snippet.value("superChatDetails").toObject();
            msg.fragments.push_back(ExternalChatFragment::makeText(
                u"[Super Chat " %
                details.value("amountDisplayString").toString() % u"]"));
        }
        auto text = snippet.value("displayMessage").toString();
        if (!text.isEmpty())
        {
            msg.fragments.push_back(ExternalChatFragment::makeText(text));
        }
        if (msg.id.isEmpty() || msg.authorName.isEmpty())
        {
            continue;
        }
        result.messages.push_back(std::move(msg));
    }
    return result;
}

}  // namespace chatterino::youtube
