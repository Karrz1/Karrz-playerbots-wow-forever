-- Migration: 0042_bot_text_meta_lines
-- Date:    2026-11-17
-- Purpose: Four 'reply' lines (not understood, reply_type 0) carried over by 0019 that talk about the chat itself the way a
--          program would: confused by the chat, everyone says the same things, trying to understand a comment. A bot
--          whispered something it does not understand answered with one of them, and a player reads it as a bot. The
--          other 'not understood' lines (huh?, what?, you lost me, ...) stay.
-- Reverts: yes (INSERT the rows back, reply_type 0).

-- @target: shared
DELETE FROM {SHARED}.playerbot_v2_bot_text WHERE name = 'reply' AND reply_type = 0 AND text IN (
    'These chat-messages are freaking me out! I feel like I know you all!',
    'Why is everybody always saying the same things???',
    'thought i should remain silent, was getting confused from chat again',
    'i will attempt to understand that comment');
