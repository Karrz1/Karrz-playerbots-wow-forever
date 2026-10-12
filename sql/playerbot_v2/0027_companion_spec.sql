-- Migration: 0027_companion_spec
-- Date:    2026-10-11
-- Purpose: `.bot save` stored a companion's role (tank / healer / dps) but not its talent spec, so a call or relog re-derived the spec from the
--          role: a DPS paladin or druid came back as whichever DPS spec the class defaults to, not the one it was saved in. The Classic spec
--          id the bot plays (ClassicSpec: what the talent build and the rotation follow) is now stored with it and pinned again when the
--          bot is set up on a call or login; the talents are re-bought for it as for any spec change.
--
--          Columns:
--            companion_spec   0 = none stored, otherwise the Classic spec id (Playerbot::Classic::EffectiveSpecId)
-- Reverts: yes (DROP COLUMN companion_spec).

ALTER TABLE playerbot_v2_character
    ADD COLUMN companion_spec SMALLINT UNSIGNED NOT NULL DEFAULT 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (27, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
