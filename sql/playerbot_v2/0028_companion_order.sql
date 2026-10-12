-- Migration: 0028_companion_order
-- Date:    2026-10-11
-- Purpose: `.bot save` stored each companion's raid sub-group but not its place inside it. The raid frame lists a sub-group in the order of
--          the group's member list (join order), which differs from call to call because the bots log in asynchronously. The member's
--          position in the group list at save time is stored with it, and the restore after a call puts the owned members back in
--          that order.
--
--          Columns:
--            companion_order   0 = none stored, otherwise the member's index in the group member list at save time + 1
-- Reverts: yes (DROP COLUMN companion_order).

ALTER TABLE playerbot_v2_character
    ADD COLUMN companion_order TINYINT UNSIGNED NOT NULL DEFAULT 0;

INSERT INTO playerbot_v2_schema_version (version, sha256) VALUES
    (28, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
