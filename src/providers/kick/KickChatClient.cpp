// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/kick/KickChatClient.hpp"

#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/kick/KickChatParser.hpp"
#include "singletons/Settings.hpp"
#include "util/PostToThread.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringBuilder>
#include <QUrl>
#include <QUuid>

namespace {

using namespace chatterino;
using namespace chatterino::kick;
using namespace chatterino::literals;

const auto &LOG = chatterinoUnifiedChat;

// Credentials kick.com's web client used on 2026-10-14. Only a fallback: the
// live ones are negotiated and cached in settings when these stop working.
const QString DEFAULT_PUSHER_KEY = u"34bf7a0ff419a2a775b9"_s;
const QString DEFAULT_PUSHER_CLUSTER = u"us2"_s;

constexpr int KEEPALIVE_INTERVAL_MS = 60 * 1000;
constexpr qint64 DEAD_CONNECTION_SECS = 150;
constexpr qint64 MIN_NEGOTIATION_INTERVAL_SECS = 60;

/// Forwards websocket events to the client on the GUI thread. A generation
/// counter lets the client ignore events from sockets it already replaced.
class Listener : public WebSocketListener
{
public:
    Listener(std::weak_ptr<KickChatClient> client, std::uint64_t generation)
        : client_(std::move(client))
        , generation_(generation)
    {
    }

    void onOpen() override
    {
        runInGuiThread([client = this->client_, gen = this->generation_] {
            if (auto strong = client.lock())
            {
                strong->handleOpen(gen);
            }
        });
    }

    void onTextMessage(QByteArray data) override
    {
        runInGuiThread([client = this->client_, gen = this->generation_,
                        data = std::move(data)] {
            if (auto strong = client.lock())
            {
                strong->handleText(gen, data);
            }
        });
    }

    void onBinaryMessage(QByteArray /*data*/) override
    {
    }

    void onClose(std::unique_ptr<WebSocketListener> self) override
    {
        runInGuiThread([client = this->client_, gen = this->generation_,
                        lifetime = std::move(self)] {
            if (auto strong = client.lock())
            {
                strong->handleClose(gen);
            }
        });
    }

private:
    std::weak_ptr<KickChatClient> client_;
    std::uint64_t generation_;
};

std::pair<QString, QString> currentPusherCredentials()
{
    const auto &s = *getSettings();
    auto key = s.unifiedChatKickPusherKey.getValue().trimmed();
    auto cluster = s.unifiedChatKickPusherCluster.getValue().trimmed();
    if (key.isEmpty() || cluster.isEmpty())
    {
        return {DEFAULT_PUSHER_KEY, DEFAULT_PUSHER_CLUSTER};
    }
    return {key, cluster};
}

}  // namespace

namespace chatterino::kick {

KickChatClient::KickChatClient(QString slug, QString manualChatroomId,
                               MessageCallback onMessage,
                               StatusCallback onStatus)
    : slug_(std::move(slug))
    , manualChatroomId_(std::move(manualChatroomId).trimmed())
    , onMessage_(std::move(onMessage))
    , onStatus_(std::move(onStatus))
{
    this->keepAlive_.setInterval(KEEPALIVE_INTERVAL_MS);
    QObject::connect(&this->keepAlive_, &QTimer::timeout, [this] {
        if (this->stopped_ || !this->connected_)
        {
            return;
        }
        auto idle = this->lastActivity_.secsTo(QDateTime::currentDateTime());
        if (idle > DEAD_CONNECTION_SECS)
        {
            qCWarning(LOG) << "Kick: no traffic for" << idle
                           << "s, reconnecting";
            this->handle_ = {};  // closes the socket -> handleClose
            return;
        }
        this->sendJson(R"({"event":"pusher:ping","data":{}})");
    });
}

KickChatClient::~KickChatClient()
{
    this->stop();
}

void KickChatClient::start()
{
    assertInGuiThread();
    this->stopped_ = false;
    this->status(u"Kick: conectando a " % this->slug_ % u"...");
    this->keepAlive_.start();

    if (!this->manualChatroomId_.isEmpty())
    {
        this->chatroomId_ = this->manualChatroomId_;
        this->connectSocket();
        return;
    }
    this->resolveChatroomId();
}

void KickChatClient::stop()
{
    assertInGuiThread();
    if (this->stopped_)
    {
        return;
    }
    this->stopped_ = true;
    this->keepAlive_.stop();
    this->generation_++;  // ignore callbacks from the socket being closed
    this->connected_ = false;
    this->handle_ = {};
}

void KickChatClient::resolveChatroomId()
{
    assertInGuiThread();
    QString url = u"https://kick.com/api/v2/channels/" % this->slug_;
    std::weak_ptr<KickChatClient> weak = this->weak_from_this();

    NetworkRequest(url)
        .timeout(10 * 1000)
        .header("Accept", "application/json")
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            auto root = result.parseJson();
            auto id = root.value("chatroom").toObject().value("id");
            if (!id.isDouble())
            {
                self->scheduleReconnect(
                    u"la respuesta de kick.com no trae chatroom.id"_s);
                return;
            }
            self->chatroomId_ =
                QString::number(static_cast<qint64>(id.toDouble()));
            qCDebug(LOG) << "Kick: chatroom id for" << self->slug_ << "is"
                         << self->chatroomId_;
            self->connectSocket();
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            self->scheduleReconnect(
                u"no se pudo obtener el chatroom id (" % result.formatError() %
                u"). Puedes ingresarlo a mano en los settings.");
        })
        .execute();
}

