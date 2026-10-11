DROP TABLE IF EXISTS `classic_creature_spell_value`;
-- Classic 1.60: vanilla (1.12) damage, heal and absorb dice of the legacy spells (VMaNGOS spell_template, latest row of build <= 5875).
-- The 70291 client data differs from vanilla for part of them (Defias Evoker Flamestrike 11829: 479 where vanilla is 75 + 1d13; player Shadow Bolt rank 10 25307: about 257 where vanilla is 481 + 1d57).
-- Used for every caster of the legacy spell (players, bots, pets, creatures): SpellMgr::LoadClassicSpellValues, SpellInfo.cpp CalcBaseValue / CalcValue.
-- Effects covered: SCHOOL_DAMAGE, HEALTH_LEECH, HEAL, and the auras PERIODIC_DAMAGE, PERIODIC_HEAL, PERIODIC_LEECH, SCHOOL_ABSORB. Safe to run again.
CREATE TABLE IF NOT EXISTS `classic_spell_value` (
  `spell_id` int unsigned NOT NULL,
  `effect_index` tinyint unsigned NOT NULL,
  `effect` smallint unsigned NOT NULL COMMENT 'SpellEffects, must equal the client effect at that index',
  `apply_aura` smallint unsigned NOT NULL DEFAULT 0 COMMENT 'AuraType of an aura effect, must equal the client one',
  `base_points` int NOT NULL COMMENT 'vanilla base points; the die comes on top',
  `die_sides` int NOT NULL DEFAULT 0 COMMENT 'vanilla die sides: 0 none, n adds 1..n',
  `points_per_level` float NOT NULL DEFAULT 0 COMMENT 'vanilla real points per level',
  `spell_name` varchar(100) NOT NULL DEFAULT '',
  PRIMARY KEY (`spell_id`,`effect_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='Classic 1.60: vanilla 1.12 spell values for every caster';
-- The rows (vanilla spell values) are not published here for now; the table is created empty.
