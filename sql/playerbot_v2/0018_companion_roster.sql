-- Migration: 0018_companion_roster
-- Date:    2026-10-05
-- Purpose: The owner's saved companion roster (`.bot save` / `.bot unsave`): `.bot call all` and the ForeverBots
--          "Call all" button bring back only the saved companions, not every bot the owner ever made.
--
--          Columns:
--            companion_saved   1 = on its owner's roster. Only meaningful for owned bots (owner_account_id != 0).
-- Reverts: yes (DROP COLUMN companion_saved).

ALTER TABLE playerbot_v2_character
    ADD COLUMN companion_saved TINYINT UNSIGNED NOT NULL DEFAULT 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (18, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
