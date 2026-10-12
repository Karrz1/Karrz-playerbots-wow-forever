-- Migration: 0026_companion_subgroup
-- Date:    2026-10-11
-- Purpose: A raid built on purpose (two tanks and two healers in sub-group 1, the DPS behind them) came back jumbled after a relog or
--          `.bot call all`: every companion joins the first free sub-group in the order it logs in. The sub-group of an owned
--          companion is now stored (`.bot save`, and whenever the leader moves it) and put back when it joins the owner's raid.
--
--          Columns:
--            companion_subgroup   0 = none stored, otherwise the raid sub-group number 1-8 (Group sub-group index + 1)
-- Reverts: yes (DROP COLUMN companion_subgroup).

ALTER TABLE playerbot_v2_character
    ADD COLUMN companion_subgroup TINYINT UNSIGNED NOT NULL DEFAULT 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (26, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
