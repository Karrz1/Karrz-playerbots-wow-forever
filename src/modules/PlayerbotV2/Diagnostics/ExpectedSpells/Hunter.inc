// Spells the Hunter rotation always uses in a 180 s damage leg against the dummy, by spell name, one string literal per line followed by a comma.
// A name listed here that the bot never got the core to accept in a reference leg (t1 / t3) fails damage_parity_hunter with an
// "expected_spell_not_cast" line. List only spells that need no condition beyond a live target (no procs, no execute range, no cooldown over 60 s). Aspect of the Hawk is left out: it is cast once and lasts the whole run, not in each leg.
"Arcane Shot",
"Multi-Shot",
"Serpent Sting",
"Hunter's Mark",
