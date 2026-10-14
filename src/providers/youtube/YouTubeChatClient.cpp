// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/youtube/YouTubeChatClient.hpp"

#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "util/PostToThread.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringBuilder>
#include <QUrl>
#include <QUrlQuery>

namespace {

using namespace chatterino;
using namespace chatterino::literals;
using namespace chatterino::youtube;

const auto &LOG = chatterinoUnifiedChat;

// YouTube serves a stub page to unknown user agents; pretend to be a browser.
const char *const BROWSER_UA =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";

constexpr int WAIT_FOR_LIVE_MS = 60 * 1000;
constexpr int MIN_POLL_MS = 1000;
constexpr int MAX_POLL_MS = 30 * 1000;
constexpr int MAX_CONSECUTIVE_ERRORS = 5;
constexpr int MAX_CHAT_PAGE_FAILURES = 2;

NetworkRequest browserRequest(const QString &url,
                              NetworkRequestType type = NetworkRequestType::Get)
{
    return NetworkRequest(url, type)
        .timeout(20 * 1000)
        .header("User-Agent", BROWSER_UA)
        .header("Accept-Language", "en-US,en;q=0.9")
        .header("Cookie", "CONSENT=YES+cb; SOCS=CAI")
        .followRedirects(true);
}

}  // namespace

namespace chatterino::youtube {

YouTubeChatClient::YouTubeChatClient(Source source, QString apiKey,
                                     MessageCallback onMessage,
                                     StatusCallback onStatus)
    : source_(std::move(source))
    , apiKey_(std::move(apiKey).trimmed())
    , onMessage_(std::move(onMessage))
    , onStatus_(std::move(onStatus))
{
    this->timer_.setSingleShot(true);
}

YouTubeChatClient::~YouTubeChatClient()
{
    this->stop();
}

void YouTubeChatClient::start()
{
    assertInGuiThread();
    this->stopped_ = false;
    this->resolveVideo();
}

void YouTubeChatClient::stop()
{
    assertInGuiThread();
    this->stopped_ = true;
    this->timer_.stop();
}

void YouTubeChatClient::scheduleNext(int delayMs,
                                     void (YouTubeChatClient::*fn)())
{
    assertInGuiThread();
    if (this->stopped_)
    {
        return;
    }
    std::weak_ptr<YouTubeChatClient> weak = this->weak_from_this();
    this->timer_.disconnect();
    QObject::connect(&this->timer_, &QTimer::timeout, [weak, fn] {
        auto self = weak.lock();
        if (!self || self->stopped_)
        {
            return;
        }
        (self.get()->*fn)();
    });
    this->timer_.start(delayMs);
}

void YouTubeChatClient::status(const QString &text)
{
    if (this->onStatus_)
    {
        this->onStatus_(text);
    }
}

void YouTubeChatClient::deliver(
    std::vector<unified::ExternalChatMessage> messages)
{
    if (!this->onMessage_)
    {
        return;
    }
    for (auto &msg : messages)
    {
        this->onMessage_(std::move(msg));
    }
}

// --- Resolution --------------------------------------------------------------

void YouTubeChatClient::resolveVideo()
{
    assertInGuiThread();
    if (this->stopped_)
    {
        return;
    }
    this->usingDataApi_ = false;
    this->consecutiveErrors_ = 0;

    if (!this->source_.videoId.isEmpty())
    {
        this->videoId_ = this->source_.videoId;
        this->loadChatPage();
        return;
    }

    std::weak_ptr<YouTubeChatClient> weak = this->weak_from_this();
    browserRequest(this->source_.liveUrl)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            auto id = extractLiveVideoId(QString::fromUtf8(result.getData()));
            if (!id)
            {
                self->waitForLive(u"esperando transmision en vivo..."_s);
                return;
            }
            if (*id != self->videoId_)
            {
                qCInfo(LOG) << "YouTube: live video is" << *id;
            }
            self->videoId_ = *id;
            self->loadChatPage();
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            self->waitForLive(u"no se pudo cargar la pagina del canal (" %
                              result.formatError() % u")");
        })
        .execute();
}

void YouTubeChatClient::waitForLive(const QString &reason)
{
    assertInGuiThread();
    qCInfo(LOG) << "YouTube:" << reason;
    this->status(u"YouTube: " % reason);
    this->videoId_.clear();
    this->session_ = {};
    this->scheduleNext(WAIT_FOR_LIVE_MS, &YouTubeChatClient::resolveVideo);
}

// --- Backend A: Innertube ----------------------------------------------------

void YouTubeChatClient::loadChatPage()
{
    assertInGuiThread();
    if (this->stopped_)
    {
        return;
    }
    QString url =
        u"https://www.youtube.com/live_chat?is_popout=1&v=" % this->videoId_;
    std::weak_ptr<YouTubeChatClient> weak = this->weak_from_this();
    browserRequest(url)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            auto session =
                parseLiveChatPage(QString::fromUtf8(result.getData()));
            if (!session)
            {
                self->chatPageFailures_++;
                qCWarning(LOG)
                    << "YouTube: live chat page for" << self->videoId_
                    << "has no chat (ended, disabled or format "
                       "changed), failures:"
                    << self->chatPageFailures_;
                if (!self->apiKey_.isEmpty() &&
                    self->chatPageFailures_ >= MAX_CHAT_PAGE_FAILURES)
                {
                    self->startDataApi();
                    return;
                }
                self->waitForLive(
                    u"el chat de este video no esta disponible, reintentando..."_s);
                return;
            }
            self->chatPageFailures_ = 0;
            self->session_ = *session;
            self->consecutiveErrors_ = 0;
            self->backoff_.reset();
            self->status(u"YouTube conectado al chat de " % self->videoId_);
            self->poll();
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            self->waitForLive(u"no se pudo cargar el chat (" %
                              result.formatError() % u")");
        })
        .execute();
}

