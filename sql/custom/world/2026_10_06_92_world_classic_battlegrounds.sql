-- Sorts after 2026_10_06_90_world_classic_graveyard_conditions_fix.sql on purpose: that file rewrites every Classic
-- graveyard condition, including the WSG/AB ones, and the AB node graveyards here depend on the node world states.
-- Classic Warsong Gulch (map 489) and Arathi Basin (map 529): classic_battleground_warsong_gulch.cpp and
-- classic_battleground_arathi_basin.cpp drive the 1.12 flags and banners. Their objects call into the map script
-- through GameObject AI, and the world states they set must belong to the classic maps (a world state without
-- map ids is realm-wide). Existing rows (the retail remakes on 2106/2107 use the same ids) get the classic map added.

-- The map scripts bind by name through battleground_scripts (none existed for the classic maps).
DELETE FROM `battleground_scripts` WHERE `MapId` IN (489, 529);
INSERT INTO `battleground_scripts` (`MapId`, `BattlemasterListId`, `ScriptName`) VALUES
(489, 0, 'classic_battleground_warsong_gulch'),
(529, 0, 'classic_battleground_arathi_basin');

UPDATE `gameobject_template` SET `ScriptName` = 'go_classic_wsg_flag' WHERE `entry` IN (179830, 179831, 179785, 179786);
UPDATE `gameobject_template` SET `ScriptName` = 'go_classic_ab_banner' WHERE `entry` IN (180058, 180059, 180060, 180061, 180087, 180088, 180089, 180090, 180091);

-- Warsong Gulch
INSERT IGNORE INTO `world_state` (`ID`, `DefaultValue`, `MapIDs`, `AreaIDs`, `ScriptName`, `Comment`) VALUES
(1545, 0, '489', NULL, '', 'Classic WSG - Alliance flag taken'),
(1546, 0, '489', NULL, '', 'Classic WSG - Horde flag taken'),
(1581, 0, '489', NULL, '', 'Classic WSG - Alliance flag captures'),
(1582, 0, '489', NULL, '', 'Classic WSG - Horde flag captures'),
(1601, 0, '489', NULL, '', 'Classic WSG - Max flag captures'),
(2338, 0, '489', NULL, '', 'Classic WSG - Horde flag carrier state'),
(2339, 0, '489', NULL, '', 'Classic WSG - Alliance flag carrier state');
UPDATE `world_state` SET `MapIDs` = CONCAT(`MapIDs`, ',489') WHERE `ID` IN (1545,1546,1581,1582,1601,2338,2339) AND `MapIDs` IS NOT NULL AND `MapIDs` <> '' AND FIND_IN_SET('489', `MapIDs`) = 0;

-- Arathi Basin
INSERT IGNORE INTO `world_state` (`ID`, `DefaultValue`, `MapIDs`, `AreaIDs`, `ScriptName`, `Comment`) VALUES
(1767, 0, '529', NULL, '', 'Classic AB - Stables Alliance'),
(1768, 0, '529', NULL, '', 'Classic AB - Stables Horde'),
(1769, 0, '529', NULL, '', 'Classic AB - Stables contested by Alliance'),
(1770, 0, '529', NULL, '', 'Classic AB - Stables contested by Horde'),
(1772, 0, '529', NULL, '', 'Classic AB - Farm Alliance'),
(1773, 0, '529', NULL, '', 'Classic AB - Farm Horde'),
(1774, 0, '529', NULL, '', 'Classic AB - Farm contested by Alliance'),
(1775, 0, '529', NULL, '', 'Classic AB - Farm contested by Horde'),
(1776, 0, '529', NULL, '', 'Classic AB - Alliance resources'),
(1777, 0, '529', NULL, '', 'Classic AB - Horde resources'),
(1778, 0, '529', NULL, '', 'Classic AB - Horde occupied bases'),
(1779, 0, '529', NULL, '', 'Classic AB - Alliance occupied bases'),
(1780, 0, '529', NULL, '', 'Classic AB - Max resources'),
(1782, 0, '529', NULL, '', 'Classic AB - Blacksmith Alliance'),
(1783, 0, '529', NULL, '', 'Classic AB - Blacksmith Horde'),
(1784, 0, '529', NULL, '', 'Classic AB - Blacksmith contested by Alliance'),
(1785, 0, '529', NULL, '', 'Classic AB - Blacksmith contested by Horde'),
(1787, 0, '529', NULL, '', 'Classic AB - Gold Mine Alliance'),
(1788, 0, '529', NULL, '', 'Classic AB - Gold Mine Horde'),
(1789, 0, '529', NULL, '', 'Classic AB - Gold Mine contested by Alliance'),
(1790, 0, '529', NULL, '', 'Classic AB - Gold Mine contested by Horde'),
(1792, 0, '529', NULL, '', 'Classic AB - Lumber Mill Alliance'),
(1793, 0, '529', NULL, '', 'Classic AB - Lumber Mill Horde'),
(1794, 0, '529', NULL, '', 'Classic AB - Lumber Mill contested by Alliance'),
(1795, 0, '529', NULL, '', 'Classic AB - Lumber Mill contested by Horde'),
(1842, 0, '529', NULL, '', 'Classic AB - Stables neutral icon'),
(1843, 0, '529', NULL, '', 'Classic AB - Gold Mine neutral icon'),
(1844, 0, '529', NULL, '', 'Classic AB - Lumber Mill neutral icon'),
(1845, 0, '529', NULL, '', 'Classic AB - Farm neutral icon'),
(1846, 0, '529', NULL, '', 'Classic AB - Blacksmith neutral icon'),
(1861, 0, '529', NULL, '', 'Classic AB - Unknown (1.12 sends 2)'),
(1955, 0, '529', NULL, '', 'Classic AB - Resources warning');
UPDATE `world_state` SET `MapIDs` = CONCAT(`MapIDs`, ',529') WHERE `ID` IN (1767,1768,1769,1770,1772,1773,1774,1775,1776,1777,1778,1779,1780,1782,1783,1784,1785,1787,1788,1789,1790,1792,1793,1794,1795,1842,1843,1844,1845,1846,1861,1955) AND `MapIDs` IS NOT NULL AND `MapIDs` <> '' AND FIND_IN_SET('529', `MapIDs`) = 0;