void KickChatClient::connectSocket()
{
    assertInGuiThread();
    if (this->stopped_)
    {
        return;
    }
    auto [key, cluster] = currentPusherCredentials();
    QUrl url(u"wss://ws-" % cluster % u".pusher.com/app/" % key %
             u"?protocol=7&client=js&version=8.4.0&flash=false");
    qCDebug(LOG) << "Kick: connecting to" << url.host() << "for chatroom"
                 << this->chatroomId_;

    this->generation_++;
    this->connected_ = false;
    this->handle_ = {};
    this->lastActivity_ = QDateTime::currentDateTime();
    this->handle_ = this->pool_.createSocket(
        WebSocketOptions{.url = url, .headers = {}},
        std::make_unique<Listener>(this->weak_from_this(), this->generation_));
}

void KickChatClient::scheduleReconnect(const QString &reason)
{
    assertInGuiThread();
    if (this->stopped_ || this->reconnectPending_)
    {
        return;
    }
    auto delay = this->backoff_.next();
    qCWarning(LOG) << "Kick:" << reason << "- retrying in" << delay.count()
                   << "ms";
    this->status(u"Kick: " % reason % u" Reintentando en " %
                 QString::number(delay.count() / 1000) % u" s.");
    this->reconnectPending_ = true;

    std::weak_ptr<KickChatClient> weak = this->weak_from_this();
    QTimer::singleShot(delay, &this->keepAlive_, [weak] {
        auto self = weak.lock();
        if (!self)
        {
            return;
        }
        self->reconnectPending_ = false;
        if (self->stopped_)
        {
            return;
        }
        if (self->chatroomId_.isEmpty())
        {
            self->resolveChatroomId();
        }
        else
        {
            self->connectSocket();
        }
    });
}

void KickChatClient::negotiateCredentials()
{
    assertInGuiThread();
    auto now = QDateTime::currentDateTime();
    if (this->lastNegotiation_.isValid() &&
        this->lastNegotiation_.secsTo(now) < MIN_NEGOTIATION_INTERVAL_SECS)
    {
        this->scheduleReconnect(
            u"credenciales de Pusher rechazadas (ya se renegociaron hace poco)."_s);
        return;
    }
    this->lastNegotiation_ = now;

    QJsonObject body{
        {"client",
         QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                     {"type", "web"}}},
        // Offering centrifugo as well makes Kick answer with centrifugo only.
        {"capabilities",
         QJsonObject{{"accepted_providers",
                      QJsonArray{QJsonObject{{"provider", "pusher"}}}}}},
    };

    std::weak_ptr<KickChatClient> weak = this->weak_from_this();
    NetworkRequest(u"https://web.kick.com/api/v1/realtime/connection"_s,
                   NetworkRequestType::Post)
        .timeout(10 * 1000)
        .header("Accept", "application/json")
        .json(body)
        .onSuccess([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            QString key;
            QString cluster;
            auto connections = result.parseJson()
                                   .value("data")
                                   .toObject()
                                   .value("connections")
                                   .toArray();
            for (const auto &value : connections)
            {
                auto conn = value.toObject();
                if (conn.value("provider").toString() != u"pusher")
                {
                    continue;
                }
                auto creds = conn.value("credentials").toObject();
                key = creds.value("app_key").toString();
                cluster = creds.value("cluster").toString();
                break;
            }
            static const QRegularExpression SAFE("^[A-Za-z0-9-]{2,64}$");
            if (!SAFE.match(key).hasMatch() || !SAFE.match(cluster).hasMatch())
            {
                self->scheduleReconnect(
                    u"Kick no devolvio credenciales de Pusher validas."_s);
                return;
            }
            qCInfo(LOG) << "Kick: negotiated pusher key" << key << "cluster"
                        << cluster;
            getSettings()->unifiedChatKickPusherKey = key;
            getSettings()->unifiedChatKickPusherCluster = cluster;
            self->backoff_.reset();
            self->connectSocket();
        })
        .onError([weak](const NetworkResult &result) {
            auto self = weak.lock();
            if (!self || self->stopped_)
            {
                return;
            }
            self->scheduleReconnect(u"no se pudieron renegociar las "
                                    u"credenciales de Pusher (" %
                                    result.formatError() % u").");
        })
        .execute();
}

