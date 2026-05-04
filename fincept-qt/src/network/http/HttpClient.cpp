#include "network/http/HttpClient.h"

#include "core/logging/Logger.h"

#include <QMutexLocker>
#include <QUrl>

namespace fincept {

HttpClient& HttpClient::instance() {
    static HttpClient s;
    return s;
}

HttpClient::HttpClient() {
    nam_ = new QNetworkAccessManager(this);
    // Enable connection pooling — QNetworkAccessManager already reuses connections
    // but we set higher defaults for concurrent requests.
    nam_->setTransferTimeout(30000); // 30s default timeout
}

void HttpClient::set_cache_ttl(int seconds) {
    cache_ttl_seconds_ = seconds;
}

bool HttpClient::check_cache(const QString& key, Result<QJsonDocument>& out) const {
    if (cache_ttl_seconds_ <= 0) return false;
    QMutexLocker lock(&const_cast<QMutex&>(cache_mutex_));
    auto* entry = cache_.object(key);
    if (!entry) return false;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - entry->timestamp) / 1000 < cache_ttl_seconds_) {
        out = Result<QJsonDocument>::ok(entry->data);
        return true;
    }
    cache_.remove(key);
    return false;
}

void HttpClient::store_cache(const QString& key, const QJsonDocument& data) {
    if (cache_ttl_seconds_ <= 0) return;
    QMutexLocker lock(&cache_mutex_);
    auto* entry = new CacheEntry{data, QDateTime::currentMSecsSinceEpoch()};
    cache_.insert(key, entry);
}

QNetworkRequest HttpClient::build_request(const QString& url) const {
    const bool is_relative = !url.startsWith("http");
    const QString full_url = is_relative ? (base_url_ + url) : url;
    QUrl qurl(full_url);
    QNetworkRequest req{qurl};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "FinceptTerminal/4.0");
    // Request compressed responses to reduce bandwidth
    req.setRawHeader("Accept-Encoding", "gzip, deflate");

    // Only attach Fincept auth headers when the request targets the configured
    // Fincept API host. Notification providers (Slack, Discord, Telegram,
    // PagerDuty, user webhooks, …) and other services pass absolute third-party
    // URLs through this same singleton — without this guard their servers would
    // receive the user's X-API-Key and X-Session-Token in every call.
    const bool same_host = is_relative || [&]() {
        const QUrl base(base_url_);
        return !base.host().isEmpty() && qurl.host().compare(base.host(), Qt::CaseInsensitive) == 0;
    }();

    if (same_host) {
        if (!api_key_.isEmpty()) {
            req.setRawHeader("X-API-Key", api_key_.toUtf8());
        }
        if (!session_token_.isEmpty()) {
            req.setRawHeader("X-Session-Token", session_token_.toUtf8());
        }
    }
    return req;
}

void HttpClient::handle_reply(QNetworkReply* reply, JsonCallback callback) {
    connect(reply, &QNetworkReply::finished, this, [reply, cb = std::move(callback)]() {
        reply->deleteLater();

        QByteArray data = reply->readAll();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        QJsonParseError parse_err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parse_err);

        if (reply->error() != QNetworkReply::NoError) {
            // Strip query string to avoid logging embedded tokens/keys
            QUrl sanitized = reply->url();
            sanitized.setQuery(QString{});
            LOG_WARN("HTTP",
                     QString("HTTP %1: %2 — %3").arg(status).arg(sanitized.toString()).arg(reply->errorString()));

            // 401/403 must always be returned as err() so session expiry detection
            // in AuthApi/SessionGuard works correctly — even when the body is valid JSON.
            if (status == 401 || status == 403) {
                cb(Result<QJsonDocument>::err(QString("HTTP_%1").arg(status).toStdString()));
                return;
            }

            // For other HTTP errors with a parseable JSON body, pass it through
            // so callers (AuthApi) can extract server-side error messages.
            if (!data.isEmpty() && parse_err.error == QJsonParseError::NoError && doc.isObject()) {
                cb(Result<QJsonDocument>::ok(doc));
                return;
            }

            // No parseable body — return sentinel so callers map status to fallback message
            cb(Result<QJsonDocument>::err(QString("HTTP_%1").arg(status).toStdString()));
            return;
        }

        if (parse_err.error != QJsonParseError::NoError) {
            cb(Result<QJsonDocument>::err(parse_err.errorString().toStdString()));
            return;
        }
        cb(Result<QJsonDocument>::ok(doc));
    });
}

void HttpClient::get(const QString& url, JsonCallback callback) {
    LOG_DEBUG("HTTP", "GET " + url);
    // Check cache first (GET is idempotent — safe to cache)
    Result<QJsonDocument> cached_result(QJsonDocument());
    if (check_cache(url, cached_result)) {
        LOG_DEBUG("HTTP", "Cache hit for " + url);
        callback(std::move(cached_result));
        return;
    }
    auto* reply = nam_->get(build_request(url));
    // Store in cache on success
    auto cache_cb = [this, url](Result<QJsonDocument> result) {
        if (result.is_ok()) {
            store_cache(url, result.value());
        }
    };
    handle_reply(reply, [cache_cb, cb = std::move(callback)](Result<QJsonDocument> result) mutable {
        cache_cb(result);
        cb(std::move(result));
    });
}

void HttpClient::post(const QString& url, const QJsonObject& body, JsonCallback callback) {
    LOG_DEBUG("HTTP", "POST " + url);
    QJsonDocument doc(body);
    auto* reply = nam_->post(build_request(url), doc.toJson());
    handle_reply(reply, std::move(callback));
}

void HttpClient::put(const QString& url, const QJsonObject& body, JsonCallback callback) {
    LOG_DEBUG("HTTP", "PUT " + url);
    QJsonDocument doc(body);
    auto* reply = nam_->put(build_request(url), doc.toJson());
    handle_reply(reply, std::move(callback));
}

void HttpClient::del(const QString& url, JsonCallback callback) {
    LOG_DEBUG("HTTP", "DELETE " + url);
    auto* reply = nam_->deleteResource(build_request(url));
    handle_reply(reply, std::move(callback));
}

void HttpClient::set_auth_header(const QString& api_key) {
    api_key_ = api_key;
}

void HttpClient::set_session_token(const QString& token) {
    session_token_ = token;
}

void HttpClient::clear_session_token() {
    session_token_.clear();
}

void HttpClient::set_base_url(const QString& base) {
    base_url_ = base;
}

} // namespace fincept
