-- Migration: 0041_bot_text_fixes
-- Date:    2026-11-17
-- Purpose: Stock lines carried over by 0019 that give the bots away as nonsense or as bots:
--            * suggest_something '%zone_name': nothing but the zone's name, so a bot posted just "Тирисфальские леса" /
--              "Tirisfal Glades" in General (cyborgx5, GitHub #12). The zone's other lines stay.
--            * reply_question_why / reply_question_generic 'dont ask me %s, im just a bot': a bot that says it is a bot.
-- Reverts: yes (INSERT the rows back).

-- @target: shared
DELETE FROM {SHARED}.playerbot_v2_bot_text WHERE name = 'suggest_something' AND text = '%zone_name';
DELETE FROM {SHARED}.playerbot_v2_bot_text WHERE name IN ('reply_question_why', 'reply_question_generic') AND text = 'dont ask me %s, im just a bot';
