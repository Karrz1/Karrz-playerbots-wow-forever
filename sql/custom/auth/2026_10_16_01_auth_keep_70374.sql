-- Keeps realms 70-73 on the WoW Classic beta client 1.60.1.70374 until the client moves to 70425.
-- 2026_10_16_00_auth_classic_beta_70425.sql sets the realm build to 70425, and a realm only accepts a client whose build equals
-- realmlist.gamebuild, so a 70374 client would be refused. Sorts after it and puts the build back. Safe to run again.
-- The 70425 update field layouts are used only when the realm build is 70425 or later, so they stay off while this file holds 70374.
-- Remove this file, and add an update that sets gamebuild to 70425, when the client is updated.
UPDATE `realmlist` SET `gamebuild`=70374 WHERE `id` IN (70,71,72,73);
