-- Migration: 0022_bot_text_weight
-- Date:    2026-10-05
-- Purpose: Keep the ike3 texts' weighting. ike3 made a line more likely by listing it more than once
--          (ai_playerbot_texts has six such repeats: one 'broadcast_looting_item_normal' line, four
--          'broadcast_looting_item_rare' lines, one 'suggest_instance' line). 0019 inserts through a
--          unique key on (name, reply_type, text) so that re-running it is a no-op, and that key
--          silently dropped every repeat, flattening the odds.
--
--          The repeat count becomes data instead of a duplicate row:
--            playerbot_v2_bot_text.weight   relative odds of the line among the rows of its key
--                                           (and, for 'reply', among the rows of its reply_type).
--                                           Default 1. Bot/BotTextMgr.cpp picks weighted.
--          The six rows ike3 listed twice get weight 2. New content that wants a line said more often
--          raises its weight; the unique key stays, so text inserts remain idempotent.
-- Reverts: yes (DROP COLUMN weight).

-- @target: shared

ALTER TABLE {SHARED}.playerbot_v2_bot_text
    ADD COLUMN weight SMALLINT UNSIGNED NOT NULL DEFAULT 1 AFTER reply_type;

UPDATE {SHARED}.playerbot_v2_bot_text SET weight = 2
    WHERE name = 'broadcast_looting_item_normal' AND reply_type = 0 AND text = 'wonder what %item_link tastes like';

UPDATE {SHARED}.playerbot_v2_bot_text SET weight = 2
    WHERE name = 'broadcast_looting_item_rare' AND reply_type = 0 AND text = '%item_link is hunter bis';

UPDATE {SHARED}.playerbot_v2_bot_text SET weight = 2
    WHERE name = 'broadcast_looting_item_rare' AND reply_type = 0 AND text = '%item_link is %my_class bis';

UPDATE {SHARED}.playerbot_v2_bot_text SET weight = 2
    WHERE name = 'broadcast_looting_item_rare' AND reply_type = 0 AND text = 'RNG is not bad today %item_link';

UPDATE {SHARED}.playerbot_v2_bot_text SET weight = 2
    WHERE name = 'broadcast_looting_item_rare' AND reply_type = 0 AND text = 'sweet %item_link, freshly looted';

UPDATE {SHARED}.playerbot_v2_bot_text SET weight = 2
    WHERE name = 'suggest_instance' AND reply_type = 0 AND text = 'What about %instance_name?';

INSERT INTO {SHARED}.playerbot_v2_schema_version (version, sha256) VALUES
    (22, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
