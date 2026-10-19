-- Migration: 0032_world_gear_repair
-- Date:    2026-10-18
-- Purpose: The first starter-outfit swap (0031) stripped some world bots under level 10 and could not put the outfit on (the pieces went through the
--          bags, which were full), clearing their mark. Every world bot under level 10 that carries neither a main-hand weapon nor a ranged weapon is
--          marked again; its next login puts the starter outfit on. A bot that wields something is left alone.
-- Reverts: no schema change (the mark is only cleared again by the swap).

UPDATE playerbot_v2_character pv JOIN characters c ON c.guid = pv.character_guid_low
    SET pv.world_gear_pending = 1
    WHERE c.level < 10 AND pv.owner_account_id = 0 AND pv.owner_player_guid = 0 AND pv.companion_saved = 0 AND pv.distribution_level > 0
      AND pv.world_gear_pending = 0
      AND NOT EXISTS (SELECT 1 FROM character_inventory ci WHERE ci.guid = pv.character_guid_low AND ci.bag = 0 AND ci.slot IN (15, 17));

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (32, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
