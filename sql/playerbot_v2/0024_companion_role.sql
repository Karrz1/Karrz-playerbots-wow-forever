-- Migration: 0024_companion_role
-- Date:    2026-10-07
-- Purpose: A companion's declared role (`.bot add <class> tank|healer|dps`, `/setrole`) lived only in memory (BotAI
--          role override plus the Classic virtual spec), so a relog, `.bot call` or restart dropped it and the role
--          was re-inferred from the bought talents; hybrids (druids, paladins) came back as tanks. The role is now
--          stored with the bot and re-applied at login and when it is called.
--
--          Columns:
--            companion_role   0 = none declared, 1 = tank, 2 = healer, 3 = dps. Only meaningful for owned bots.
-- Reverts: yes (DROP COLUMN companion_role).

ALTER TABLE playerbot_v2_character
    ADD COLUMN companion_role TINYINT UNSIGNED NOT NULL DEFAULT 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (24, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
