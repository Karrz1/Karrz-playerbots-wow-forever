# Builds the vanilla (1.12) data files of WoW Forever Bots from a VMaNGOS world database dump that YOU download yourself.
# Nothing from that dump is shipped in this repository, only this code and the empty tables (sql/custom/world, "2026_10_10_01", "_11_01", "_11_05", "_11_06").
#
#   python3 build_vanilla_data.py --dump world_full_14_june_2021.sql [--out ../../sql/custom/world]
#
# --dump is the world_full_*.sql of the VMaNGOS database (https://github.com/brotalnia/database), or the small file made by extract-vmangos.ps1
# (data\vmangos_extract.sql.gz) when you do not want to keep the big one. This writes six files from the dump alone:
#   2026_10_12_50_world_classic_vanilla_spell_values.sql, _51_..._spell_costs.sql, _52_..._base_mana.sql,
#   2026_10_12_53_world_classic_vanilla_creature_levels.sql, _54_..._creature_melee.sql, _55_..._creature_immunities.sql
# Two more files need what your own server and a 1.12 client know: --audit (the SpellAudit.tsv of `.playerbot spellaudit` on your server) and
# --dbc (the dbc folder of a 1.12 client: SpellCastTimes, SpellDuration, SpellRadius and SpellRange .dbc). With both this also writes
#   2026_10_12_56_world_classic_vanilla_spell_info.sql, 2026_10_12_57_world_classic_vanilla_spell_twins.sql
# Run it again whenever you like (every file is safe to run again); the worldserver applies the new files at its next start.
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
P = '2026_10_12_%s_world_classic_vanilla_%s.sql'
DUMP_ONLY = (('gen_spell_values.py', ['values'], P % ('50', 'spell_values')),
             ('gen_spell_values.py', ['costs'], P % ('51', 'spell_costs')),
             ('gen_spell_values.py', ['mana'], P % ('52', 'base_mana')),
             ('gen_creature_stats.py', [], P % ('53', 'creature_levels')),
             ('gen_creature_melee.py', [], P % ('54', 'creature_melee')),
             ('gen_creature_immunities.py', [], P % ('55', 'creature_immunities')))
NEEDS_AUDIT = (('gen_spell_info.py', P % ('56', 'spell_info')),
               ('gen_spell_twins.py', P % ('57', 'spell_twins')))


def run(script, args, env, out_dir, name):
    cmd = [sys.executable, '-I', os.path.join(HERE, script)] + args
    r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode:
        sys.exit('%s failed:\n%s' % (script, r.stderr.decode('utf-8', 'replace')))
    with open(os.path.join(out_dir, name), 'wb') as f:
        f.write(r.stdout)
    print('wrote', name, '-', r.stderr.decode('utf-8', 'replace').strip())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', required=True, help='world_full_*.sql of the VMaNGOS database, or the .sql.gz made by extract-vmangos.ps1')
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'sql', 'custom', 'world'))
    ap.add_argument('--audit', help='SpellAudit.tsv of `.playerbot spellaudit` (with --dbc: also writes the spell timing and twin files)')
    ap.add_argument('--dbc', help='dbc folder of a 1.12 client')
    ap.add_argument('--report', help='where gen_spell_twins.py writes its list of uncertain twins (Markdown)')
    a = ap.parse_args()
    if not os.path.isfile(a.dump):
        sys.exit('dump not found: ' + a.dump)
    if bool(a.audit) != bool(a.dbc):
        sys.exit('--audit and --dbc go together')
    out_dir = os.path.abspath(a.out)
    os.makedirs(out_dir, exist_ok=True)
    dump = os.path.abspath(a.dump)
    env = dict(os.environ, VMANGOS_SRC=dump)
    for script, args, name in DUMP_ONLY:
        run(script, args, env, out_dir, name)
    if a.audit:
        extra = ['--audit', os.path.abspath(a.audit), '--extract', dump, '--dbc', os.path.abspath(a.dbc)]
        for script, name in NEEDS_AUDIT:
            more = ['--report', os.path.abspath(a.report)] if (a.report and script == 'gen_spell_twins.py') else []
            run(script, extra + more, env, out_dir, name)
    else:
        print('skipped spell timing and twin files (they need --audit and --dbc)')


main()