void KickChatClient::sendJson(const QByteArray &payload)
{
    this->handle_.sendText(payload);
}

void KickChatClient::status(const QString &text)
{
    if (this->onStatus_)
    {
        this->onStatus_(text);
    }
}

void KickChatClient::handleOpen(std::uint64_t generation)
{
    assertInGuiThread();
    if (generation != this->generation_ || this->stopped_)
    {
        return;
    }
    this->lastActivity_ = QDateTime::currentDateTime();
    qCDebug(LOG) << "Kick: websocket open, waiting for "
                    "pusher:connection_established";
}

void KickChatClient::handleText(std::uint64_t generation,
                                const QByteArray &data)
{
    assertInGuiThread();
    if (generation != this->generation_ || this->stopped_)
    {
        return;
    }
    this->lastActivity_ = QDateTime::currentDateTime();

    QJsonParseError parseError{};
    auto doc = QJsonDocument::fromJson(data, &parseError);
    if (!doc.isObject())
    {
        qCWarning(LOG) << "Kick: ignoring non-JSON frame:"
                       << parseError.errorString();
        return;
    }
    auto root = doc.object();
    auto event = root.value("event").toString();

    if (event == u"pusher:connection_established")
    {
        QJsonObject subscribe{
            {"event", "pusher:subscribe"},
            {"data",
             QJsonObject{{"auth", ""},
                         {"channel", QString(u"chatrooms." % this->chatroomId_ %
                                             u".v2")}}},
        };
        this->sendJson(QJsonDocument(subscribe).toJson(QJsonDocument::Compact));
        this->connected_ = true;
        this->backoff_.reset();
        this->status(u"Kick conectado a " % this->slug_);
        return;
    }
    if (event == u"pusher:ping")
    {
        this->sendJson(R"({"event":"pusher:pong","data":{}})");
        return;
    }
    if (event == u"pusher:pong" ||
        event == u"pusher_internal:subscription_succeeded")
    {
        return;
    }
    if (event == u"pusher:error")
    {
        auto errorData = root.value("data").toObject();
        auto code = errorData.value("code").toInt();
        auto message = errorData.value("message").toString();
        qCWarning(LOG) << "Kick: pusher error" << code << message;
        if (code == 4001 || code == 4003)
        {
            // Rotated/unknown app key: fetch fresh credentials.
            this->generation_++;
            this->handle_ = {};
            this->status(u"Kick: credenciales de Pusher rechazadas, "
                         u"renegociando..."_s);
            this->negotiateCredentials();
        }
        return;
    }
    if (event == u"App\\Events\\ChatMessageEvent")
    {
        // `data` is a JSON document encoded as a string.
        auto payloadDoc =
            QJsonDocument::fromJson(root.value("data").toString().toUtf8());
        auto parsed = parseChatMessageEvent(payloadDoc.object());
        if (!parsed)
        {
            qCWarning(LOG) << "Kick: could not parse ChatMessageEvent";
            return;
        }
        if (this->onMessage_)
        {
            this->onMessage_(std::move(*parsed));
        }
        return;
    }
    // Other App\Events\* (subscriptions, deletions, ...) are ignored.
}

void KickChatClient::handleClose(std::uint64_t generation)
{
    assertInGuiThread();
    if (generation != this->generation_ || this->stopped_)
    {
        return;
    }
    this->connected_ = false;
    this->scheduleReconnect(u"conexion cerrada."_s);
}

}  // namespace chatterino::kick
