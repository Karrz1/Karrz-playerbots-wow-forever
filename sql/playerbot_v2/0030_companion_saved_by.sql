-- Migration: 0030_companion_saved_by
-- Date:    2026-10-17
-- Purpose: The saved companion roster is per character, not per account: a level 22 rogue's Raid ready called the level 60
--          warlock's 39 bots. companion_saved_by is the character whose roster the bot is on. 0 = any character (kept for a
--          roster saved by an owner with no bound character).
-- Reverts: yes (DROP COLUMN companion_saved_by).

ALTER TABLE playerbot_v2_character
    ADD COLUMN companion_saved_by BIGINT UNSIGNED NOT NULL DEFAULT 0;

UPDATE playerbot_v2_character SET companion_saved_by = owner_player_guid WHERE companion_saved = 1;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (30, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
