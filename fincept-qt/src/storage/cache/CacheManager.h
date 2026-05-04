#pragma once
#include <QCache>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariant>

#include <optional>

namespace fincept {

/// SQLite-backed cache (CacheDatabase / cache.db) with a small in-memory LRU
/// hot tier. Hot reads bypass SQLite entirely; misses fall through to disk and
/// repopulate the memory tier. All writes go through SQLite (the source of
/// truth) and then refresh memory.
class CacheManager : public QObject {
    Q_OBJECT
  public:
    static CacheManager& instance();

    void put(const QString& key, const QVariant& value, int ttl_seconds = 300, const QString& category = "general");
    /// Returns the cached value (as QString-convertible QVariant) or a null QVariant on miss/expiry.
    QVariant get(const QString& key) const;
    /// Single-query variant of get(): std::nullopt on miss, value on hit. Prefer this over has()+get()
    /// — those two-round-trips duplicate work since get() already checks expiry.
    std::optional<QString> try_get(const QString& key) const;
    bool has(const QString& key) const;
    void remove(const QString& key);
    void remove_prefix(const QString& prefix);
    void clear();
    void clear_category(const QString& category);

    int entry_count() const;

  private:
    explicit CacheManager(QObject* parent = nullptr);

    // In-memory hot tier — fronts the SQLite cache so frequent reads of the
    // same key (hot tickers, dashboard refreshes) skip the SQL query entirely.
    // Cap of 512 entries keeps memory bounded; the SQLite tier remains
    // authoritative for capacity and persistence.
    struct MemoryEntry {
        QString value;
        qint64 expires_at_ms; // ms since epoch; 0 = no expiry tracked here
    };
    mutable QCache<QString, MemoryEntry> mem_cache_{512};
    mutable QMutex mem_mutex_;

    bool mem_lookup(const QString& key, QString& out) const;
    void mem_store(const QString& key, const QString& value, int ttl_seconds) const;
    void mem_remove(const QString& key);
    void mem_clear();
    void mem_remove_prefix(const QString& prefix);
};

} // namespace fincept
