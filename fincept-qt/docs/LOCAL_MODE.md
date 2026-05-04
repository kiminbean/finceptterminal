# Local-Only Mode (V5.0)

A compile-time switch that turns FinceptTerminal into a fully self-hosted,
single-user terminal. No traffic to `api.fincept.in`, no signup/login screen,
no credit accounting, ENTERPRISE plan unlocked unconditionally.

> **Scope:** This mode is intended for *your own personal use* on your own
> machines. The synthetic ENTERPRISE session bypasses all upstream auth and
> billing — do not redistribute builds compiled with `FINCEPT_LOCAL_MODE=ON`.

## Enable

```bash
cmake -B build/macos-release \
      -DFINCEPT_LOCAL_MODE=ON \
      -DDEPLOY_QT=ON \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/macos-release --target FinceptTerminal -j
```

CMake will print `FINCEPT_LOCAL_MODE: ON — synthetic ENTERPRISE session, no
api.fincept.in calls` during configure. The flag propagates to C++ as the
`FINCEPT_LOCAL_MODE` preprocessor define and only the gated code paths change;
without the flag the build is bit-for-bit equivalent to upstream.

## What changes when ON

| Component | Behavior |
| --- | --- |
| `AuthManager::initialize()` | Skips `validate_saved_session()`. Synthesizes ENTERPRISE session with `credit_balance = 1e9`, emits `auth_state_changed`, `subscription_fetched`, `login_succeeded` immediately. |
| Login / Register / Pricing screens | Never shown — `WindowFrame` lands directly on the dashboard because `AuthManager::is_authenticated()` returns true on first paint. |
| `v002_llm_chat` migration | Replaces the seeded `fincept` LLM provider row with a `zai` (Z.AI Coding Plan, GLM-4.6) row. The `api_key` column is left blank for you to fill in. |
| `LlmService` | Adds `zai` to the list of supported OpenAI-compatible providers. Routes `/chat/completions` to `https://api.z.ai/api/coding/paas/v4` by default. |
| Toolbar / Navigation | Shows `enterprise` plan badge with very-large credit balance. |

`FINCEPT_LOCAL_MODE=OFF` (default) keeps all original behavior.

## Pointing at your own Fincept server

`AppConfig::api_base_url()` reads `api/base_url` from `QSettings` and falls back
to `https://api.fincept.in`. To redirect everything (chat history, news WS,
maritime, geopolitics, macro calendar, forum, quantlib, …) to your self-hosted
backend, set the value once before launching the app:

```bash
defaults write com.fincept.terminal "api/base_url" "https://your-fincept-host.example.com"
```

(or via the in-app Settings → API panel if the build includes that section.)

The synthetic auth still suppresses the user-facing login pipeline; remote
calls that the rest of the app makes will go to the host you configured.

## Z.AI Coding Plan setup

1. Sign in to <https://z.ai> and obtain a Coding Plan API key.
2. In Fincept Terminal, open **Settings → LLM**.
3. The provider `zai` will already be present (seeded by the migration).
4. Paste your key into the `api_key` field and save.
5. Default model is `glm-5.1`; `glm-4.6`, `glm-4.5`, and `glm-4.5-air` are also valid.

The endpoint resolves to `https://api.z.ai/api/coding/paas/v4/chat/completions`.
The request format is OpenAI-compatible (Bearer auth, `messages` array,
`max_tokens`, `stream`), so all existing chat features (streaming, tool calls
where supported, history) work without further changes.

If you replace the seeded `base_url` with a custom one, `LlmService::get_url()`
will append `/chat/completions` (note: not `/v1/chat/completions` like other
providers — Z.AI's path already contains the `v4` segment).

## Reverting to upstream behavior

```bash
rm -rf build/macos-release    # clean slate
cmake -B build/macos-release -DFINCEPT_LOCAL_MODE=OFF -DDEPLOY_QT=ON
cmake --build build/macos-release --target FinceptTerminal -j
```

The compile-time switch ensures the synthetic session code is excluded from
the binary entirely — there is no runtime toggle.

## Diagnostics in LOCAL_MODE

| Check | Command |
| --- | --- |
| Is the build flagged? | `strings build/macos-release/FinceptTerminal.app/Contents/MacOS/FinceptTerminal \| grep -i "FINCEPT_LOCAL_MODE active"` |
| Did synthesis fire? | `grep "FINCEPT_LOCAL_MODE active" ~/Library/Application\ Support/com.fincept.terminal/logs/fincept.log` |
| Any traffic to upstream? | `tcpdump -i any host api.fincept.in` while the app is running |

## See also

- `fincept-qt/docs/MACOS_PACKAGING.md` — bundle/sign/deploy requirements that
  also apply to LOCAL_MODE builds.
- `src/auth/AuthManager.cpp` — `bootstrap_local_enterprise()` definition.
- `src/storage/sqlite/migrations/v002_llm_chat.cpp` — Z.AI seed.
- `src/ai_chat/LlmService.cpp` — `zai` provider URL resolution.
