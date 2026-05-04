#pragma once
#include "core/result/Result.h"

#include <QString>

namespace fincept {

/// Secure credential storage using OS-native backends:
///   Windows : Windows Credential Manager (DPAPI-encrypted, per-user DPAPI key).
///   macOS   : Security.framework Keychain via the SecItem API (per-user
///             Keychain, OS-protected).
///   Linux   : libsecret / Secret Service (GNOME Keyring, KWallet via the
///             freedesktop compatibility layer) when libsecret-1 is detected
///             at configure time (FINCEPT_HAVE_LIBSECRET). On distros without
///             libsecret-1 installed the build falls back to XOR-obfuscated
///             QSettings — the loader emits a one-shot WARN at startup.
///             The XOR fallback is NOT cryptographically secure; it only
///             stops casual `grep`-through-config inspection. Production
///             Linux builds MUST install libsecret-1-dev (Debian/Ubuntu),
///             libsecret-devel (Fedora), or libsecret (Arch) and reconfigure.
/// The PIN-manager PBKDF2 path still protects the PIN itself on every
/// platform; this layer holds API keys, session tokens, and PIN salt/hash.
class SecureStorage {
  public:
    static SecureStorage& instance();

    Result<void> store(const QString& key, const QString& value);
    Result<QString> retrieve(const QString& key);
    Result<void> remove(const QString& key);

  private:
    SecureStorage() = default;
};

} // namespace fincept
