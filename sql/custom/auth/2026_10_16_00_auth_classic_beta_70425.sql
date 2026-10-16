-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70425
DELETE FROM `build_info` WHERE `build`=70425;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70425,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70425 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf

UPDATE `realmlist` SET `gamebuild`=70425 WHERE `id` IN (70,71,72,73);
