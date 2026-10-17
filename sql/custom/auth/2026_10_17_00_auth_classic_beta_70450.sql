-- WoW Classic beta client (wow_classic_beta, WowB.exe) 1.60.1.70450
DELETE FROM `build_info` WHERE `build`=70450;
INSERT INTO `build_info` (`build`,`majorVersion`,`minorVersion`,`bugfixVersion`,`hotfixVersion`) VALUES
(70450,1,60,1,NULL);

-- No auth key is known for Win-x64-WoWB 70450 yet; see Network.SkipBuildAuthKeyCheck in worldserver.conf
-- The 70425 update field layouts apply to every realm build from 70425 on (ClassicOpcodes::IsBuild70425OrLater).

UPDATE `realmlist` SET `gamebuild`=70450 WHERE `id` IN (70,71,72,73);
