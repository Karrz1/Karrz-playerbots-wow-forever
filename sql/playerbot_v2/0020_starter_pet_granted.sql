-- Migration: 0020_starter_pet_granted
-- Date:    2026-10-05
-- Purpose: A hunter companion's starter pet (the pet its Taming the Beast quest would have given it) is granted once
--          per bot, ever. Without the flag the companion upkeep tamed a new pet every pass once the owner abandoned
--          the first, and the bot's pet slot blocked the owner's own Tame Beast.
--
--          Columns:
--            starter_pet_granted   1 = the bot has had its starter pet (granted, or it already had a pet of its own).
-- Reverts: yes (DROP COLUMN starter_pet_granted).

ALTER TABLE playerbot_v2_character
    ADD COLUMN starter_pet_granted TINYINT UNSIGNED NOT NULL DEFAULT 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (20, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
