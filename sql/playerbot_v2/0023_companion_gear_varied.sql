-- Migration: 0023_companion_gear_varied
-- Date:    2026-10-06
-- Purpose: Companions geared before per-bot gear variety (BotGearGenerator picks among equally good items by the
--          bot's guid) all wear the single top-scoring item per slot, and the re-gear pass never replaces gear that
--          is already good enough. Each companion gets one full re-roll of its equipment with the varied generator
--          (BotPopulationManager::RerollCompanionGear) and is then marked here so it never happens again.
--          `.bot regear` re-rolls on demand whatever the flag says.
--
--          Columns:
--            gear_varied   1 = the companion's equipment has had its one re-roll with the varied generator.
--                          Every existing row starts at 0, which is how the companions made before variety are found.
-- Reverts: yes (DROP COLUMN gear_varied).

ALTER TABLE playerbot_v2_character
    ADD COLUMN gear_varied TINYINT UNSIGNED NOT NULL DEFAULT 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (23, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
