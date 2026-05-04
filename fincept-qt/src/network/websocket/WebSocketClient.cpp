#include "network/websocket/WebSocketClient.h"

#include "core/logging/Logger.h"

#include <QRandomGenerator>
#include <algorithm>

namespace fincept {

#ifdef HAS_QT_WEBSOCKETS

WebSocketClient::WebSocketClient(QObject* parent) : QObject(parent) {
    connect(&socket_, &QWebSocket::connected, this, &WebSocketClient::on_connected);
    connect(&socket_, &QWebSocket::disconnected, this, &WebSocketClient::on_disconnected);
    connect(&socket_, &QWebSocket::textMessageReceived, this, &WebSocketClient::on_text_received);
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, &WebSocketClient::on_binary_received);
    connect(&socket_, &QWebSocket::errorOccurred, this, &WebSocketClient::on_error);
    connect(&reconnect_timer_, &QTimer::timeout, this, &WebSocketClient::attempt_reconnect);

    // Batching timer — flushes accumulated messages periodically
    batch_timer_.setSingleShot(true);
    connect(&batch_timer_, &QTimer::timeout, this, &WebSocketClient::flush_batch);

    // Ping/pong keepalive — detects stale connections early (30s interval)
    ping_timer_.setInterval(30000);
    connect(&ping_timer_, &QTimer::timeout, this, [this]() {
        if (socket_.state() == QAbstractSocket::ConnectedState) {
            socket_.ping();
        }
    });

    reconnect_timer_.setSingleShot(true);
}

void WebSocketClient::connect_to(const QString& url) {
    url_ = url;
    reconnect_attempts_ = 0;
    LOG_INFO("WS", "Connecting to " + url);
    socket_.open(QUrl(url));
}

void WebSocketClient::disconnect() {
    reconnect_timer_.stop();
    batch_timer_.stop();
    ping_timer_.stop();
    flush_batch(); // Flush any pending messages before disconnecting
    socket_.close();
}

void WebSocketClient::send(const QString& message) {
    socket_.sendTextMessage(message);
}

void WebSocketClient::send_binary(const QByteArray& data) {
    socket_.sendBinaryMessage(data);
}

bool WebSocketClient::is_connected() const {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void WebSocketClient::set_batch_interval(int interval_ms) {
    batch_interval_ms_ = interval_ms;
    if (interval_ms > 0) {
        batch_buffer_.reserve(64); // Pre-allocate for typical message burst
    }
}

void WebSocketClient::on_connected() {
    LOG_INFO("WS", "Connected to " + url_);
    reconnect_attempts_ = 0;
    ping_timer_.start();
    emit connected();
}

void WebSocketClient::on_disconnected() {
    LOG_WARN("WS", "Disconnected from " + url_);
    ping_timer_.stop();
    flush_batch(); // Flush any remaining messages
    emit disconnected();
    if (reconnect_attempts_ < MAX_RECONNECT_ATTEMPTS) {
        // Exponential backoff with jitter: base * 2^n + random jitter
        int base_delay = std::min(1000 * (1 << reconnect_attempts_), 30000);
        int jitter = static_cast<int>(QRandomGenerator::global()->bounded(500)) - 250;
        int delay = std::max(100, base_delay + jitter);
        reconnect_timer_.start(delay);
    }
}

void WebSocketClient::on_text_received(const QString& msg) {
    if (batch_interval_ms_ > 0) {
        // Batch mode: accumulate messages and flush periodically
        batch_buffer_.append(msg);
        if (!batch_timer_.isActive()) {
            batch_timer_.start(batch_interval_ms_);
        }
    } else {
        // Immediate mode: emit signal directly (backward compatible)
        emit message_received(msg);
    }
}

void WebSocketClient::flush_batch() {
    if (batch_buffer_.isEmpty()) return;
    // Emit individual messages for backward compatibility
    for (const auto& msg : batch_buffer_) {
        emit message_received(msg);
    }
    // Also emit batch signal for consumers that want bulk processing
    emit messages_batched(batch_buffer_);
    batch_buffer_.clear();
}

void WebSocketClient::on_binary_received(const QByteArray& data) {
    emit binary_message_received(data);
}

void WebSocketClient::on_error(QAbstractSocket::SocketError err) {
    Q_UNUSED(err);
    LOG_ERROR("WS", "Error: " + socket_.errorString());
    emit error_occurred(socket_.errorString());
}

void WebSocketClient::attempt_reconnect() {
    reconnect_attempts_++;
    LOG_INFO("WS", QString("Reconnect attempt %1/%2").arg(reconnect_attempts_).arg(MAX_RECONNECT_ATTEMPTS));
    socket_.open(QUrl(url_));
}

#else // No Qt WebSockets — stub implementations

WebSocketClient::WebSocketClient(QObject* parent) : QObject(parent) {}
void WebSocketClient::connect_to(const QString& /*url*/) {
    LOG_WARN("WS", "WebSocket not available — Qt6::WebSockets not installed");
}
void WebSocketClient::disconnect() {}
void WebSocketClient::send(const QString& /*message*/) {}
void WebSocketClient::send_binary(const QByteArray& /*data*/) {}
bool WebSocketClient::is_connected() const { return false; }
void WebSocketClient::set_batch_interval(int /*interval_ms*/) {}
void WebSocketClient::flush_batch() {}

#endif

} // namespace fincept
