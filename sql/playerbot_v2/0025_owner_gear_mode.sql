-- Migration: 0025_owner_gear_mode
-- Date:    2026-10-10
-- Purpose: A player chooses how the gear of the companions they own is picked: `match` (default; the best gear for the bot's
--          level, at the cap up to the owner's own gear level), `best`, or the class tier set `t1` / `t2` / `t3` (level cap only).
--          The choice is per owner account and kept across relog and restart.
--
--          Columns:
--            owner_account_id   the owning account (one row per owner)
--            gear_mode          0 = match, 1 = best, 2 = t1, 3 = t2, 4 = t3 (Gear::Owned::GearMode)
-- Reverts: yes (DROP TABLE playerbot_v2_owner_gear).

CREATE TABLE IF NOT EXISTS playerbot_v2_owner_gear (
    owner_account_id INT UNSIGNED     NOT NULL,
    gear_mode        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (owner_account_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (25, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
