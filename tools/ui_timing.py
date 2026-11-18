#!/usr/bin/env python3
"""UI timing diagnostic and remote panel (firmware built with --define BR_UI_TIMING_DIAG=1 only).

  ui_timing.py --expect FM-1_9262330 where
  ui_timing.py --expect FM-1_9262330 button lfo                 (tap; --hold MS for a long press)
  ui_timing.py --expect FM-1_9262330 button oct_down --hold 3000 --no-wait   (returns at once; turn meanwhile)
  ui_timing.py --expect FM-1_9262330 chord oct_down seq --hold 600   (FIRST held, SECOND tapped while it is)
  ui_timing.py --expect FM-1_9262330 key 13 --hold 200          (note key 0..26, white and black)
  ui_timing.py --expect FM-1_9262330 turn knob1 -3 --every 120  (one detent every 120 ms)
  ui_timing.py --expect FM-1_9262330 window --secs 20           (reset, wait, read: one JSON line)
  ui_timing.py --expect FM-1_9262330 read | reset

Port: --port (default "FM-1"). The identity is checked first; nothing else is sent if it differs.

EXACTLY these messages are sent (firmware/src/ui_timing_diag.c has the reply layouts):
  * identity handshake F0 00 32 45 00 00 00 40 7F F7 (SysEx cmd 0x11, empty body);
  * 0xB8 where (empty body; read-only) before anything else, so a firmware without the diagnostic
    is reported (no reply) before any other command is sent;
  * 0xB5 read (empty body; read-only), 0xB6 reset of these counters only (body 'U','T',1);
  * 0xB7 remote input (body 'U','T',1,kind,index,v0,v1): a button tap or long press, a chord of two
    (the second while the first is held), a note key, or encoder detents. This tool refuses PLAY and
    REC on their own (transport, record arm); REC only as a chord's second button. While the SAVE
    page is open it sends nothing but HOME and turns (the firmware refuses those too, and strips them
    in a race); SAVE only opens the page (a tap, never with a hold, never while the page is open), and
    the firmware refuses SAVE while REC is held and REC while a SAVE of ours is in flight.
No 0x61 (audio counter reset), note, transport, session, SAVE confirmation or upgrade message.
Standard library and mido only.

Window figures (from the unit's own elapsed ms): passes per second, the worst pass, drawing compute
(time in br_draw_pass_with less LCD waits) and LCD time in ms per second and as a share of the wall
time, pixel bytes per second, frames per second (all, and those that sent pixels), the worst frame,
played-note lag and input-to-screen (average and worst, ms). Tick totals are 32-bit at 24 MHz, so a
window is at most 170 s.
"""
import argparse, json, sys, time

TICKS_PER_MS = 24000.0
HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
CMD_READ, CMD_RESET, CMD_INPUT, CMD_WHERE = 0xB5, 0xB6, 0xB7, 0xB8
MAGIC = bytes((ord('U'), ord('T'), 1))
BTN = dict(oct_down=0, oct_up=1, fx=2, sel=3, env=4, lfo=5, edit=6, glo=7, home=8, save=9, arp=10, seq=11, play=12, rec=13)
ENC = dict(select=0, algorithm=1, knob1=2, knob2=3, knob3=4, knob4=5, presets=6)
REFUSED_BUTTONS = {'play': 'transport', 'rec': 'record arm / REC+SAVE quick save'}
PAGES = ['HOME', 'ENV', 'FX', 'MIX', 'SEQ', 'ARP/RETRIG', 'SOUNDS', 'LFO', 'ROUTE', 'PUNCH', 'PATTERNS']
WORDS = ['passes', 'pass_worst', 'pass_over5ms', 'pass_over10ms', 'draw_passes', 'draw_compute', 'lcd_ticks',
         'pixel_bytes', 'frames', 'frame_worst', 'frame_total', 'notes', 'note_lag_total', 'note_lag_worst',
         'notes_over6ms', 'inputs', 'input_total', 'input_worst', 'elapsed_ms', 'draw_ticks', 'lcd_in_draw',
         'draw_passes_sending', 'frames_sending', 'notes_stale', 'input_passes',
         'region_worst', 'region_where', 'draw_passes_timed', 'draw_pass_worst']   # the last four: 29-word firmware
STATUS = {1: 'accepted', 2: 'refused: the SAVE page is open (only HOME and encoder turns)',
          3: 'refused: busy (both slots in use, the first not yet held, or the same button)',
          4: 'refused: SAVE while REC is held or pending, or REC while a SAVE of ours is in flight',
          5: 'refused: SAVE with a hold', 6: 'refused: bad kind, index or value',
          7: 'the press did not register within 100 ms', 8: 'the hold was cut short: the SAVE page opened'}
