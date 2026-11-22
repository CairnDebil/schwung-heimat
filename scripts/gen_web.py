#!/usr/bin/env python3
"""Keep the browser editor and the manual in step with the engine.

Reads the parameter table (g_params) from src/dsp/heimat.c and writes it
  * as JSON into src/web/web_ui.html (between the heimat-params script tags),
  * as the parameter reference table into docs/index.html (between the
    params:begin / params:end comments),
then copies the editor to docs/editor.html so the manual can show it in demo mode.

  python3 scripts/gen_web.py          update the files
  python3 scripts/gen_web.py --check  exit 1 if any file is out of date
"""
import html, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'dsp', 'heimat.c')
EDITOR = os.path.join(ROOT, 'src', 'web', 'web_ui.html')
MANUAL = os.path.join(ROOT, 'docs', 'index.html')
DEMO = os.path.join(ROOT, 'docs', 'editor.html')

# which section of the manual / editor each parameter belongs to
GROUPS = [
    ('Genes', ['gene_size', 'slide', 'morph', 'vari_speed', 'organize', 'organize_mode']),
    ('Micro', ['gene_window', 'scatter', 'spread', 'spread_mode', 'gene_reverse', 'width', 'threshold', 'thr_mode',
               'pattern', 'pattern_seed']),
    ('EQ', ['eq_low', 'eq_mid', 'eq_freq', 'eq_high']),
    ('Heads', ['hd_mix', 'hd_time', 'hd_heads', 'hd_spacing', 'hd_fdbk', 'hd_reverse', 'hd_wear', 'hd_wear_amt',
               'hd_mode', 'hd_route', 'hd_freeze']),
    ('Reverb', ['rev_mix', 'rev_size', 'rev_predelay', 'rev_reverse', 'rev_decay', 'rev_damp', 'rev_tilt',
                'rev_depth', 'rev_speed']),
    ('Modulators', ['mod_wave_shape', 'mod_wave_rate', 'mod_wave_sync', 'mod_wave_retrig',
                    'mod_rand_rate', 'mod_rand_smooth', 'mod_rand_sync', 'mod_rand_retrig',
                    'mod_follow_src', 'mod_follow_gain', 'mod_follow_attack', 'mod_follow_release',
                    'mod_env_attack', 'mod_env_decay', 'mod_env_mode', 'mod_env_trigger', 'mod_restart']),
    ('Play', ['pad_mode', 'play_mode', 'filter', 'release_ms', 'root_note', 'octave_transpose', 'bend_range', 'volume', 'mute']),
    ('Splices and chords', ['splice_source', 'divide', 'splice_base', 'mcr1', 'mcr2', 'mcr3']),
    ('Recording', ['sos', 'in_gain', 'rec_source', 'rec_sync', 'rec_length', 'rec_after', 'rec_head', 'rec_trim', 'in_gate',
                   'fx_rec', 'save_48k']),
    ('Clock', ['clock_source', 'clock_div', 'clock_mode', 'bpm']),
]


def num(s):
    s = s.strip().rstrip('f')
    s = s.replace('(float)', '').strip('() ')
    return float(s)


def parse():
    c = open(SRC, encoding='utf-8').read()
    opts = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
            for m in re.finditer(r'static const char \*const (OPT_\w+)\[\]\s*=\s*\{([^}]*)\}', c)}
    consts = {'PAD_PITCH': 0, 'PLAY_MOMENTARY': 1, 'CLK_OFF': 0, 'CM_AUTO': 0}
    table = c[c.index('static const pdef_t g_params[P_COUNT]'):]
    table = table[:table.index('};')]
    out = []
    for line in table.splitlines():
        m = re.match(r'\s*\[P_\w+\]\s*=\s*(PF|PI|PE)\((.*)\),?\s*$', line)
        if not m:
            continue
        kind, args = m.group(1), m.group(2)
        a = [x.strip() for x in re.findall(r'"[^"]*"|[^,]+', args)]
        key, name, short = (x.strip('"') for x in a[:3])
        p = {'key': key, 'name': name, 'short': short}
        if kind == 'PF':
            p.update(type='float', min=num(a[3]), max=num(a[4]), default=num(a[5]), step=num(a[6]))
        elif kind == 'PI':
            p.update(type='int', min=num(a[3]), max=num(a[4]), default=num(a[5]), step=1)
        else:
            o = opts[a[3]]
            d = a[5]
            p.update(type='enum', options=o, min=0, max=len(o) - 1,
                     default=float(consts[d]) if d in consts else num(d), step=1)
        out.append(p)
    group_of = {k: g for g, ks in GROUPS for k in ks}
    for p in out:
        p['group'] = group_of.get(p['key'], 'Other')
    return out


def fmt_num(v):
    return ('%g' % v)


def ref_table(params):
    rows = []
    for g, keys in GROUPS:
        rows.append('<tr class="grp"><th colspan="4" scope="rowgroup">%s</th></tr>' % html.escape(g))
        for k in keys:
            p = next(x for x in params if x['key'] == k)
            if p['type'] == 'enum':
                rng = ' / '.join(html.escape(o) for o in p['options'])
                d = html.escape(p['options'][int(p['default'])])
            else:
                rng = '%s to %s' % (fmt_num(p['min']), fmt_num(p['max']))
                d = fmt_num(p['default'])
            rows.append('<tr><td>%s</td><td><code>%s</code></td><td>%s</td><td>%s</td></tr>'
                        % (html.escape(p['name']), html.escape(k), rng, d))
    return '\n'.join(rows)


def splice(text, start, end, body):
    i, j = text.index(start) + len(start), text.index(end)
    return text[:i] + body + text[j:]


def main():
    check = '--check' in sys.argv
    params = parse()
    assert len(params) >= 40, 'parameter table not found (%d entries)' % len(params)
    stale = []

    ed = open(EDITOR, encoding='utf-8').read()
    ed2 = splice(ed, '<script id="heimat-params" type="application/json">', '</script>',
                 '\n' + json.dumps(params, separators=(',', ':')) + '\n')
    if ed2 != ed:
        stale.append(EDITOR)
        if not check:
            open(EDITOR, 'w', encoding='utf-8').write(ed2)

    if os.path.exists(MANUAL):
        man = open(MANUAL, encoding='utf-8').read()
        man2 = splice(man, '<!-- params:begin -->', '<!-- params:end -->', '\n' + ref_table(params) + '\n')
        if man2 != man:
            stale.append(MANUAL)
            if not check:
                open(MANUAL, 'w', encoding='utf-8').write(man2)

    demo_old = open(DEMO, encoding='utf-8').read() if os.path.exists(DEMO) else ''
    if demo_old != ed2:
        stale.append(DEMO)
        if not check:
            open(DEMO, 'w', encoding='utf-8').write(ed2)

    if check:
        if stale:
            print('out of date (run python3 scripts/gen_web.py):', ', '.join(os.path.relpath(s, ROOT) for s in stale))
            sys.exit(1)
        print('web files up to date (%d parameters)' % len(params))
    else:
        print('%d parameters; updated: %s' % (len(params), ', '.join(os.path.relpath(s, ROOT) for s in stale) or 'nothing'))


if __name__ == '__main__':
    main()
