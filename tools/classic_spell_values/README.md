# Vanilla (1.12) numbers for Classic: the builder

WoW Forever Bots (and the Classic rules in this tree) can use the original 1.12 numbers: spell damage and healing, mana costs, base
mana, creature levels, resistances, melee damage, school and mechanic immunities, spell timing. **These numbers are not in this repository.**
You build them from the VMaNGOS world database dump, which you download yourself. This repository holds only the code that reads it, and
the empty tables (`sql/custom/world/2026_10_10_01_*`, `2026_10_11_01_*`, `_05_*`, `_06_*`). With no data the tables stay empty and the
server uses the client's own values; nothing breaks.

## What you need

- Python 3.8 or newer (no extra packages).
- The VMaNGOS world database dump `world_full_14_june_2021.sql` (inside the `.7z`), from https://github.com/brotalnia/database .
  Unpack it with 7-Zip. The dump belongs to its authors; read its terms before you use it.

## Build

```
cd tools/classic_spell_values
python3 build_vanilla_data.py --dump /path/to/world_full_14_june_2021.sql
```

It writes six files into `sql/custom/world/` (named `2026_10_12_50_...` to `_55_...`; they are ignored by git). Start the worldserver:
its database updater applies them. Run the builder again whenever you like; every file is safe to run again.

No Python on the server PC? On Windows, `extract-vmangos.ps1 -Dump <dump>` makes a small `data\vmangos_extract.sql.gz` (about 2 MB)
that you can carry to a machine with Python and pass as `--dump`.

### Spell timing and Forever "twins" (optional)

Cast times, cooldowns, durations and ranges (`_56_`) and the Forever copies of vanilla spells (`_57_`) also need two things from your side:
`--audit`, the `SpellAudit.tsv` that `.playerbot spellaudit` writes on your own server, and `--dbc`, the `dbc` folder of a 1.12 client
(`SpellCastTimes.dbc`, `SpellDuration.dbc`, `SpellRadius.dbc`, `SpellRange.dbc`).

```
python3 build_vanilla_data.py --dump world_full_14_june_2021.sql --audit SpellAudit.tsv --dbc /path/to/1.12/dbc --report twins.md
```

`twins.md` lists the spells that were too uncertain to map; nothing is guessed.

## Other scripts

`compare_spells.py`, `compare_creatures.py` and `creature_instance_report.py` write Markdown reports that compare your server with the
dump (coverage, mismatches). `selftest.py` runs the spell generator on a tiny made-up extract. `gen_spell_effects.py` is an experiment
that is not part of the build.
