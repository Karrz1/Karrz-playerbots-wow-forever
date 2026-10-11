-- Classic Life Tap (all six ranks) did nothing: the client spell data has a dummy effect no core code handled.
-- 1454 was bound to the retail trigger script; replace it with the Classic implementation.
DELETE FROM `spell_script_names` WHERE `spell_id` IN (1454, 1455, 1456, 11687, 11688, 11689);
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(1454, 'spell_warl_classic_life_tap'),
(1455, 'spell_warl_classic_life_tap'),
(1456, 'spell_warl_classic_life_tap'),
(11687, 'spell_warl_classic_life_tap'),
(11688, 'spell_warl_classic_life_tap'),
(11689, 'spell_warl_classic_life_tap');
