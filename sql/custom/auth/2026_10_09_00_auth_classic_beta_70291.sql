-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70291
DELETE FROM `build_info` WHERE `build`=70291;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70291,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70291 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf

-- Forever (this fork): upstream moves realmlist.gamebuild to 70291 here; ours does it in 2026_10_09_01_auth_forever_move_to_70291.sql, so this file no longer touches the realm.