NO_DIAG = 'no reply to 0x%02X: this firmware has no UI timing diagnostic (build with --define BR_UI_TIMING_DIAG=1)'


def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb; nb += 8
        while nb >= 7:
            out.append(acc & 0x7F); acc >>= 7; nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb; nb += 7
        while nb >= 8:
            out.append(acc & 0xFF); acc >>= 8; nb -= 8
    return bytes(out)


def frame(cmd, body):
    raw = bytes((0, 0x59, cmd)) + len(body).to_bytes(3, 'little') + body + bytes(((~sum(body)) & 0xFF,))
    return b'\xf0' + pack7(raw) + b'\xf7'


class Link:
    def __init__(self, port):
        import mido, queue
        self.mido, self.q = mido, queue.Queue()
        if port not in mido.get_input_names() or port not in mido.get_output_names():
            raise SystemExit('no MIDI port named exactly %r; ports: %r' % (port, mido.get_input_names()))
        self.inp = mido.open_input(port, callback=lambda m: m.type == 'sysex' and self.q.put(bytes([0xF0, *m.data, 0xF7])))
        self.out = mido.open_output(port)
        time.sleep(0.3)

    def send(self, pkt):
        self.out.send(self.mido.Message('sysex', data=pkt[1:-1]))

    def read(self, timeout):
        import queue
        try:
            return self.q.get(timeout=max(0, timeout))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        self.inp.close(); self.out.close()


def identity(link):
    for _ in range(3):
        link.drain(); link.send(HS_QUERY)
        end = time.monotonic() + 1.0
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                break
            d = unpack7(p[1:-1]) if p[:1] == b'\xf0' and p[-1:] == b'\xf7' else b''
            if len(d) == 34 and d[:3] == b'\x00\x59\x11':
                return d[6:33].rstrip(b'\0').decode('latin-1')
    return None


def command(link, cmd, body=b'', retries=1):
    """one command; None if the unit does not answer (a refused or unknown command gets no reply).
    0xB7 (input) is sent once only, so an input is never given twice."""
    for _ in range(retries):
        link.drain(); link.send(frame(cmd, body))
        end = time.monotonic() + 1.5
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                break
            if p[:1] != b'\xf0' or p[-1:] != b'\xf7':
                continue
            d = unpack7(p[1:-1])
            if (len(d) >= 7 and d[:3] == bytes((0, 0x59, cmd)) and len(d) == int.from_bytes(d[3:6], 'little') + 7
                    and d[-1] == (~sum(d[6:-1]) & 255)):
                return d[6:-1]
    return None


def where(link):
    r = command(link, CMD_WHERE, retries=2)
    if r is None or len(r) < 36 or r[:3] != b'UW\x01':
        raise SystemExit(NO_DIAG % CMD_WHERE)
    return {'page': PAGES[r[3]] if r[3] < len(PAGES) else r[3], 'page_index': r[3], 'screen': r[4], 'track': r[5],
            'save_mode': r[6], 'save_open': bool(r[6]), 'playing': r[7], 'recording': r[8], 'injector': [r[9] & 15, r[9] >> 4],
            'last_input': STATUS.get(r[10], r[10]) if r[10] else None, 'buttons_held': r[11] | r[12] << 8,
            'frame_going_out': r[13], 'title': r[14:36].rstrip(b'\0').decode('latin-1')}


def read(link):
    r = command(link, CMD_READ, retries=2)
    if r is None or len(r) < 4 or r[:3] != b'UT\x01' or len(r) != 4 + 4 * r[3]:
        raise SystemExit(NO_DIAG % CMD_READ)
    words = [int.from_bytes(r[4 + 4 * i:8 + 4 * i], 'little') for i in range(r[3])]
    return dict(zip(WORDS, words))


def reset(link):
    r = command(link, CMD_RESET, MAGIC, retries=2)
    if r != b'\x01':
        raise SystemExit(NO_DIAG % CMD_RESET)


def inject(link, kind, index, value):
    v = value & 0xFFFF if kind == 1 else value & 0xFF
    r = command(link, CMD_INPUT, MAGIC + bytes((kind, index, v & 0xFF, v >> 8)))
    if r is None or len(r) != 3:
        raise SystemExit('no reply to 0xB7 (input): not sent twice; check with "where"')
    return r[0]


