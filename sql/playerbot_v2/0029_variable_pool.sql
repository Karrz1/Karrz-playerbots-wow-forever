-- Migration: 0029_variable_pool
-- Date:    2026-10-16
-- Purpose: the world spawner keeps a core roster (Playerbot.World.PoolMax) and a variable pool (Playerbot.World.VariableMax) of
--          temporary residents created around a real player where the core has nobody fitting (a new character on Zephras Isle,
--          a new level 1-9 player). A temporary resident is flagged here and is deleted again after Playerbot.World.VariableTtlHours
--          offline with no owner, group or queue tag. Core rows, companions and every other row keep 0 and are never deleted by it.
--
--          Columns:
--            variable_pool   0 = core / not temporary, 1 = temporary resident
-- Reverts: yes (DROP COLUMN variable_pool).

ALTER TABLE playerbot_v2_character
    ADD COLUMN variable_pool TINYINT UNSIGNED NOT NULL DEFAULT 0,
    ADD KEY idx_variable_pool (variable_pool);

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (29, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
