#pragma once
#include "core/result/Result.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QCache>
#include <QDateTime>
#include <QMutex>

#include <functional>
#include <QMap>
#include <QVector>

namespace fincept {

/// Async HTTP client wrapping QNetworkAccessManager.
/// All responses delivered via callbacks on the Qt event loop.
class HttpClient : public QObject {
    Q_OBJECT
  public:
    static HttpClient& instance();

    using JsonCallback = std::function<void(Result<QJsonDocument>)>;

    void get(const QString& url, JsonCallback callback);
    void post(const QString& url, const QJsonObject& body, JsonCallback callback);
    void put(const QString& url, const QJsonObject& body, JsonCallback callback);
    void del(const QString& url, JsonCallback callback);

    void set_auth_header(const QString& api_key);
    void set_session_token(const QString& token);
    void clear_session_token();
    void set_base_url(const QString& base);

    /// Set cache TTL in seconds (default 0 = no caching).
    void set_cache_ttl(int seconds);

  private:
    HttpClient();
    QNetworkRequest build_request(const QString& url) const;
    void handle_reply(QNetworkReply* reply, JsonCallback callback);

    QNetworkAccessManager* nam_ = nullptr;
    QString base_url_;
    QString api_key_;
    QString session_token_;

    // Request cache with TTL
    struct CacheEntry {
        QJsonDocument data;
        qint64 timestamp; // ms since epoch
    };
    QCache<QString, CacheEntry> cache_{256}; // max 256 entries
    int cache_ttl_seconds_ = 0; // 0 = disabled
    mutable QMutex cache_mutex_;
    bool check_cache(const QString& key, Result<QJsonDocument>& out) const;
    void store_cache(const QString& key, const QJsonDocument& data);

    // Request deduplication — coalesces duplicate in-flight GET requests
    // so only one network call is made; all callers share the result.
    QMap<QString, QVector<JsonCallback>> in_flight_gets_;
    void handle_deduped_get(const QString& url, QNetworkReply* reply, JsonCallback first_callback);
};

} // namespace fincept
