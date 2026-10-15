# Playerbot V2 — Configuration

**Status**: Pass B
**Last updated**: 2026-10-09 (the 58 keys no code reads were removed; the live reference for defaults is `src/modules/PlayerbotV2/conf/playerbot.conf.dist`)
**Purpose**: Every config key, what it does, default value, hot-reload eligibility. The full schema for `playerbot.conf` (loaded by `Util/ConfigReader`).

## 1. File location & format

`playerbot.conf` lives next to `worldserver.conf`. Same INI-like format TrinityCore uses elsewhere:

```ini
KeyName = value
```

`playerbot.conf.dist` ships the documented defaults; admins copy to `playerbot.conf` and override.

## 2. Hot-reload semantics

Each key is tagged:
- **HR-yes**: change applies on next config reload (`.playerbot reload`) without restart.
- **HR-no**: requires worldserver restart (because it changes thread counts, table layouts, or invariants).

## 3. Key reference

### 3.1 Core / threading

| Key | Default | HR | Description |
|---|---|---|---|
| `Playerbot.AiWorkerThreads` | `0` | HR-no | 0 = auto (`min(8, hardware_concurrency)`). Set explicit count to force. |
| `Playerbot.FleetThreadEnabled` | `true` | HR-no | Per Pass A §14.5 default. False = collapse onto world thread. |
| `Playerbot.TickBudgetMs` | `10` | HR-yes | World-tick time budget for V2 (per `REQUIREMENTS.md` §2.1). Bots skip ticks beyond this. |

### 3.2 Population

| Key | Default | HR | Description |
|---|---|---|---|
| `Playerbot.Population.TotalTarget` | `0` | HR-yes | Target concurrent bot count. |
| `Playerbot.Population.Floor` | `100` | HR-yes | Always at least this many bots online. |
| `Playerbot.Population.Ceiling` | `5000` | HR-yes | Hard upper limit. |
| `Playerbot.Population.AutoScale` | `true` | HR-yes | Whether to dynamically scale to real-player count. |
| `Playerbot.Population.HordePct` | `50` | HR-yes | Faction balance bias (Horde percentage). |

### 3.4 Player interaction

| Key | Default | HR | Description |
|---|---|---|---|
| `Playerbot.Interact.AutoAcceptInvites` | `friends_or_higher` | HR-yes | `none`, `friends_or_higher`, `guild_or_higher`, `anyone`. |
| `Playerbot.Interact.AutoLeaderHandover` | `true` | HR-yes | Bot relinquishes leadership on player join. Per `SYSTEM_FEATURES.md` §3.2. |
| `Playerbot.Interact.CommandPrefix` | `!` | HR-yes | Whisper command prefix. |

### 3.6 Housing / neighborhoods

| Key | Default | HR | Description |
|---|---|---|---|
| `Playerbot.Housing.Enabled` | `true` | HR-yes | Master switch for bot housing participation. |
| `Playerbot.Housing.NeighborhoodTargetOccupancy` | `60` | HR-yes | Per-neighborhood bot residency target percent of capacity. |
| `Playerbot.Housing.PlotPurchaseEligibilityCheck` | `strict` | HR-no | `strict` = bot must meet level/quest/currency exactly as player; `lenient` (DEV ONLY) = relaxed for testing. **Production must be `strict`** per `REQUIREMENTS.md` §1.1 #7. |

### 3.10 Diagnostics

| Key | Default | HR | Description |
|---|---|---|---|
| `Playerbot.Diag.HealthEndpointPort` | `0` | HR-no | TCP port for monitoring. 0 = disabled. |

### 3.12 Logging

| Key | Default | HR | Description |
|---|---|---|---|
| `Playerbot.Log.Level` | `info` | HR-yes | `error`, `warn`, `info`, `debug`, `trace`. |

## 4. `playerbot.conf.dist` shipping content

Pass B Implementation note: when the `playerbot.conf.dist` file ships, every key above appears with:
- A heading comment (purpose, allowed values)
- The default value (commented if it represents the in-code default; uncommented otherwise)
- A `# HR-yes` or `# HR-no` tag

Sample entry:
```ini
###################
# Population sizing
###################

# Target concurrent bot count. The system tries to maintain this many bots
# online, scaling between Floor and Ceiling as real-player count varies
# (when AutoScale is true).
# HR-yes
Playerbot.Population.TotalTarget = 0

# Always have at least this many bots online, even when real player count is high.
# HR-yes
Playerbot.Population.Floor = 100
```

## 5. Validation

`Util/ConfigReader::load(path)`:
- Loads file, parses keys.
- Validates types and ranges per a hard-coded schema in `ConfigReader.cpp`.
- Reports errors line-numbered to the log; refuses to start with malformed config.
- For HR-yes keys, supports `.playerbot reload`. Updates atomic config snapshot. bot workers see new values on next tick.
- For HR-no keys, reload logs a warning and ignores.

## 6. What's locked vs open

**Locked**: every key name and HR-tag above. Defaults are starting points; admins override per server.

**Open**:
- The `Population.LevelCustom` and `Population.ClassCustom` formats — likely JSON in v1.1 if simple comma-lists prove too rigid.
- Whether population curves should support multiple realm-specific overrides via DB rows (`playerbot_v2_population_target`) in addition to file config — leaning yes; deferred to V1.1.
- Localization of `playerbot.conf.dist` comments — out of scope.
