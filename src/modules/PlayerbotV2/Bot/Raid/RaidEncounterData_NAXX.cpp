#include "RaidEncounters.h"

// Naxxramas (map 533), 40-player Classic version. New for Forever: Karrz's ike3 bots had no Naxxramas data.
// Sources: Forever's Classic ports of the VMaNGOS scripts, src/server/scripts/Custom/Classic/instances/naxxramas
// (abbreviated naxx/<file>.cpp below). Dispel kinds are only a fallback: the brain reads the real dispel type from
// the spell data. Fights that need a mechanic the bots cannot do are listed at the end.

namespace Playerbot::Raid {

    void RegisterNaxxramas(RaidRules& r)
    {
        r.name = "Naxxramas";

        // Kick-first casts (VMaNGOS creature_spells): Mad Scientist Mana Burn 28301, Living Monstrosity Chain Lightning 28293,
        // Acolyte / Necropolis Acolyte Shadow Bolt Volley 28448, Unholy Staff Polymorph 29848, Kel'Thuzad's Frostbolt 28478
        // (classic_boss_kelthuzad.cpp:84).
        r.interruptFirst.push_back(28301);
        r.interruptFirst.push_back(28293);
        r.interruptFirst.push_back(28448);
        r.interruptFirst.push_back(29848);
        r.interruptFirst.push_back(28478);

        // Encounter order (progression advice): Arachnid, Plague, Military, Construct quarter, then Sapphiron and Kel'Thuzad.
        r.bosses = { 15956, 15953, 15952,          // Anub'Rekhan, Faerlina, Maexxna
                     15954, 15936, 16011,          // Noth, Heigan, Loatheb
                     16061, 16060, 16062,          // Razuvious, Gothik, Four Horsemen (Mograine)
                     16028, 15931, 15932, 15928,   // Patchwerk, Grobbulus, Gluth, Thaddius
                     15989, 15990 };               // Sapphiron, Kel'Thuzad

        // ---------------------------------------------------------------------------------------------------------------
        // ANUB'REKHAN (15956): Locust Swarm 28785 is a self buff that pulses damage around him (naxx/classic_boss_anubrekhan.cpp);
        // the tank kites him, everyone else keeps away until it ends (Classic: 30 y, 20 s after a 3 s cast, Anub slowed 40%, a pacifying
        // 875-1125 per 2 s DoT that stacks without cap; warcraft.wiki.gg Anub'Rekhan (Classic)).
        // Everybody leaves, tanks too: the circle's way out for a tank goes sideways to the raid and back toward his pull spot (Escape, the
        // Geddon idiom), so the boss walks after his tank at 60% speed, which is the kite. naxx_anubrekhan 1114a under Who::NonTank: all four
        // tanks stood in it (the new Crypt Guard rose at his feet) and died 19 s in with 6.4-7.5k ticks; Anub, loose at 13%, walked into
        // the raid and wiped it. 38 y, not 30: bots that had reached 32.0-32.7 y were still ticked there (Mchuntd 5260 at 32.0 y,
        // Mcshadb 3090 at 32.1 y), so the hit reaches past 30 by the two hit boxes; the margin also covers the boss stepping after them.
        r.avoidEnemyAura.push_back({ 15956, 28785, 38.0f, Who::Anyone });
        // His tank holds him on the far side from the raid, so the kite's way out (straight away from him, then toward his pull spot)
        // leads him away from the raid. naxx_anubrekhan 1114b: his tank stood between him and the raid, left the circle with the raid to
        // the west, he followed it at 34% into the raid, and 40 bots ended against the west wall with "no way out" (44 lines) and 31
        // Locust Swarm deaths.
        r.faceAway.push_back({ 15956 });
        r.priority.push_back({ 16573, 3 });     // Crypt Guard: Acid Spit / Web / Cleave (classic_boss_anubrekhan.cpp:65)
        r.priority.push_back({ 16698, 2 });     // Corpse Scarab: spawned from corpses (classic_instance_naxxramas.cpp:1237)

        // ---------------------------------------------------------------------------------------------------------------
        // GRAND WIDOW FAERLINA (15953): Poison Bolt Volley 28796, Rain of Fire 28794 (naxx/classic_boss_faerlina.cpp).
        r.dispel.push_back({ 28796, DispelKind::Poison });
        r.avoidGroundEffect.push_back({ 15953, 28794, 3.0f, Who::Anyone });
        r.priority.push_back({ 16505, 3 });     // Naxxramas Follower (classic_boss_faerlina.cpp:53): silence, charge
        // The Follower's Silence 30225 reaches 10 yd around it (SAI 16505, vanilla data radius index 13): healers keep 13.
        r.avoidEnemy.push_back({ 16505, 13.0f, Who::Healer });
        // Worshippers are the enrage counter: after each Enrage 28798 a priest mind-controls one and sacrifices it with Widow's
        // Embrace 28732 on her (RaidMechanics_Naxx.cpp). They have no bot logic; never a DPS target.
        r.priority.push_back({ 16506, -1 });
        // Each tank holds its own pile, apart (Classic: her tank keeps her, the off tanks keep the Worshippers and Followers away from her
        // and each other). naxx_faerlina 1114e: all four tanks stood within 5 y of each other at her feet (3350-3355,-3639), so each
        // Challenging Shout took the whole pack of seven, the tank under it died in 3-4 s (Mctankwa at 4 s, Mctankwc at 10 s), the next
        // tank shouted and died the same way, and from 38 s on no tank was left. An arc round the raid's camp, 22 y north of it toward her
        // dais (she spawns at 3353,-3620, the raid lands at 3354,-3660; the floor there is z 259.1), one slot per tank 16 y apart, wider
        // than a Challenging Shout's 10 y, so no shout reaches the next tank's pile. The ranged and the healers keep to the camp.
        constexpr float kFaerCampX = 3354.0f, kFaerCampY = -3664.0f, kFaerHealY = -3672.0f, kFaerFloorZ = 259.1f;
        r.tankSpot.push_back({ 15953, 15953, 22.0f, 16.0f, true, kFaerCampX, kFaerCampY, 90.0f, { 16506u, 16505u } });
        r.stand.push_back({ 15953, Who::Ranged, kFaerCampX, kFaerCampY, kFaerFloorZ, 6.0f, false, 0 });
        r.stand.push_back({ 15953, Who::Healer, kFaerCampX, kFaerHealY, kFaerFloorZ, 6.0f, false, 0 });

        // ---------------------------------------------------------------------------------------------------------------
        // MAEXXNA (15952): Web Wrap cocoons (16486) hold players until killed; Necrotic Poison 28776 on the tank (-90% healing,
        // "Dispelllable" per the script comment: a tank that cannot be healed is dispelled before anything else); Poison Shock 28741
        // hits everything in front of her within 30 yd (vanilla data, radius index 10); spiderlings 17055 (naxx/classic_boss_maexxna.cpp).
        r.priority.push_back({ 16486, 7 });     // Web Wrap: free the wrapped player
        r.priority.push_back({ 17055, 4 });     // Maexxna Spiderling
        r.dispel.push_back({ 28776, DispelKind::Poison, true });
        r.avoidFrontal.push_back({ 15952, 30.0f, 60.0f, Who::NonTank });
        // Web Spray (29484) stuns her tank with everyone else for 10 s while she goes on hitting it, every hit landing (no dodge, parry or
        // block while stunned): 3.2-4.8k a swing on a 12.3k tank (naxx_maexxna r24h, dead 7-9 s into the first spray). The tanks drink a
        // Greater Stoneshield Potion (+2000 armour, 2 min) when she is engaged, as on Patchwerk.
        r.resistPotion.push_back({ 15952, 13455, Who::Tank });

        // ---------------------------------------------------------------------------------------------------------------
        // NOTH THE PLAGUEBRINGER (15954): Curse of the Plaguebringer 29213 turns into Wrath of the Plaguebringer (4,000 shadow to
        // everybody near the carrier) when it runs out, so it is dispelled before anything else; balcony phase adds
        // (naxx/classic_boss_noth.cpp).
        r.dispel.push_back({ 29213, DispelKind::Curse, true });
        // The Wrath hits everybody near a carrier the dispellers did not reach in time: the ranged and the healers keep 10 yards apart.
        r.spread.push_back({ 15954, 10.0f, Who::Ranged });
        r.spread.push_back({ 15954, 10.0f, Who::Healer });
        // Blink 29208-29211 wipes his threat list and he turns on a random raider (classic_boss_noth.cpp BlinkAndRepeatEvent: ResetThreatList,
        // AttackStart): a tank taunts him back at once (naxx_noth 2026-10-20: 30 melee deaths of casters and healers over 13 minutes, every one
        // under rule loose_escape, with no retaunt in the log).
        r.retaunt.push_back(15954);
        r.priority.push_back({ 16983, 3 });     // Plagued Champion
        r.priority.push_back({ 16981, 3 });     // Plagued Guardian
        r.priority.push_back({ 16984, 2 });     // Plagued Warrior

        // ---------------------------------------------------------------------------------------------------------------
        // HEIGAN THE UNCLEAN (15936): Decrepit Fever 29998 (naxx/classic_boss_heigan.cpp). The eruption dance is scripted.
        r.dispel.push_back({ 29998, DispelKind::Disease });
        // Mana Burn 29310 every 3 s on mana users within 28 yd in the fight phase: RaidMechanics_Naxx.cpp keeps them out.

        // ---------------------------------------------------------------------------------------------------------------
        // LOATHEB (16011): spores 16286 give the killers a crit buff (naxx/classic_boss_loatheb.cpp:458-466).
        r.priority.push_back({ 16286, 4 });     // Spore
        // Corrupted Mind 29201 hands every priest/druid (29194), paladin (29196) and shaman (29198) a one-heal-per-minute
        // debuff (:511-546). The heals fail while it lasts, so the healers rotate on their own; it is never dispelled.
        r.noDispel.push_back(29194);
        r.noDispel.push_back(29196);
        r.noDispel.push_back(29198);

        // ---------------------------------------------------------------------------------------------------------------
        // INSTRUCTOR RAZUVIOUS (16061): priests mind-control the Deathknight Understudies (16803), which taunt and tank him
        // (RaidMechanics_Naxx.cpp). The understudies must stay alive for that, so DPS never attack them; tanks hold the free ones.
        r.priority.push_back({ 16803, -1 });

        // ---------------------------------------------------------------------------------------------------------------
        // GOTHIK THE HARVESTER (16060): waves on both sides (naxx/classic_naxxramas.h:114-120). Riders hit hardest. He teleports
        // between the sides once he comes down: a tank taunts him back each time.
        r.retaunt.push_back(16060);
        r.priority.push_back({ 16126, 4 });     // Unrelenting Rider
        r.priority.push_back({ 16125, 3 });     // Unrelenting Deathknight
        r.priority.push_back({ 16124, 2 });     // Unrelenting Trainee
        r.priority.push_back({ 16150, 4 });     // Spectral Rider
        r.priority.push_back({ 16149, 3 });     // Spectral Horse
        r.priority.push_back({ 16148, 3 });     // Spectral Deathknight
        r.priority.push_back({ 16127, 2 });     // Spectral Trainee

        // ---------------------------------------------------------------------------------------------------------------
        // THE FOUR HORSEMEN (16062 Mograine, 16063 Zeliek, 16064 Thane, 16065 Blaumeux): Blaumeux's Void Zones are creatures
        // 16697 (naxx/classic_boss_four_horsemen.cpp:59,501) pulsing Consumption 28865 (radius index 15 = 3 yd, 3,960 shadow);
        // 5.5 yd = 3 yd plus combat reach plus a step. Zeliek's Holy Wrath chains between close players.
        r.avoidCreature.push_back({ 16697, 5.5f, Who::Anyone });
        r.spread.push_back({ 16063, 10.0f, Who::Ranged });
        r.spread.push_back({ 16063, 10.0f, Who::Healer });
        // Tank rotation: every mark (cast every 12 s, classic_boss_four_horsemen.cpp:407) stacks, and from the 3rd stack it hurts
        // (1000 / 3000 / 1000 x stacks). At 3 marks the tank hands its horseman to a tank without that mark, who walks over from
        // its own corner if it is out of taunt range, and the old tank moves on to a horseman whose mark it does not carry.
        r.tankSwap.push_back({ 16062, 28834, 3 });  // Highlord Mograine: Mark of Mograine
        r.tankSwap.push_back({ 16064, 28832, 3 });  // Thane Korth'azz: Mark of Korth'azz
        // Thane's Meteor 28884 (12825 fire split among everybody within 8 yd of a random player in 20 yd of him, every 12-15 s,
        // classic_boss_four_horsemen.cpp:684-695): his melee stack on him so it is shared.
        r.stayNear.push_back({ 16064, 8.0f, Who::Melee });
        r.tankSwap.push_back({ 16065, 28833, 3 });  // Lady Blaumeux: Mark of Blaumeux
        r.tankSwap.push_back({ 16063, 28835, 3 });  // Sir Zeliek: Mark of Zeliek

        // ---------------------------------------------------------------------------------------------------------------
        // PATCHWERK (16028): Hateful Strike hits the highest-health player in melee among his top four on threat, never the main
        // tank while others are in melee (naxx/classic_boss_patchwerk.cpp DoHatefulStrike). Three off tanks stay on him in melee.
        r.meleeSoak.push_back({ 16028, Who::OffTank, 3 });
        // The soakers' armour is what Hateful Strike meets: a Greater Stoneshield Potion (+2000) on every tank when he is engaged, as a vanilla
        // raid's tanks drank for him. naxx_patchwerk 1115b: T3 warriors at 9621 armour and 10759 health took 9-15k a strike and died to one.
        r.resistPotion.push_back({ 16028, 13455, Who::Tank });
        // Hateful Strike is 22,100-29,900 before armour (vanilla data) on anybody within his melee reach, and his reach is long: the ranged
        // and the healers keep 18 yards off (naxx_patchwerk 2026-10-20: healers and mages 10-13 y from him took 14-28k each; 29 dead in 47 s).
        r.avoidEnemy.push_back({ 16028, 18.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 16028, 18.0f, Who::Healer });
        // Where he is fought: in his hall, the floor he walks (z 294.1, y -3217..-3247 from x 3248 to 3333, his spawn 3308.5,-3232.1),
        // where everybody sees everybody. naxx-patch-grid (2026-11-15, [scenario_grid] round his spawn): the whole hall is level with him
        // and in his sight; the floor south of it (y -3257..-3292) lies 2 y lower and out of his sight, and the one north 4 y higher and
        // out of sight too. naxx_patchwerk 1114f and 1114g fought from that lower floor: 89 heals and shots failed for line of sight, the
        // main tank went up into the hall alone and the healers followed it into his reach. His walk comes back east along y -3230 to his
        // spawn, and he is met close to it (naxx_patchwerk 1115a: engaged at 3296,-3236 and fought at 3290-3311,-3232..-3249). The main
        // tank holds him by his spawn at 3305,-3232.
        // The ranged stand at the hall's east end with the healers, 4 y further east between their corners (3330,-3227 and 3330,-3237): 25 y
        // from the main tank's post, past Hateful Strike's 18 y and inside every ranged spell's reach to him (30 y + his reach), and they
        // wait there before the pull (NaxxPrePullPatchwerk). Their post was west of him at 3272,-3240, and from the lower floor the way up
        // to it runs past him: in the 1115 threat run all 16 walked into his 18 y circle and out again for the whole fight, 0 damage in 90 s.
        // The healers stand at the hall's east end in its two corners, 3326,-3223 and 3326,-3241: 20 y from his spawn and 23-26 y from him
        // where the main tank holds him (west of its post, he comes from the west), and off his walk, so they wait there before the pull
        // (NaxxPrePullPatchwerk) instead of walking 75 y from the lower floor after it (naxx_patchwerk 1115i: 29 of 61 s walking, no heal
        // for 18 s, every tank dead). 1115a had them at that end 8-12 y from his spawn: they walked into his reach and Hateful Strike
        // killed them; the corners keep the 18 y.
        constexpr float kPatchHallZ = 294.1f;
        r.stand.push_back({ 16028, Who::MainTank, 3305.0f, -3232.0f, kPatchHallZ, 5.0f, true, 0 });
        r.stand.push_back({ 16028, Who::Ranged, 3330.0f, -3227.0f, kPatchHallZ, 4.0f, false, 0, 100.0f, 0.0f, 1 });
        r.stand.push_back({ 16028, Who::Ranged, 3330.0f, -3237.0f, kPatchHallZ, 4.0f, false, 0, 100.0f, 0.0f, 2 });
        r.stand.push_back({ 16028, Who::Healer, 3326.0f, -3223.0f, kPatchHallZ, 4.0f, false, 0, 100.0f, 0.0f, 1 });
        r.stand.push_back({ 16028, Who::Healer, 3326.0f, -3241.0f, kPatchHallZ, 4.0f, false, 0, 100.0f, 0.0f, 2 });
        // The posts are for a fight in his hall. Pulled anywhere else (Karrz 11-15 live: met at the hall's west end, 3210,-3190, on his
        // walk), the healers ran 120 y off to the east corners and the ranged 80 y to theirs, nobody healed the soakers and every tank died
        // to Hateful Strike in the first 30 s. Past 40 y from a post he is fought where he is, by the plain raid layout.
        for (Stand& s : r.stand)
            if (s.enemy == 16028)
                s.maxFromEnemy = 40.0f;

        // ---------------------------------------------------------------------------------------------------------------
        // GROBBULUS (15931): Mutating Injection 28169 explodes into a poison cloud (creature 16363, triggers 28241 every
        // second); Slime Spray 28157 to the front spawns Fallout Slimes 16290 (naxx/classic_boss_grobbulus.cpp).
        // Dispelling it early only halves the explosion, the poison cloud spawns either way where the carrier stands
        // (classic_boss_grobbulus.cpp:224-257), so nobody dispels it in the middle of the raid: the carrier runs out.
        r.noDispel.push_back(28169);
        r.carrierRun.push_back({ 28169, 15.0f, Who::NonTank });
        r.avoidCarrier.push_back({ 28169, 10.0f, Who::NonTank });
        r.avoidCreature.push_back({ 16363, 9.0f, Who::Anyone });
        // The clouds last, and he stood in them: naxx21 the melee held off him for 61-63 of 90 s (raid:melee_hold, his centre inside a
        // cloud's 9 y), 36% of his health in 90 s, and the tanks died at 88-104 s. His tank leads him out of them.
        r.dragOut.push_back({ 15931, 16363, 11.0f });
        r.avoidFrontal.push_back({ 15931, 20.0f, 50.0f, Who::NonTank });
        // Every player the spray hits leaves a Fallout Slime where it stands (classic_boss_grobbulus.cpp SpellHitTarget, one each), so
        // the main tank is the only one in front of him: the off tanks keep the Fallout Slimes from his flank. naxx_grobbulus 1115a: all
        // four tanks stood on him in front from 69 s, one spray at 160 s put six slimes on them at once, the four tanks died to the slimes
        // in 11 s and the slimes then killed 30 of the raid.
        r.avoidFrontal.push_back({ 15931, 20.0f, 50.0f, Who::OffTank });
        r.priority.push_back({ 16290, 3 });     // Fallout Slime

        // ---------------------------------------------------------------------------------------------------------------
        // GLUTH (15932): Mortal Wound 25646 stacks -10% healing on the tank (naxx/classic_boss_gluth.cpp): swap at 5.
        // Zombie Chows (16360) heal him when he eats them; Decimate leaves them at 5% for the AoE.
        r.tankSwap.push_back({ 15932, 25646, 5 });
        // Frenzy 28371: Tranquilizing Shot is the PvE coordinator's (PlanTranq, any aura the shot removes). Frenzy every 10 s: tranquilized (classic_boss_gluth.cpp:221-232)
        // Zombie Chows heal him 5% each when he eats one within 15 yd (:290-312). Each has 146,496 health (creaturestats, 1115e-b) against his
        // 1,998,600: the raid does not burn them. The chow tanks hold them at their posts (RaidMechanics_Naxx.cpp, Gluth) until Decimate leaves
        // every one at 5% and sends it walking to him; then they die first, before they reach him.
        r.priority.push_back({ 16360, 5, Who::Anyone, 16360, 10.0f, 0.0f });   // Zombie Chow after Decimate
        r.priority.push_back({ 16360, -1 });                                     // Zombie Chow: tanked, not killed
        // Where: away from the chows' three spawns (ClassicGluthZombieSummonLoc, +-7 y: 3268,-3172 / 3253,-3132 / 3308,-3186, the room's west
        // and south side). naxx-gluth-1115b [scenario_grid]: level floor from x 3233 to 3338 and y -3107 to -3207, the way in at the
        // north-west (3253-3263,-3100). His body is 15 y wide and he hits from 17.7 y (1115e), so his tank does not lead him to a post:
        // RaidMechanics_Naxx.cpp (Gluth) drags him to the east wall and puts his tank on the camp side of him, 30-40 y from the camp, and
        // keeps the chow tanks at two posts by the spawns. naxx_gluth 1115e, with his tank on a north-east post 33 y from the camp: he stood
        // 17 y short of it and 19 y from the camp, ate the chows the raid drew, and killed the camp once his tank fell (42 deaths, all his).
        // The camp is on the north wall, 28 y from where the raid lands, within 43 y of the drag spot and 40 y of both chow posts (1115f's
        // camp 3280,-3115 was 57 y from the drag spot, out of heal reach while his tank dragged him).
        constexpr float kGluthFloorZ = 297.8f;
        r.stand.push_back({ 15932, Who::Ranged, 3290.0f, -3110.0f, kGluthFloorZ, 6.0f, false, 0 });
        r.stand.push_back({ 15932, Who::Healer, 3290.0f, -3110.0f, kGluthFloorZ, 6.0f, false, 0 });

        // ---------------------------------------------------------------------------------------------------------------
        // THADDIUS (15928): Polarity Shift gives Positive Charge 28059 / Negative Charge 28084 (naxx/classic_boss_thaddius.cpp).
        // Same charges stack up together, so each charge group stands on its own side of him. Sides follow the line through
        // Stalagg's and Feugen's platforms (ClassicThadAddPositions, :154-157): bearing 2.36 rad points at one, -0.78 at the other.
        r.standByAura.push_back({ 15928, 28059, 2.36f, 9.0f, 4.0f });
        r.standByAura.push_back({ 15928, 28084, -0.78f, 9.0f, 4.0f });
        // Feugen (15930) burns the mana of everybody within 30 yd of him (Static Field 28135, vanilla data: power burn, radius index 10):
        // his healers and the ranged on him stand 32 yards off (his tank is still within their 40).
        r.avoidEnemy.push_back({ 15930, 32.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 15930, 32.0f, Who::Healer });
        // Stalagg and Feugen go down together: each one fakes death at 1 hp for 5 s and stands up at full health unless the other is down
        // too (classic_boss_thaddius.cpp fakeDeathTimer, HandleReviveEvent). naxx_thaddius 1114d: the raid took them one at a time,
        // Stalagg 4% -> 99%, Feugen 9% -> 99%, five times over 600 s, and wiped. Damage is spread, neither more than 10% below the other.
        r.killTogether.push_back({ 15929, 0, 10.0f, { 15930 } });

        // ---------------------------------------------------------------------------------------------------------------
        // SAPPHIRON (15989): Life Drain 28542, Blizzard creatures 16474 that drift around (naxx/classic_boss_sapphiron.cpp),
        // Cleave / Tail Sweep front and back. Life Drain heals him for what it takes, so it goes before any other dispel.
        r.dispel.push_back({ 28542, DispelKind::Curse, true });
        // Frost Aura, Blizzard and Frost Breath: Greater Frost Protection for everyone.
        r.resistPotion.push_back({ 15989, 13456, Who::Anyone });
        r.avoidCreature.push_back({ 16474, 8.0f, Who::Anyone });
        // Cleave 19983 is a chain of 10 (SpellEffect: ChainTargets 10), 5 y a jump for a melee spell (Spell::SearchChainTargets): from his
        // tank to whoever stands within 5 y of the tank, and on. naxx22 1116: 9 healers cleaved 9-23 y from him, a chain through the camp
        // behind his tank, which the old 50 degree cone left alone. His whole front half is kept clear to 30 y (his tank stands in it, at
        // his reach; anyone within a jump of it is in it). Tail Sweep 15847 is Onyxia's spell (radius index 10, 30 y; 60 degrees each side
        // of straight behind, RaidEncounters.h kOnyTailHalfDeg). The raid stands on his flanks between them, or beyond 30 y.
        r.avoidFrontal.push_back({ 15989, 30.0f, 90.0f, Who::NonTank, 0.0f, 100.0f, 0.0f, true, 6.0f });
        r.avoidFrontal.push_back({ 15989, 30.0f, kOnyTailHalfDeg, Who::NonTank, 180.0f, 100.0f, 0.0f, true, 6.0f });   // Tail Sweep behind him

        // ---------------------------------------------------------------------------------------------------------------
        // KEL'THUZAD (15990): Detonate Mana 27819 on a mana user (the carrier runs, others keep clear), Shadow Fissure 27810
        // summons Shadow Fissure 16129 (spell 27810 effect 41, misc 16129). The fissure carries no aura: 3 s after it appears it
        // casts Void Blast 27812 (62,500 shadow, radius index 8 = 5 yd around itself) and despawns 2.25 s later
        // (classic_mob_shadow_fissure, naxx/classic_boss_kelthuzad.cpp). 7.5 yd = 5 yd plus the target's combat reach plus a step.
        // Frost Bolt Nova punishes stacking (naxx/classic_boss_kelthuzad.cpp:85-95).
        r.carrierRun.push_back({ 27819, 15.0f, Who::ManaUser });
        r.avoidCarrier.push_back({ 27819, 12.0f, Who::Anyone });
        r.avoidCreature.push_back({ 16129, 7.5f, Who::Anyone });
        r.spread.push_back({ 15990, 10.0f, Who::Ranged });
        r.spread.push_back({ 15990, 10.0f, Who::Healer });
        // Phase 1 adds (naxx/classic_naxxramas.h:123-126). Guardians of Icecrown (phase 3) are shackled.
        r.priority.push_back({ 16429, 4 });     // Soul Weaver
        // Soldiers explode (Dark Blast 28457) when they reach somebody, so the ranged shoot them down on the way in; the melee keep
        // the Soul Weavers and the Abominations (first matching row per bot wins, so the ranged row comes first).
        r.priority.push_back({ 16427, 6, Who::Ranged });
        r.priority.push_back({ 16427, 3 });     // Soldier of the Frozen Wastes
        // No tank holds a Soldier: each one is sent at a random raider and blows up on arrival (classic_kt_p1AddAI, Dark Blast 28457), so
        // the raid shoots them on the way in and the tanks keep the Abominations (naxx-kt-1114e: the tanks chased Soldiers 26-59 y out and
        // the DPS held their damage waiting for them, 0 damage a second for 18 s).
        r.noTank.push_back(16427);
        r.priority.push_back({ 16428, 2 });     // Unstoppable Abomination
        // The seven alcoves are out of bounds (naxx-kt-1114d: the tanks met the first Soldier at the south alcove, 3704,-5170, pulled its
        // 3 Abominations and its Soul Weaver and the raid died in 18 s). Each alcove keeps 10 Soldiers wandering within 14 y of its
        // centre, 3 Abominations and a Soul Weaver (classic_boss_kelthuzad.cpp:518-556); they wake for anyone within their own aggro
        // range (about 13-15 y) and 30 y 2D (classic_kt_p1AddAI). 30 y round each centre covers both; the centres lie 61-65 y from Kel'Thuzad,
        // so the middle keeps about 31 y of room, where the raid fights the adds the script sends at it. The static adds stay up
        // through phase 2, so the circles hold on Kel'Thuzad too: in phase 1 he is not in the raid's combat (immune), the sent
        // Soldiers are.
        for (auto const& alcove : { std::pair{ 3768.40f, -5072.00f }, { 3729.30f, -5044.10f }, { 3683.00f, -5054.05f }, { 3654.15f, -5093.48f },
                                    { 3664.55f, -5140.50f }, { 3704.00f, -5170.00f }, { 3751.95f, -5158.90f } })
        {
            r.keepOut.push_back({ 16427, alcove.first, alcove.second, 30.0f });
            r.keepOut.push_back({ 15990, alcove.first, alcove.second, 30.0f });
        }
        // Soul Weaver: Wail of Souls 28459, damage and a knockback in front of it within 30 yd (vanilla data, radius index 10).
        r.avoidFrontal.push_back({ 16429, 30.0f, 60.0f, Who::NonTank });
        // Shackle at most 3 Guardians: a 4th frees them all (classic_boss_kelthuzad.cpp:1027-1100).
        r.crowdControl.push_back({ 16441, CcKind::Shackle, 0, 3 });
        // A Guardian that is tanked stacks Blood Tap 28470 on its tank (+damage, +size per stack, classic_boss_kelthuzad.cpp:104):
        // another tank takes it at 4.
        r.tankSwap.push_back({ 16441, 28470, 4 });
        // Chains of Kel'Thuzad 28410 mind-controls a raider (:690-704): sheep or stun, never kill.
        r.controlledMember.push_back({ 28410 });

        // ---------------------------------------------------------------------------------------------------------------
        // TRASH (casts: sql/custom/world/2026_09_28_15_world_vanilla_smartai.sql, classic_instance_naxxramas.cpp; names, radii,
        // durations and dispel types: classic spell data). The dispel kind is a fallback, the brain reads the real type.
        r.dispel.push_back({ 28431, DispelKind::Poison });   // Poison Charge, 2500 every 3 s for 9 s: Venom/Necro Stalker (SAI 15976, 16453)
        r.dispel.push_back({ 29848, DispelKind::Magic });    // Polymorph, Unholy Staff 16215
        // (removed) Aura of Agony 28413 (Deathknight Cavalier/Vindicator): the spell data lists no dispel type, a dispel never lands.
        r.dispel.push_back({ 30113, DispelKind::Disease });  // Putrid Bite, +120 damage taken for 30 s: Plagued Bat 16037 on the tank
        r.dispel.push_back({ 28311, DispelKind::Poison });   // Slime Bolt, 475 every 5 s: Stitched Spewer 16025
        r.dispel.push_back({ 28440, DispelKind::Curse });    // Veil of Shadow, -75% healing taken for 6 s: Dread Creeper 15974
        r.dispel.push_back({ 28350, DispelKind::Curse });    // Veil of Darkness, cannot be healed for 7 s: Deathknight 16146
        // Construct wing.
        // Patchwork Golem 16017: War Stomp 27758, weapon + 936 and a 5 s stun within 15 yd; Cleave 27794 (2.5 s cast).
        r.avoidEnemy.push_back({ 16017, 18.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 16017, 18.0f, Who::Healer });
        r.avoidFrontal.push_back({ 16017, 10.0f, 70.0f, Who::NonTank });
        // Bile Retcher 16018: Bile Vomit 27807 on its victim, 1750 nature + DoT to everybody within 20 yd in front.
        r.avoidFrontal.push_back({ 16018, 24.0f, 50.0f, Who::NonTank });
        // Stitched Spewer 16025: Knockback 28405, 1850 physical and a knockback within 15 yd.
        r.avoidEnemy.push_back({ 16025, 18.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 16025, 18.0f, Who::Healer });
        // Embalming Slime 16024: Embalming Cloud 28322, 3515 nature within 8 yd of the slime.
        r.avoidEnemy.push_back({ 16024, 11.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 16024, 11.0f, Who::Healer });
        // Spider wing.
        // Carrion Spinner 15975: Poison Spray 30043, 1313 nature within 15 yd of the spider.
        r.avoidEnemy.push_back({ 15975, 18.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 15975, 18.0f, Who::Healer });
        // Naxxramas Cultist 15980: Shadow Burst 28447, shadow and a knockback within 8 yd.
        r.avoidEnemy.push_back({ 15980, 11.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 15980, 11.0f, Who::Healer });
        // Plague wing.
        // Plague Beast 16034: Mutated Spores 30110 pulses 30111 (324 nature within 10 yd) every 3 s, and Trample 5568 hits
        // everybody within 10 yd every 3 s.
        r.avoidEnemy.push_back({ 16034, 13.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 16034, 13.0f, Who::Healer });
        // Military wing.
        // Deathknight Captain 16145: Whirlwind 28333, a 6 s channel (weapon + 300 to everybody near it every 2 s): everybody but
        // the tank steps out while it spins. Unholy Axe 16194: Whirlwind 24236, a 2 s channel of 15589 (8 yd).
        r.avoidEnemyAura.push_back({ 16145, 28333, 11.0f, Who::NonTank });
        r.avoidEnemyAura.push_back({ 16194, 24236, 11.0f, Who::NonTank });
        // Bony Construct 16167: Sweeping Slam 25322, weapon + 1750-2250 and a knockback in a 15-yard frontal cone; Cleave 19632.
        r.avoidFrontal.push_back({ 16167, 18.0f, 60.0f, Who::NonTank });
        // Unholy Swords 16216: Cleave 19632.
        r.avoidFrontal.push_back({ 16216, 10.0f, 70.0f, Who::NonTank });
        // Unholy Staff 16215: Arcane Explosion 28450, 1838 arcane and a stun within 20 yd (0.5 s cast); Frost Nova 29849 (10 yd).
        r.avoidEnemy.push_back({ 16215, 23.0f, Who::Ranged });
        r.avoidEnemy.push_back({ 16215, 23.0f, Who::Healer });
        // Stoneskin / Plagued Gargoyles (16168 / 16446) channel Stoneskin 28995 below 30% (6 s, immune, heals
        // 100000 a second; classic_naxxramas_gargoyle_ai): burn them first so they get there late and die through the cast.
        r.priority.push_back({ 16168, 4 });
        r.priority.push_back({ 16446, 4 });
        // Frenzy 24318: Tranquilizing Shot is the PvE coordinator's (PlanTranq, any aura the shot removes). Frenzy at 30%, Infectious/Plagued Ghoul (SAI 16244, 16447)
        r.priority.push_back({ 15981, 4 });     // Naxxramas Acolyte: Shadow Bolt Volley 28448
        r.priority.push_back({ 16368, 4 });     // Necropolis Acolyte: Arcane Explosion + Shadow Bolt Volley
        r.priority.push_back({ 16020, 5 });     // Mad Scientist: Great Heal at 30%, Mana Burn
        r.priority.push_back({ 16385, 6 });     // Lightning Totem from a Living Monstrosity
        r.priority.push_back({ 16021, 4 });     // Living Monstrosity: fear, Chain Lightning, totem
        r.priority.push_back({ 16022, 3 });     // Surgical Assistant: Mind Flay
        r.priority.push_back({ 16164, 5 });     // Shade of Naxxramas: Shadow Bolt Volley every 4 s
        r.priority.push_back({ 16420, 6 });     // Portal of Shadows from a Spirit of Naxxramas
        r.priority.push_back({ 16215, 5 });     // Unholy Staff: polymorph, Frost Nova, Arcane Explosion
        r.priority.push_back({ 16146, 4 });     // Deathknight: fear, Raise Dead

        // Scripted in RaidMechanics_Naxx.cpp: Heigan's eruption dance and Mana Burn range, Sapphiron's air phase (spread,
        // hide behind ice blocks), Razuvious' mind-controlled understudies, Faerlina's worshipper sacrifice, Gothik's side
        // split (pre-pull) and per-side targets while his combat gate is closed.
        //
        // Not ruled:
        //  * Thaddius: Forever's port gives every player Negative Charge (classic_boss_thaddius.cpp:851-858), so the charge
        //    sides above all resolve to one side; harmless. Magnetic Pull tank swap of Stalagg/Feugen relies on pvecoord.
        //  * Noth's balcony phase (immune 29230, adds) and blink: the generic tank pickup handles both.
        //  * Kel'Thuzad: Frost Blast 27808 (heal through).
        //  * Loatheb Inevitable Doom 29204 (heal through); Anub'Rekhan Impale (random line, nothing to dodge in time).
        //  * Trash: Plague Slime 16243 changes school immunity with its colour (needs the casters' rotations to pick a school);
        //    Plague Beast 16034 Mutated Spores (radius not in the spell data); Gargoyle Stoneskin needs an interrupt (pvecoord).
    }

} // namespace Playerbot::Raid
