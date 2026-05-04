#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>

#ifdef HAS_QT_WEBSOCKETS
#    include <QWebSocket>
#endif

#include <functional>

namespace fincept {

/// WebSocket client with auto-reconnect for real-time market data.
/// Includes message batching to reduce signal overhead and a ping/pong keepalive.
class WebSocketClient : public QObject {
    Q_OBJECT
  public:
    explicit WebSocketClient(QObject* parent = nullptr);

    void connect_to(const QString& url);
    void disconnect();
    void send(const QString& message);
    void send_binary(const QByteArray& data);
    bool is_connected() const;

    /// Enable message batching: messages received within `interval_ms` are
    /// coalesced into a single signal emission. Reduces UI update overhead
    /// for high-frequency data feeds (order books, tick data).
    void set_batch_interval(int interval_ms);

  signals:
    void connected();
    void disconnected();
    void message_received(const QString& message);
    void binary_message_received(const QByteArray& data);
    /// Emitted when multiple messages are batched together.
    void messages_batched(const QVector<QString>& messages);
    void error_occurred(const QString& error);

  private slots:
#ifdef HAS_QT_WEBSOCKETS
    void on_connected();
    void on_disconnected();
    void on_text_received(const QString& msg);
    void on_binary_received(const QByteArray& data);
    void on_error(QAbstractSocket::SocketError err);
    void attempt_reconnect();
    void flush_batch();
#endif

  private:
#ifdef HAS_QT_WEBSOCKETS
    QWebSocket socket_;
#endif
    QTimer reconnect_timer_;
    QTimer batch_timer_;
    QTimer ping_timer_;
    QString url_;
    int reconnect_attempts_ = 0;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 10;

    // Message batching state
    QVector<QString> batch_buffer_;
    int batch_interval_ms_ = 0; // 0 = no batching (immediate emit)
};

} // namespace fincept