-- Graveyards. graveyard_zone already lists the classic graveyards (WSG 769-772, AB 889-899); without conditions every team
-- could release at every one of them. Bases/entrances go by team; an AB node graveyard only while that team holds the
-- node (the 1.12 node world states the AB script sets). 1.12 had no other rule (VMaNGOS GetClosestGraveYard).
DELETE FROM `conditions` WHERE `SourceTypeOrReferenceId` = 27 AND `SourceGroup` IN (3277, 3358) AND `SourceEntry` IN (769,770,771,772,889,890,893,894,895,896,897,898,899);
INSERT INTO `conditions` (`SourceTypeOrReferenceId`, `SourceGroup`, `SourceEntry`, `SourceId`, `ElseGroup`, `ConditionTypeOrReference`, `ConditionTarget`, `ConditionValue1`, `ConditionValue2`, `ConditionValue3`, `ConditionStringValue1`, `NegativeCondition`, `Comment`) VALUES
(27, 3277, 769, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic WSG Alliance start graveyard - Alliance only'),
(27, 3277, 771, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic WSG Alliance base graveyard - Alliance only'),
(27, 3277, 770, 0, 0, 6, 0, 67, 0, 0, '', 0, 'Classic WSG Horde start graveyard - Horde only'),
(27, 3277, 772, 0, 0, 6, 0, 67, 0, 0, '', 0, 'Classic WSG Horde base graveyard - Horde only'),
(27, 3358, 890, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic AB Alliance entrance graveyard - Alliance only'),
(27, 3358, 898, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic AB Trollbane Hall graveyard - Alliance only'),
(27, 3358, 889, 0, 0, 6, 0, 67, 0, 0, '', 0, 'Classic AB Horde entrance graveyard - Horde only'),
(27, 3358, 899, 0, 0, 6, 0, 67, 0, 0, '', 0, 'Classic AB Defiler\'s Den graveyard - Horde only'),
(27, 3358, 895, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic AB Stables graveyard - Alliance'),
(27, 3358, 895, 0, 0, 11, 0, 1767, 1, 0, '', 0, 'Classic AB Stables graveyard - while the Alliance holds it'),
(27, 3358, 895, 0, 1, 6, 0, 67, 0, 0, '', 0, 'Classic AB Stables graveyard - Horde'),
(27, 3358, 895, 0, 1, 11, 0, 1768, 1, 0, '', 0, 'Classic AB Stables graveyard - while the Horde holds it'),
(27, 3358, 894, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic AB Blacksmith graveyard - Alliance'),
(27, 3358, 894, 0, 0, 11, 0, 1782, 1, 0, '', 0, 'Classic AB Blacksmith graveyard - while the Alliance holds it'),
(27, 3358, 894, 0, 1, 6, 0, 67, 0, 0, '', 0, 'Classic AB Blacksmith graveyard - Horde'),
(27, 3358, 894, 0, 1, 11, 0, 1783, 1, 0, '', 0, 'Classic AB Blacksmith graveyard - while the Horde holds it'),
(27, 3358, 893, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic AB Farm graveyard - Alliance'),
(27, 3358, 893, 0, 0, 11, 0, 1772, 1, 0, '', 0, 'Classic AB Farm graveyard - while the Alliance holds it'),
(27, 3358, 893, 0, 1, 6, 0, 67, 0, 0, '', 0, 'Classic AB Farm graveyard - Horde'),
(27, 3358, 893, 0, 1, 11, 0, 1773, 1, 0, '', 0, 'Classic AB Farm graveyard - while the Horde holds it'),
(27, 3358, 897, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic AB Lumber Mill graveyard - Alliance'),
(27, 3358, 897, 0, 0, 11, 0, 1792, 1, 0, '', 0, 'Classic AB Lumber Mill graveyard - while the Alliance holds it'),
(27, 3358, 897, 0, 1, 6, 0, 67, 0, 0, '', 0, 'Classic AB Lumber Mill graveyard - Horde'),
(27, 3358, 897, 0, 1, 11, 0, 1793, 1, 0, '', 0, 'Classic AB Lumber Mill graveyard - while the Horde holds it'),
(27, 3358, 896, 0, 0, 6, 0, 469, 0, 0, '', 0, 'Classic AB Gold Mine graveyard - Alliance'),
(27, 3358, 896, 0, 0, 11, 0, 1787, 1, 0, '', 0, 'Classic AB Gold Mine graveyard - while the Alliance holds it'),
(27, 3358, 896, 0, 1, 6, 0, 67, 0, 0, '', 0, 'Classic AB Gold Mine graveyard - Horde'),
(27, 3358, 896, 0, 1, 11, 0, 1788, 1, 0, '', 0, 'Classic AB Gold Mine graveyard - while the Horde holds it');
