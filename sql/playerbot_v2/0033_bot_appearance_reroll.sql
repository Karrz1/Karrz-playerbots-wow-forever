-- Migration: 0033_bot_appearance_reroll
-- Date:    2026-10-26
-- Purpose: Bots took every appearance choice from the client-made characters of their race and gender, so a realm with one
--          client-made human woman gave every female human bot her face, hair and colours (same for any race/gender with a
--          single player character). Bots now roll from every choice the client offers. The appearances of all characters on
--          bot accounts are cleared here; at the same start, before any bot logs in, BotCharacterFactory::RepairAppearances
--          rolls a new one for each.
-- Reverts: no schema change (old appearances are not kept).

DELETE cc FROM character_customizations cc
    JOIN characters c ON c.guid = cc.guid
    JOIN playerbot_v2_account a ON a.account_id = c.account;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (33, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