def summary(c):
    secs = c['elapsed_ms'] / 1000.0 or 1e-9
    ms = lambda t: round(t / TICKS_PER_MS, 3)
    avg = lambda tot, n: round(tot / n / TICKS_PER_MS, 3) if n else None
    return {
        'secs': round(secs, 3),
        'loop_passes_per_s': round(c['passes'] / secs, 1),
        'worst_pass_ms': ms(c['pass_worst']),
        'passes_over_5ms': c['pass_over5ms'], 'passes_over_10ms': c['pass_over10ms'],
        'draw_compute_ms_per_s': round(c['draw_compute'] / TICKS_PER_MS / secs, 2),
        'draw_compute_pct': round(100.0 * c['draw_compute'] / TICKS_PER_MS / (1000.0 * secs), 2),
        'lcd_ms_per_s': round(c['lcd_ticks'] / TICKS_PER_MS / secs, 2),
        'lcd_pct': round(100.0 * c['lcd_ticks'] / TICKS_PER_MS / (1000.0 * secs), 2),
        'lcd_in_draw_ms_per_s': round(c['lcd_in_draw'] / TICKS_PER_MS / secs, 2),
        'draw_total_ms_per_s': round(c['draw_ticks'] / TICKS_PER_MS / secs, 2),
        'bytes_per_s': round(c['pixel_bytes'] / secs),
        'draw_passes_per_s': round(c['draw_passes'] / secs, 1),
        'frames_per_s': round(c['frames'] / secs, 2),
        'frames_sending_per_s': round(c['frames_sending'] / secs, 2),
        'worst_frame_ms': ms(c['frame_worst']), 'avg_frame_ms': avg(c['frame_total'], c['frames']),
        'notes': c['notes'], 'note_lag_avg_ms': avg(c['note_lag_total'], c['notes']),
        'note_lag_worst_ms': ms(c['note_lag_worst']), 'notes_over_6ms': c['notes_over6ms'],
        'notes_stale': c['notes_stale'],
        'inputs': c['inputs'], 'input_to_screen_avg_ms': avg(c['input_total'], c['inputs']),
        'input_to_screen_worst_ms': ms(c['input_worst']),
        **({} if 'region_worst' not in c else {
            'worst_region_ms': ms(c['region_worst']),
            'worst_region_at': {'page': PAGES[c['region_where'] >> 24] if (c['region_where'] >> 24) < len(PAGES)
                                else c['region_where'] >> 24, 'screen': (c['region_where'] >> 16) & 255,
                                'save_mode': (c['region_where'] >> 12) & 15, 'order': c['region_where'] & 4095},
            'draw_passes_timed_per_s': round(c['draw_passes_timed'] / secs, 1),
            'worst_draw_pass_ms': ms(c['draw_pass_worst'])}),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', default='FM-1')
    ap.add_argument('--expect', required=True, help='the unit identity, e.g. FM-1_9262330')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('read', help='counters since the last reset (raw and per second)')
    sub.add_parser('reset', help='zero the UI timing counters (not the 0x60 audio counters)')
    sub.add_parser('where', help='page, screen, track, SAVE page state, title')
    b = sub.add_parser('button', help='tap a button (or hold it with --hold MS)')
    b.add_argument('name', choices=sorted(BTN))
    b.add_argument('--hold', type=int, default=0, metavar='MS', help='hold after the press registers (0..5000)')
    b.add_argument('--no-wait', action='store_true', help='return at once (send turns or a second button meanwhile)')
    ch = sub.add_parser('chord', help='hold FIRST, tap SECOND once FIRST has registered, then FIRST lets go')
    ch.add_argument('first', choices=sorted(BTN))
    ch.add_argument('second', choices=sorted(BTN))
    ch.add_argument('--hold', type=int, default=600, metavar='MS', help='how long FIRST is held (0..5000, default 600)')
    k = sub.add_parser('key', help='press a note key (0..26) and let it go (--hold MS)')
    k.add_argument('index', type=int)
    k.add_argument('--hold', type=int, default=0, metavar='MS')
    t = sub.add_parser('turn', help='turn an encoder by signed detents')
    t.add_argument('enc', choices=sorted(ENC))
    t.add_argument('detents', type=int)
    t.add_argument('--every', type=int, metavar='MS', help='one detent every MS ms (default: all at once)')
    w = sub.add_parser('window', help='reset, wait --secs, read: one JSON line')
    w.add_argument('--secs', type=float, required=True)
    a = ap.parse_args()
    if a.cmd == 'button':
        if a.name in REFUSED_BUTTONS:
            raise SystemExit('%s is not sent by this tool (%s)' % (a.name.upper(), REFUSED_BUTTONS[a.name]))
        if not 0 <= a.hold <= 5000:
            raise SystemExit('--hold: 0..5000 ms')
        if a.name == 'save' and a.hold:
            raise SystemExit('SAVE is only tapped (it opens the SAVE page)')
    if a.cmd == 'chord':
        for n in (a.first, a.second):
            if n in REFUSED_BUTTONS and not (n == 'rec' and n == a.second):
                raise SystemExit('%s is not sent by this tool (%s)' % (n.upper(), REFUSED_BUTTONS[n]))
        if 'save' in (a.first, a.second) or a.first == a.second or not 0 < a.hold <= 5000:
            raise SystemExit('chord: two different buttons, not SAVE; --hold 1..5000 ms')
    if a.cmd == 'key' and (not 0 <= a.index <= 26 or not 0 <= a.hold <= 5000):
        raise SystemExit('key: index 0..26, --hold 0..5000 ms')
    if a.cmd == 'turn' and (a.detents == 0 or abs(a.detents) > 1000 or (a.every is not None and a.every < 1)):
        raise SystemExit('turn: detents -1000..1000, not 0; --every >= 1 ms')
    if a.cmd == 'window' and not 0 < a.secs <= 170:
        raise SystemExit('--secs: 0..170 (32-bit tick totals wrap at 178 s)')

    link = Link(a.port)
    try:
        ident = identity(link)
        if ident != a.expect:
            raise SystemExit('identity %r, expected %r: nothing else was sent' % (ident, a.expect))
        here = where(link)                    # also proves the firmware has the diagnostic
        if a.cmd == 'where':
            print(json.dumps(here))
        elif a.cmd == 'read':
            c = read(link)
            print(json.dumps({'identity': ident, 'counters': c, 'summary': summary(c), 'where': here}))
        elif a.cmd == 'reset':
            reset(link)
            print('counters reset')
        elif a.cmd == 'window':
            reset(link)
            t0 = time.time()
            time.sleep(a.secs)
            c = read(link)
            out = {'identity': ident, 'utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(t0)), 'host_secs': round(time.time() - t0, 2),
                   'page': here['page'], 'screen': here['screen'], 'track': here['track'], 'title': here['title'],
                   'save_open': here['save_open'], 'playing': here['playing'], **summary(c), 'counters': c}
            print(json.dumps(out))
        elif a.cmd == 'button':
            if here['save_open'] and a.name != 'home':
                raise SystemExit('the SAVE page is open: only HOME (and encoder turns) are sent; nothing sent')
            s = inject(link, 1, BTN[a.name], a.hold)
            if s != 1:
                raise SystemExit('%s: %s' % (a.name.upper(), STATUS.get(s, s)))
            if not a.no_wait:
                time.sleep((a.hold + 150) / 1000.0)  # the press registers, is held, and its release debounces
            after = where(link)
            print(json.dumps({'button': a.name, 'hold_ms': a.hold, 'status': STATUS[1], 'waited': not a.no_wait, 'where': after}))
        elif a.cmd == 'chord':
            if here['save_open']:
                raise SystemExit('the SAVE page is open: no chord; nothing sent')
            s = inject(link, 1, BTN[a.first], a.hold)
            if s != 1:
                raise SystemExit('%s: %s' % (a.first.upper(), STATUS.get(s, s)))
            t0 = time.time()
            while where(link)['injector'][0] != 2:            # until FIRST is held (its press registered)
                if time.time() - t0 > 0.5:
                    raise SystemExit('%s did not register: nothing more sent' % a.first.upper())
                time.sleep(0.01)
            s = inject(link, 1, BTN[a.second], 0)
            if s != 1:
                raise SystemExit('%s: %s (%s is let go at the end of its hold)' % (a.second.upper(), STATUS.get(s, s), a.first.upper()))
            time.sleep((a.hold + 150) / 1000.0)
            print(json.dumps({'chord': [a.first, a.second], 'hold_ms': a.hold, 'status': STATUS[1], 'where': where(link)}))
        elif a.cmd == 'key':
            s = inject(link, 3, a.index, a.hold)
            if s != 1:
                raise SystemExit('key %d: %s' % (a.index, STATUS.get(s, s)))
            time.sleep((a.hold + 150) / 1000.0)
            print(json.dumps({'key': a.index, 'hold_ms': a.hold, 'status': STATUS[1], 'where': where(link)}))
        elif a.cmd == 'turn':
            step = 1 if a.detents > 0 else -1
            chunks = [step] * abs(a.detents) if a.every else \
                [step * min(32, abs(a.detents) - i) for i in range(0, abs(a.detents), 32)]
            for i, d in enumerate(chunks):
                if i and a.every:
                    time.sleep(a.every / 1000.0)
                s = inject(link, 2, ENC[a.enc], d)
                if s != 1:
                    raise SystemExit('%s %+d: %s (after %d sent)' % (a.enc, d, STATUS.get(s, s), i))
            time.sleep(0.1)
            print(json.dumps({'turn': a.enc, 'detents': a.detents, 'every_ms': a.every, 'where': where(link)}))
    finally:
        link.close()


if __name__ == '__main__':
    sys.exit(main())
