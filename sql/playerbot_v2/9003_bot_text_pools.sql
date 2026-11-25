-- Migration: 9003_bot_text_pools
-- Date:    2026-11-25
-- Purpose: Move the chat lines that were hard-coded in the source into the shared text table,
--          so a realm can rewrite bot speech without rebuilding. Every line below is the text the
--          code already said, verbatim ({} fmt args became %placeholders); the code keeps its
--          builtin pool as fallback for a server whose shared table has no rows for a key.
--
--          New keys (playerbot_v2_bot_text.name):
--            guild_ding            a bot levels up; %my_level is the new level
--            guild_login_greet     greet the guild at login
--            guild_smalltalk       idle guild smalltalk (only with a human guildmate online)
--            guild_tavern_hangout  tavern-night pitch; %venue is the faction tavern name
--            guild_rival           a rival guild's members are near
--            guild_quest_brag      a quest was turned in
--            guild_lfm             long LFG queue pulse
--            guild_event_announce  officer callout before a guild event; %event_label, %mins_until
--            guild_tavern_chat     chatter during the tavern party
--            guild_staging_*       chatter during a raid / dungeon / bg night staging (generic = fallback)
--            guild_babble          the slow ambient babble (30 min per bot)
--            guild_join_welcome    a real player joined the guild; %s is the joiner's first name
--            banter_<kind>         a companion's party-chat banter (ready / wipe / thanks / ouch /
--                                  bosskill); banter_<kind>_<tank|healer|caster|polite|rude|rp> are
--                                  the per-role / per-personality extras, merged in with the base key
--            banter_raid_wipe      one corpse in a wiped raid says so in raid chat
--            say_kindness_heal     a free bot healed a passing player: the /say with it
--            say_kindness_rez      a free bot resurrects a passing player: the /say with it
--
--          INSERT IGNORE against the (name, reply_type, text(200)) unique key: re-running is a no-op.
--          No text contains a ';' (the migration runner splits statements on it).
-- Reverts: yes (DELETE the rows of the keys above).
--
-- @target: shared

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_ding', 'Ding %my_level!', 0, 0),
    ('guild_ding', '%my_level achieved', 0, 0),
    ('guild_ding', 'hit %my_level :)', 0, 0),
    ('guild_ding', 'Level %my_level!', 0, 0),
    ('guild_ding', 'ding %my_level', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_login_greet', 'hi all', 0, 0),
    ('guild_login_greet', 'hey :)', 0, 0),
    ('guild_login_greet', 'o/', 0, 0),
    ('guild_login_greet', 'hello!', 0, 0),
    ('guild_login_greet', 'evening', 0, 0),
    ('guild_login_greet', 'yo', 0, 0),
    ('guild_login_greet', 'hi guild', 0, 0),
    ('guild_login_greet', 'hey hey', 0, 0),
    ('guild_login_greet', 'o/ everyone', 0, 0),
    ('guild_login_greet', 'back online', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_smalltalk', 'anyone need help with anything?', 0, 0),
    ('guild_smalltalk', 'what''s good tonight folks', 0, 0),
    ('guild_smalltalk', 'soloing this is rough', 0, 0),
    ('guild_smalltalk', 'anyone running dungeons?', 0, 0),
    ('guild_smalltalk', 'pulled too much again, classic', 0, 0),
    ('guild_smalltalk', 'where is everyone leveling?', 0, 0),
    ('guild_smalltalk', 'the new patch feels good', 0, 0),
    ('guild_smalltalk', 'anyone got mats to spare?', 0, 0),
    ('guild_smalltalk', 'drinking tea, brb', 0, 0),
    ('guild_smalltalk', 'good run earlier, ty', 0, 0),
    ('guild_smalltalk', 'checking AH brb', 0, 0),
    ('guild_smalltalk', 'this zone is gorgeous', 0, 0),
    ('guild_smalltalk', 'lf groupmates for tonight', 0, 0),
    ('guild_smalltalk', 'back from a break, hi all', 0, 0),
    ('guild_smalltalk', 'rolling around questing', 0, 0),
    ('guild_smalltalk', 'anyone seen [redacted-name] online?', 0, 0),
    ('guild_smalltalk', 'love this server', 0, 0),
    ('guild_smalltalk', 'phase is acting weird, anyone else?', 0, 0),
    ('guild_smalltalk', 'great patch notes today', 0, 0),
    ('guild_smalltalk', 'stretch break', 0, 0),
    ('guild_smalltalk', 'tea then back to grinding', 0, 0),
    ('guild_smalltalk', 'anyone need a port?', 0, 0),
    ('guild_smalltalk', 'this mount drop is brutal', 0, 0),
    ('guild_smalltalk', 'looking for a leveling buddy', 0, 0),
    ('guild_smalltalk', 'kicking back, easy night', 0, 0),
    ('guild_smalltalk', 'anyone want to chain quests?', 0, 0),
    ('guild_smalltalk', 'this rep grind...', 0, 0),
    ('guild_smalltalk', 'soloing dungeons for chests', 0, 0),
    ('guild_smalltalk', 'wow my repair bill', 0, 0),
    ('guild_smalltalk', 'good music tonight, share recs', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_tavern_hangout', 'anyone want to hit the tavern? %venue', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_rival', 'rival guild spotted', 0, 0),
    ('guild_rival', 'show them how it''s done', 0, 0),
    ('guild_rival', 'they think they''re tough', 0, 0),
    ('guild_rival', 'second place again', 0, 0),
    ('guild_rival', 'watch the rivals here', 0, 0),
    ('guild_rival', 'let''s outperform them', 0, 0),
    ('guild_rival', 'rival in sight, on guard', 0, 0),
    ('guild_rival', 'amateurs', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_quest_brag', 'another one down', 0, 0),
    ('guild_quest_brag', 'just finished a quest', 0, 0),
    ('guild_quest_brag', 'feeling productive tonight', 0, 0),
    ('guild_quest_brag', 'quest log getting lighter', 0, 0),
    ('guild_quest_brag', 'another tick on the list', 0, 0),
    ('guild_quest_brag', 'ding XP', 0, 0),
    ('guild_quest_brag', 'easy gold', 0, 0),
    ('guild_quest_brag', 'quest hub clear', 0, 0),
    ('guild_quest_brag', 'love a good quest chain', 0, 0),
    ('guild_quest_brag', 'rolling through quests', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_lfm', 'LFM for a dungeon, anyone?', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_event_announce', '%event_label starts in %mins_until min!', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_tavern_chat', 'this is fun', 0, 0),
    ('guild_tavern_chat', 'cheers everyone', 0, 0),
    ('guild_tavern_chat', '/dance', 0, 0),
    ('guild_tavern_chat', 'best night of the week', 0, 0),
    ('guild_tavern_chat', 'love these party nights', 0, 0),
    ('guild_tavern_chat', 'more bots should hit the tavern', 0, 0),
    ('guild_tavern_chat', 'great turnout', 0, 0),
    ('guild_tavern_chat', 'tavern night incoming', 0, 0),
    ('guild_tavern_chat', 'rolling tipsy', 0, 0),
    ('guild_tavern_chat', '/toast', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_staging_generic', 'anyone joining?', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_staging_raid', 'raid night, who''s in?', 0, 0),
    ('guild_staging_raid', 'rolling for raid spots', 0, 0),
    ('guild_staging_raid', '/g raid in 10', 0, 0),
    ('guild_staging_raid', 'looking for one more dps', 0, 0),
    ('guild_staging_raid', 'loot reservations open', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_staging_dungeon', 'dungeon night, who''s down?', 0, 0),
    ('guild_staging_dungeon', 'lf2m for keys', 0, 0),
    ('guild_staging_dungeon', 'let''s chain some 5-mans', 0, 0),
    ('guild_staging_dungeon', 'got a healer?', 0, 0),
    ('guild_staging_dungeon', 'rolling tank dual spec', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_staging_bg', 'BG night! /join wsg', 0, 0),
    ('guild_staging_bg', 'premade rolling', 0, 0),
    ('guild_staging_bg', 'honor cap incoming', 0, 0),
    ('guild_staging_bg', 'anyone seen the queue times?', 0, 0),
    ('guild_staging_bg', 'rated arena later maybe', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_babble', 'Hey everyone!', 0, 0),
    ('guild_babble', 'anyone need a hand with anything?', 0, 0),
    ('guild_babble', 'Got some good loot today.', 0, 0),
    ('guild_babble', 'Anyone running dungeons later?', 0, 0),
    ('guild_babble', 'Crafting reagents up in the bank if anyone needs them.', 0, 0),
    ('guild_babble', 'Who''s online tonight?', 0, 0),
    ('guild_babble', 'lfg if anyone wants to group', 0, 0),
    ('guild_babble', 'How''s everyone doing?', 0, 0),
    ('guild_babble', 'Just dinged again — slow grind.', 0, 0),
    ('guild_babble', 'Hope to see folks at the next raid night.', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('guild_join_welcome', 'welcome %s!', 0, 0),
    ('guild_join_welcome', 'welcome to the guild %s :)', 0, 0),
    ('guild_join_welcome', 'hey %s, welcome!', 0, 0),
    ('guild_join_welcome', 'welcome aboard %s!', 0, 0),
    ('guild_join_welcome', '%s welcome! :)', 0, 0),
    ('guild_join_welcome', 'glad to have you %s!', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('say_kindness_heal', 'there you go', 0, 0),
    ('say_kindness_heal', 'patched you up', 0, 0),
    ('say_kindness_heal', 'heal inc', 0, 0),
    ('say_kindness_heal', 'careful out there', 0, 0),
    ('say_kindness_heal', 'you looked like you needed that', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('say_kindness_rez', 'rez inc', 0, 0),
    ('say_kindness_rez', 'hold on, rezzing you', 0, 0),
    ('say_kindness_rez', 'got you', 0, 0),
    ('say_kindness_rez', 'rez coming', 0, 0),
    ('say_kindness_rez', 'don''t release, rezzing', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_raid_wipe', 'well, that went badly', 0, 0),
    ('banter_raid_wipe', 'wipe :(', 0, 0),
    ('banter_raid_wipe', 'ouch', 0, 0),
    ('banter_raid_wipe', 'ok that one is on me', 0, 0),
    ('banter_raid_wipe', 'and we''re all dead', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ready', 'ready', 0, 0),
    ('banter_ready', 'rdy', 0, 0),
    ('banter_ready', 'yep ready', 0, 0),
    ('banter_ready', 'good to go', 0, 0),
    ('banter_ready', 'all set', 0, 0),
    ('banter_ready', 'ready :)', 0, 0),
    ('banter_ready', 'yup', 0, 0),
    ('banter_ready', 'ready when you are', 0, 0);

-- 'ready' was listed twice in the builtin pool (twice as likely); the unique key keeps one row,
-- so the repeat becomes weight 2 (the same trick migration 0022 used for the ike3 repeats).
UPDATE {SHARED}.playerbot_v2_bot_text SET weight = 2
    WHERE name = 'banter_ready' AND reply_type = 0 AND text = 'ready';

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ready_tank', 'ready to tank', 0, 0),
    ('banter_ready_tank', 'tank ready', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ready_healer', 'healer ready', 0, 0),
    ('banter_ready_healer', 'heals ready', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ready_caster', 'ready, mana is good', 0, 0),
    ('banter_ready_caster', 'mana is good', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ready_rp', 'aye, ready', 0, 0),
    ('banter_ready_rp', 'ready, lead on', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_wipe', 'my bad', 0, 0),
    ('banter_wipe', 'wp', 0, 0),
    ('banter_wipe', 'wp wp', 0, 0),
    ('banter_wipe', 'ouch', 0, 0),
    ('banter_wipe', 'rip', 0, 0),
    ('banter_wipe', 'well that went well', 0, 0),
    ('banter_wipe', 'oof', 0, 0),
    ('banter_wipe', 'that was rough', 0, 0),
    ('banter_wipe', 'rip us', 0, 0),
    ('banter_wipe', 'wow ok', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_wipe_tank', 'my bad, bad pull', 0, 0),
    ('banter_wipe_tank', 'sorry, lost them', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_wipe_healer', 'sorry, too slow with the heals', 0, 0),
    ('banter_wipe_healer', 'my bad, couldn''t keep up', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_wipe_polite', 'sorry everyone', 0, 0),
    ('banter_wipe_polite', 'sorry guys, my fault', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_wipe_rude', 'great job everyone', 0, 0),
    ('banter_wipe_rude', 'wow', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_thanks', 'ty', 0, 0),
    ('banter_thanks', 'thanks', 0, 0),
    ('banter_thanks', 'thx', 0, 0),
    ('banter_thanks', 'ty!', 0, 0),
    ('banter_thanks', 'tyvm', 0, 0),
    ('banter_thanks', 'ty rez', 0, 0),
    ('banter_thanks', 'thx rez', 0, 0),
    ('banter_thanks', 'ty :)', 0, 0),
    ('banter_thanks', 'thanks for the rez', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_thanks_polite', 'thank you!', 0, 0),
    ('banter_thanks_polite', 'much appreciated', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_thanks_rude', 'finally', 0, 0),
    ('banter_thanks_rude', 'about time', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ouch', 'ouch', 0, 0),
    ('banter_ouch', 'ow', 0, 0),
    ('banter_ouch', 'oof', 0, 0),
    ('banter_ouch', 'that hurt', 0, 0),
    ('banter_ouch', 'ugh', 0, 0),
    ('banter_ouch', 'argh', 0, 0),
    ('banter_ouch', 'rip', 0, 0),
    ('banter_ouch', 'owie', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ouch_tank', 'that stings', 0, 0),
    ('banter_ouch_tank', 'ow, my armor', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_ouch_caster', 'squishy, i know', 0, 0),
    ('banter_ouch_caster', 'paper armor strikes again', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_bosskill', 'gg', 0, 0),
    ('banter_bosskill', 'gg!', 0, 0),
    ('banter_bosskill', 'gg wp', 0, 0),
    ('banter_bosskill', 'nice', 0, 0),
    ('banter_bosskill', 'well done', 0, 0),
    ('banter_bosskill', 'nice one', 0, 0),
    ('banter_bosskill', 'woo', 0, 0),
    ('banter_bosskill', 'finally', 0, 0),
    ('banter_bosskill', 'good one', 0, 0),
    ('banter_bosskill', 'that went well', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_bosskill_polite', 'gg everyone', 0, 0),
    ('banter_bosskill_polite', 'well played all', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_bosskill_tank', 'easy', 0, 0),
    ('banter_bosskill_tank', 'gg, nice heals', 0, 0);

INSERT IGNORE INTO {SHARED}.playerbot_v2_bot_text (name, text, say_type, reply_type) VALUES
    ('banter_bosskill_rp', 'victory!', 0, 0),
    ('banter_bosskill_rp', 'for glory, gg', 0, 0);

INSERT INTO {SHARED}.playerbot_v2_schema_version (version, sha256) VALUES
    (9003, REPEAT('0', 64))
ON DUPLICATE KEY UPDATE applied_at = CURRENT_TIMESTAMP;