void YouTubeChatClient::poll()
{
    assertInGuiThread();
    if (this->stopped_ || this->session_.continuation.isEmpty())
    {
        return;
    }

    QJsonObject body{
        {"context",
         QJsonObject{{"client",
                      QJsonObject{
                          {"clientName", "WEB"},
                          {"clientVersion", this->session_.clientVersion},
                          {"hl", "en"},
                          {"gl", "US"},
                      }}}},
        {"continuation", this->session_.continuation},
    };
    QString url = u"https://www.youtube.com/youtubei/v1/live_chat/"
                  u"get_live_chat?prettyPrint=false&key=" %
                  this->session_.apiKey;

    std::weak_ptr<YouTubeChatClient> weak = this->weak_from_this();
    browserRequest(url, NetworkRequestType::Post)
        .json(body)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            auto parsed = parseGetLiveChat(result.parseJson());
            self->consecutiveErrors_ = 0;
            self->backoff_.reset();
            self->deliver(std::move(parsed.messages));
            if (parsed.continuation.isEmpty())
            {
                self->waitForLive(
                    u"la transmision termino, esperando una nueva..."_s);
                return;
            }
            self->session_.continuation = parsed.continuation;
            auto delay = std::clamp(parsed.timeoutMs, MIN_POLL_MS, MAX_POLL_MS);
            self->scheduleNext(delay, &YouTubeChatClient::poll);
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            self->consecutiveErrors_++;
            qCWarning(LOG) << "YouTube: poll failed" << result.formatError()
                           << "errors:" << self->consecutiveErrors_;
            if (self->consecutiveErrors_ >= MAX_CONSECUTIVE_ERRORS)
            {
                // Continuation may be stale: start over from the chat page.
                self->consecutiveErrors_ = 0;
                self->status(u"YouTube: reconectando al chat..."_s);
                self->resolveVideo();
                return;
            }
            auto delay = static_cast<int>(self->backoff_.next().count());
            self->scheduleNext(delay, &YouTubeChatClient::poll);
        })
        .execute();
}

// --- Backend B: Data API v3 --------------------------------------------------

void YouTubeChatClient::startDataApi()
{
    assertInGuiThread();
    if (this->stopped_)
    {
        return;
    }
    this->usingDataApi_ = true;
    this->liveChatId_.clear();
    this->pageToken_.clear();
    qCInfo(LOG) << "YouTube: falling back to Data API v3 for" << this->videoId_;

    QUrl url(u"https://www.googleapis.com/youtube/v3/videos"_s);
    QUrlQuery query;
    query.addQueryItem("part", "liveStreamingDetails");
    query.addQueryItem("id", this->videoId_);
    query.addQueryItem("key", this->apiKey_);
    url.setQuery(query);

    std::weak_ptr<YouTubeChatClient> weak = this->weak_from_this();
    NetworkRequest(url)
        .timeout(15 * 1000)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            auto items = result.parseJson().value("items").toArray();
            QString chatId;
            if (!items.isEmpty())
            {
                chatId = items.first()
                             .toObject()
                             .value("liveStreamingDetails")
                             .toObject()
                             .value("activeLiveChatId")
                             .toString();
            }
            if (chatId.isEmpty())
            {
                self->waitForLive(
                    u"la Data API no reporta un chat activo para este video"_s);
                return;
            }
            self->liveChatId_ = chatId;
            self->status(u"YouTube conectado (Data API) al chat de " %
                         self->videoId_);
            self->pollDataApi();
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            self->waitForLive(u"error de la Data API (" % result.formatError() %
                              u")");
        })
        .execute();
}

void YouTubeChatClient::pollDataApi()
{
    assertInGuiThread();
    if (this->stopped_ || this->liveChatId_.isEmpty())
    {
        return;
    }
    QUrl url(u"https://www.googleapis.com/youtube/v3/liveChat/messages"_s);
    QUrlQuery query;
    query.addQueryItem("liveChatId", this->liveChatId_);
    query.addQueryItem("part", "snippet,authorDetails");
    query.addQueryItem("maxResults", "200");
    query.addQueryItem("key", this->apiKey_);
    if (!this->pageToken_.isEmpty())
    {
        query.addQueryItem("pageToken", this->pageToken_);
    }
    url.setQuery(query);

    std::weak_ptr<YouTubeChatClient> weak = this->weak_from_this();
    NetworkRequest(url)
        .timeout(15 * 1000)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            auto parsed = parseDataApiMessages(result.parseJson());
            // The first page is history; deliver it anyway, the controller
            // deduplicates and the user wants the backlog on connect.
            self->deliver(std::move(parsed.messages));
            if (parsed.ended)
            {
                self->waitForLive(
                    u"la transmision termino, esperando una nueva..."_s);
                return;
            }
            self->pageToken_ = parsed.nextPageToken;
            auto delay =
                std::clamp(parsed.pollingIntervalMs, MIN_POLL_MS, MAX_POLL_MS);
            self->scheduleNext(delay, &YouTubeChatClient::pollDataApi);
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            qCWarning(LOG) << "YouTube: Data API poll failed"
                           << result.formatError();
            // 403 here usually means the daily quota is exhausted.
            self->waitForLive(u"error de la Data API (" % result.formatError() %
                              u"), reintentando"_s);
        })
        .execute();
}

}  // namespace chatterino::youtube
