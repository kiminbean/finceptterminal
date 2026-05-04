#include "storage/cache/CacheManager.h"

#include "storage/sqlite/CacheDatabase.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QSqlQuery>
#include <utility>

namespace fincept {

namespace {
// Escape LIKE wildcards (% and _) in user-supplied prefixes. Paired with "ESCAPE '\\'" in SQL.
QString escape_like(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (QChar ch : s) {
        if (ch == '\\' || ch == '%' || ch == '_')
            out.append('\\');
        out.append(ch);
    }
    return out;
}
} // namespace

CacheManager& CacheManager::instance() {
    static CacheManager s;
    return s;
}

CacheManager::CacheManager(QObject* parent) : QObject(parent) {}

bool CacheManager::mem_lookup(const QString& key, QString& out) const {
    QMutexLocker lock(&mem_mutex_);
    auto* entry = mem_cache_.object(key);
    if (!entry)
        return false;
    if (entry->expires_at_ms > 0 && QDateTime::currentMSecsSinceEpoch() >= entry->expires_at_ms) {
        mem_cache_.remove(key);
        return false;
    }
    out = entry->value;
    return true;
}

quint64 CacheManager::mem_generation() const {
    QMutexLocker lock(&mem_mutex_);
    return invalidation_gen_;
}

void CacheManager::mem_store_if_unchanged(const QString& key, const QString& value, int ttl_seconds,
                                          quint64 gen_seen) const {
    QMutexLocker lock(&mem_mutex_);
    // If any remove*/clear* fired between gen_seen capture and now, a deleted
    // key may have just been read from SQL — refuse to repopulate to avoid
    // resurrecting it for the duration of its TTL.
    if (invalidation_gen_ != gen_seen)
        return;
    auto* e = new MemoryEntry{value, ttl_seconds > 0
                                         ? QDateTime::currentMSecsSinceEpoch() + qint64(ttl_seconds) * 1000
                                         : 0};
    mem_cache_.insert(key, e);
}

void CacheManager::mem_remove(const QString& key) {
    QMutexLocker lock(&mem_mutex_);
    ++invalidation_gen_;
    mem_cache_.remove(key);
}

void CacheManager::mem_clear() {
    QMutexLocker lock(&mem_mutex_);
    ++invalidation_gen_;
    mem_cache_.clear();
}

void CacheManager::mem_remove_prefix(const QString& prefix) {
    QMutexLocker lock(&mem_mutex_);
    ++invalidation_gen_;
    const auto keys = mem_cache_.keys();
    for (const QString& k : keys) {
        if (k.startsWith(prefix))
            mem_cache_.remove(k);
    }
}

void CacheManager::put(const QString& key, const QVariant& value, int ttl_seconds, const QString& category) {
    if (key.isEmpty())
        return;
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return;

    const QString data = value.toString();
    const int size_bytes = data.toUtf8().size();
    // ON CONFLICT DO UPDATE preserves created_at and hit_count across re-puts of the same key.
    // (INSERT OR REPLACE would delete+insert, resetting those fields.)
    cdb.execute("INSERT INTO unified_cache "
                "(key, value, category, ttl_seconds, expires_at, size_bytes) "
                "VALUES (?, ?, ?, ?, datetime('now', '+' || ? || ' seconds'), ?) "
                "ON CONFLICT(key) DO UPDATE SET "
                "  value=excluded.value, "
                "  category=excluded.category, "
                "  ttl_seconds=excluded.ttl_seconds, "
                "  expires_at=excluded.expires_at, "
                "  size_bytes=excluded.size_bytes",
                {key, data, category, ttl_seconds, ttl_seconds, size_bytes});

    // Refresh the in-memory tier so subsequent reads are served without a SQL
    // roundtrip. Using the gen-checked variant means a concurrent remove*
    // of any key will cause this store to be skipped — the next get() will
    // simply repopulate from SQL. That's strictly safer than caching the
    // put's value over a possibly-deleted row.
    mem_store_if_unchanged(key, data, ttl_seconds, mem_generation());
}

QVariant CacheManager::get(const QString& key) const {
    if (key.isEmpty())
        return {};

    // Hot path: serve from memory if present and unexpired.
    QString cached;
    if (mem_lookup(key, cached))
        return cached;

    // Capture generation before the SQL fetch — if a concurrent remove()
    // bumps the counter while we read, we'll refuse to repopulate the
    // memory tier with what may be a just-deleted entry.
    const quint64 gen_seen = mem_generation();

    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return {};

    auto r = cdb.execute(
        "SELECT value, CAST((julianday(expires_at) - julianday('now')) * 86400 AS INTEGER) "
        "FROM unified_cache WHERE key = ? AND expires_at > datetime('now')",
        {key});
    if (r.is_err())
        return {};

    QSqlQuery q = std::move(r.value());
    if (!q.next())
        return {};

    const QString value = q.value(0).toString();
    const int remaining_seconds = q.value(1).toInt();
    if (remaining_seconds > 0)
        mem_store_if_unchanged(key, value, remaining_seconds, gen_seen);
    return value;
}

std::optional<QString> CacheManager::try_get(const QString& key) const {
    const QVariant v = get(key);
    if (v.isNull())
        return std::nullopt;
    return v.toString();
}

bool CacheManager::has(const QString& key) const {
    if (key.isEmpty())
        return false;
    QString cached;
    if (mem_lookup(key, cached))
        return true;

    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return false;

    auto r = cdb.execute("SELECT 1 FROM unified_cache WHERE key = ? AND expires_at > datetime('now')", {key});
    if (r.is_err())
        return false;

    QSqlQuery q = std::move(r.value());
    return q.next();
}

void CacheManager::remove(const QString& key) {
    if (key.isEmpty())
        return;
    mem_remove(key);
    auto& cdb = CacheDatabase::instance();
    if (cdb.is_open())
        cdb.execute("DELETE FROM unified_cache WHERE key = ?", {key});
}

void CacheManager::remove_prefix(const QString& prefix) {
    // Empty prefix would match everything — refuse to accidentally DELETE FROM unified_cache.
    if (prefix.isEmpty())
        return;
    mem_remove_prefix(prefix);
    auto& cdb = CacheDatabase::instance();
    if (cdb.is_open())
        cdb.execute("DELETE FROM unified_cache WHERE key LIKE ? ESCAPE '\\'", {escape_like(prefix) + "%"});
}

void CacheManager::clear() {
    mem_clear();
    auto& cdb = CacheDatabase::instance();
    if (cdb.is_open())
        cdb.exec("DELETE FROM unified_cache");
}

void CacheManager::clear_category(const QString& category) {
    if (category.isEmpty())
        return;
    // We don't track category in the memory tier — clear it conservatively to
    // avoid serving stale entries from a deleted category.
    mem_clear();
    auto& cdb = CacheDatabase::instance();
    if (cdb.is_open())
        cdb.execute("DELETE FROM unified_cache WHERE category = ?", {category});
}

int CacheManager::entry_count() const {
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return 0;

    auto r = cdb.execute("SELECT COUNT(*) FROM unified_cache WHERE expires_at > datetime('now')", {});
    if (r.is_err())
        return 0;

    QSqlQuery q = std::move(r.value());
    return q.next() ? q.value(0).toInt() : 0;
}

} // namespace fincept
