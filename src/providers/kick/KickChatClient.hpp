// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: read-only Kick chat client (Pusher protocol over WebSocket).
//
// Flow (all public methods and callbacks run on the GUI thread):
//   1. Resolve the chatroom id from the channel slug
//      (GET https://kick.com/api/v2/channels/{slug} -> chatroom.id), unless
//      a manual id was configured.
//   2. Connect to wss://ws-{cluster}.pusher.com/app/{key}, subscribe to
//      chatrooms.{id}.v2, answer pusher:ping with pusher:pong.
//   3. On pusher:error 4001/4003 (rotated app key), negotiate fresh
//      credentials from https://web.kick.com/api/v1/realtime/connection,
//      cache them in settings and reconnect.
//   4. On close/error, reconnect with exponential backoff (1s .. 32s).
#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "providers/unified/ExternalChatMessage.hpp"
#include "util/ExponentialBackoff.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>

namespace chatterino::kick {

class KickChatClient : public std::enable_shared_from_this<KickChatClient>
{
public:
    using MessageCallback =
        std::function<void(unified::ExternalChatMessage message)>;
    /// Human readable status updates ("Kick conectado a xqc").
    using StatusCallback = std::function<void(const QString &text)>;

    KickChatClient(QString slug, QString manualChatroomId,
                   MessageCallback onMessage, StatusCallback onStatus);
    ~KickChatClient();

    KickChatClient(const KickChatClient &) = delete;
    KickChatClient(KickChatClient &&) = delete;
    KickChatClient &operator=(const KickChatClient &) = delete;
    KickChatClient &operator=(KickChatClient &&) = delete;

    /// Must be called on a shared_ptr-owned instance, on the GUI thread.
    void start();
    /// Stops reconnecting and closes the socket. GUI thread.
    void stop();

    [[nodiscard]] const QString &slug() const
    {
        return this->slug_;
    }

    // --- Internal: called by the websocket listener on the GUI thread.
    void handleOpen(std::uint64_t generation);
    void handleText(std::uint64_t generation, const QByteArray &data);
    void handleClose(std::uint64_t generation);

private:
    void resolveChatroomId();
    void connectSocket();
    void scheduleReconnect(const QString &reason);
    void negotiateCredentials();
    void sendJson(const QByteArray &payload);
    void status(const QString &text);

    QString slug_;
    QString manualChatroomId_;
    MessageCallback onMessage_;
    StatusCallback onStatus_;

    QString chatroomId_;
    WebSocketPool pool_{"kick"};
    WebSocketHandle handle_;
    std::uint64_t generation_{0};
    bool stopped_{false};
    bool connected_{false};
    bool reconnectPending_{false};
    ExponentialBackoff<6> backoff_{std::chrono::milliseconds(1000)};
    QDateTime lastActivity_;
    QDateTime lastNegotiation_;
    QTimer keepAlive_;
};

}  // namespace chatterino::kick
