-- Migration: 0031_world_gear_pending
-- Date:    2026-10-18
-- Purpose: World bots made before build 109 wear a generated full set at level 1-9 (a level 1 wore rings, trinkets, neck and cloak). New world bots
--          under level 10 keep the outfit their character was created with. Each existing world bot under level 10 is marked here; the world
--          spawner strips it to its starter outfit once, on its next login, and clears the mark.
--
--          Columns:
--            world_gear_pending   1 = this world bot still has to be set back to its starter outfit (a companion or a bot of level 10+ is never marked)
-- Reverts: yes (DROP COLUMN world_gear_pending).

ALTER TABLE playerbot_v2_character
    ADD COLUMN world_gear_pending TINYINT UNSIGNED NOT NULL DEFAULT 0;

UPDATE playerbot_v2_character pv JOIN characters c ON c.guid = pv.character_guid_low
    SET pv.world_gear_pending = 1
    WHERE c.level < 10 AND pv.owner_account_id = 0 AND pv.owner_player_guid = 0 AND pv.companion_saved = 0 AND pv.distribution_level > 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (31, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
