#!/usr/bin/env python3
"""Find the car-object fields that a motion platform / force feedback need.

Reads <prefix>.bin written by BO3_TELEMETRY (src/game/telemetry.c): the raw
bytes of player 1's car object, once a frame. The position and orientation of
the car are known (the world matrix at +0x10); what is not known is where the
title keeps its velocity, angular velocity, acceleration and so on. They are
found by what they do: a field that holds the car's velocity follows the
derivative of its position. For every 4-byte slot of the car object this
tool correlates the slot, read as a float, with:

    v_world   v_local   a_world   a_local   w_world   w_local   (3 axes each)

computed from the matrix by finite differences while the race is in play,
and prints the best slots for each. A slot that matches all three axes of
a target at consecutive offsets is a vector (report: byte offset in the car).

Also: the wheels' offsets in the car's frame (suspension travel), and the
slots that take only a few values and change (flags: airborne, boost, drift).

    py -3 tools/telemetry/find_fields.py <prefix> [--min-corr 0.9]
    py -3 tools/telemetry/find_fields.py --selftest

Needs numpy.
"""
import argparse
import struct
import sys

import numpy as np

MATRIX = 0x10        # car + : the world matrix (right, up, at, pos at +0x30)
WHEELS = 0x90        # car + : four matrices of 0x40
SPEED = 0xBC
TIME = 0x10DC
PHASE = 0x134C
TARGETS = ["v_world", "v_local", "a_world", "a_local", "w_world", "w_local"]


def load(prefix):
    with open(prefix + ".bin", "rb") as f:
        data = f.read()
    magic, ver, size = struct.unpack_from("<4sII", data, 0)
    if magic != b"BO3T" or ver != 1:
        sys.exit("not a BO3_TELEMETRY .bin (magic %r version %d)" % (magic, ver))
    rec = 8 + size
    n = (len(data) - 12) // rec
    raw = np.frombuffer(data, dtype=np.uint8, count=n * rec, offset=12).reshape(n, rec)
    flips = raw[:, 0:4].copy().view("<u4").ravel()
    dump = np.ascontiguousarray(raw[:, 8:])
    return flips, dump


def slot(dump, off):
    """Column of the dump read as float32 at byte offset off."""
    return np.ascontiguousarray(dump[:, off:off + 4]).view("<f4").ravel()


def vec(dump, off):
    return np.stack([slot(dump, off + 4 * k) for k in range(3)], axis=1)


def derive(dump):
    """The targets, per frame, over the frames of a race in play with a steady step."""
    t = slot(dump, TIME).astype(np.float64)
    phase = np.ascontiguousarray(dump[:, PHASE:PHASE + 4]).view("<u4").ravel()
    pos = vec(dump, MATRIX + 0x30).astype(np.float64)
    right = vec(dump, MATRIX + 0x00).astype(np.float64)
    up = vec(dump, MATRIX + 0x10).astype(np.float64)
    at = vec(dump, MATRIX + 0x20).astype(np.float64)
    n = len(t)
    ok = np.zeros(n, bool)
    # frame i is usable when i-1, i, i+1 are in play and the clock moved by a sane step
    dt = np.diff(t)
    good = (phase[1:] == 2) & (phase[:-1] == 2) & (dt > 1e-4) & (dt < 0.1)
    ok[1:-1] = good[:-1] & good[1:]
    h = np.zeros(n)
    h[1:-1] = t[2:] - t[:-2]                       # central step (2 dt)
    h2 = np.zeros(n)
    h2[1:-1] = (t[2:] - t[:-2]) / 2                # dt for the second difference
    with np.errstate(all="ignore"):
        vw = np.zeros((n, 3))
        vw[1:-1] = (pos[2:] - pos[:-2]) / h[1:-1, None]
        aw = np.zeros((n, 3))
        aw[1:-1] = (pos[2:] - 2 * pos[1:-1] + pos[:-2]) / (h2[1:-1, None] ** 2)
        m = np.stack([right, up, at], axis=1)       # rows are the car's axes in the world
        wm = np.transpose(m, (0, 2, 1))             # columns are the axes: world <- local
        dw = np.zeros_like(wm)
        dw[1:-1] = (wm[2:] - wm[:-2]) / h[1:-1, None, None]
        k = dw @ np.transpose(wm, (0, 2, 1))        # the skew matrix of the angular velocity
        ww = np.stack([k[:, 2, 1], k[:, 0, 2], k[:, 1, 0]], axis=1)
        def local(x):
            return np.einsum("nij,nj->ni", m, x)
        out = {"v_world": vw, "v_local": local(vw), "a_world": aw, "a_local": local(aw),
               "w_world": ww, "w_local": local(ww)}
    # A teleport (a reset, a respawn) is not motion.
    jump = np.zeros(n, bool)
    sp = np.linalg.norm(vw, axis=1)
    jump |= sp > 400.0
    ok &= ~jump & np.isfinite(sp)
    return ok, out, t, phase, pos, m, np.array([slot(dump, SPEED)]).ravel()


def correlate(cols, ys, ok, lag):
    """Pearson correlation of every column against every y, with y shifted by lag frames."""
    n = cols.shape[0]
    idx = np.where(ok)[0]
    idx = idx[(idx + lag >= 0) & (idx + lag < n)]
    a = cols[idx + lag].astype(np.float64)
    b = ys[idx]
    with np.errstate(all="ignore"):
        a = a - a.mean(0)
        b = b - b.mean(0)
        sa = a.std(0)
        sb = b.std(0)
        c = (a.T @ b) / (len(idx) * np.outer(sa, sb))
        slope = (a.T @ b) / (len(idx) * (sa[:, None] ** 2))      # y per unit of the slot
    c[(sa < 1e-9)[:, None].repeat(c.shape[1], 1)] = 0
    return np.nan_to_num(c), np.nan_to_num(slope), len(idx)


def report(prefix, min_corr, dump=None, flips=None, quiet=False):
    if dump is None:
        flips, dump = load(prefix)
    nfr, size = dump.shape
    ok, tg, t, phase, pos, m, speed = derive(dump)
    use = int(ok.sum())
    print("frames %d, in play and steady %d (%.1f s)" % (nfr, use, use / 60.0))
    if use < 120:
        print("too little race to say anything: drive at least ~10 s with turns")
        return {}
    nfl = size // 4
    cols = np.ascontiguousarray(dump[:, :nfl * 4]).view("<f4").reshape(nfr, nfl)
    cols = np.where(np.isfinite(cols) & (np.abs(cols) < 1e7), cols, 0.0)
    # sanity: the speed the title keeps against the speed of the position
    sp = np.linalg.norm(tg["v_world"], axis=1)
    c = np.corrcoef(sp[ok], speed[ok])[0, 1]
    print("check: |d(position)/dt| against the speed at +0x%X: corr %.4f, ratio %.3f" %
          (SPEED, c, float(np.median(sp[ok] / np.maximum(speed[ok], 1e-6)))))
    found = {}
    for name in TARGETS:
        y = tg[name]
        best = None
        for lag in (0, 1, -1):
            cm, sl, _ = correlate(cols, y, ok, lag)
            score = np.abs(cm).max(0)
            if best is None or score.sum() > best[0].sum():
                best = (score, cm, sl, lag)
        score, cm, sl, lag = best
        print("\n== %s  (lag %+d frame)" % (name, lag))
        for ax, an in enumerate("xyz"):
            order = np.argsort(-np.abs(cm[:, ax]))[:4]
            line = ["+0x%X corr %+.3f x%.3g" % (o * 4, cm[o, ax], sl[o, ax]) for o in order]
            print("  %s: %s" % (an, "   ".join(line)))
        # A vector: each axis that varies has its best slot at base + 4*axis. The
        # axes that do not vary in this recording cannot be matched and are skipped.
        # Slots that match equally well are all kept (two slots can hold the same
        # number: a copy, or a velocity that happens to equal another): a base is
        # any offset every varying axis agrees on, and several may be listed.
        sd = y[ok].std(0)
        vary = [k for k in range(3) if sd[k] > 1e-3]
        sets = []
        for k in vary:
            a = np.abs(cm[:, k])
            sets.append({int(o) - k for o in np.where(a >= max(min_corr, a.max() - 0.01))[0]})
        agree = sorted(b for b in (set.intersection(*sets) if sets else set()) if b >= 0)
        if agree:
            shown = ", ".join("car+0x%X" % (b * 4) for b in agree[:6])
            print("  -> VECTOR at %s%s  (%d varying axes agree, corr >= %.2f)" %
                  (shown, "  (more than one: they hold the same numbers)" if len(agree) > 1 else "",
                   len(vary), min_corr))
            found[name] = [b * 4 for b in agree]
    # wheels in the car's frame: suspension travel
    print("\n== wheels in the car's frame (m): x right, y up, z forward")
    for w in range(4):
        wp = vec(dump, WHEELS + 0x40 * w + 0x30).astype(np.float64)
        rel = np.einsum("nij,nj->ni", m, wp - pos)
        r = rel[ok]
        print("  wheel %d: mean (%+.3f %+.3f %+.3f)   range y %.3f..%.3f (travel %.3f)" %
              (w, *r.mean(0), r[:, 1].min(), r[:, 1].max(), np.ptp(r[:, 1])))
    # slots that behave as flags or small states: few distinct integer values, changing
    ints = np.ascontiguousarray(dump[:, :nfl * 4]).view("<u4").reshape(nfr, nfl)
    flags = []
    for j in range(nfl):
        v = ints[ok, j]
        u = np.unique(v)
        if 2 <= len(u) <= 6 and (np.abs(np.diff(v.astype(np.int64))) > 0).sum() >= 2:
            flags.append((j * 4, u, int((np.diff(v.astype(np.int64)) != 0).sum())))
    print("\n== slots that take 2..6 values and change (flag candidates; %d)" % len(flags))
    for off, u, ch in flags[:40]:
        print("  +0x%X  values %s  changes %d" % (off, ", ".join("0x%X" % x for x in u[:6]), ch))
    return found


def selftest():
    """A made-up car with known fields: the tool must find them."""
    rng = np.random.default_rng(1)
    n, size = 1800, 0x2900
    dt = 1.0 / 60
    dump = np.zeros((n, size), np.uint8)
    t = np.arange(n) * dt
    theta = 1.2 * np.sin(0.5 * t) + 0.3 * np.sin(1.7 * t)
    speed = 25 + 10 * np.sin(0.3 * t)
    fwd = np.stack([np.sin(theta), 0 * t, np.cos(theta)], 1)
    right = np.stack([np.cos(theta), 0 * t, -np.sin(theta)], 1)
    up = np.tile([0.0, 1.0, 0.0], (n, 1))
    slip = 3.0 * np.sin(1.3 * t)                 # sideways, m/s
    vy = 2.0 * np.sin(1.1 * t)                   # vertical, m/s
    vw = fwd * speed[:, None] + right * slip[:, None] + up * vy[:, None]
    pos = np.cumsum(vw * dt, axis=0)
    wz = np.gradient(theta, dt)
    w = np.stack([0 * t, wz, 0 * t], 1)
    m = np.stack([right, up, fwd], axis=1)
    vl = np.einsum("nij,nj->ni", m, vw)
    al = np.einsum("nij,nj->ni", m, np.gradient(vw, dt, axis=0))
    f = dump.view  # noqa

    def put(off, a):
        a = np.asarray(a, "<f4").reshape(n, -1)
        dump[:, off:off + 4 * a.shape[1]] = a.view(np.uint8).reshape(n, -1)

    for k, v in enumerate((right, up, fwd, pos)):
        put(MATRIX + 0x10 * k, v)
    put(SPEED, speed)
    put(TIME, t)
    dump[:, PHASE:PHASE + 4] = np.array([2], "<u4").view(np.uint8)
    put(0x400, vw)                      # world velocity
    put(0x500, vl)                      # local velocity
    put(0x600, w)                       # angular velocity, world
    put(0x700, al)                      # local acceleration
    put(0x800, rng.normal(size=(n, 40)))           # noise
    air = (np.sin(0.7 * t) > 0.8).astype("<u4")
    dump[:, 0x900:0x904] = air.reshape(n, 1).view(np.uint8)  # a flag
    for wi in range(4):
        wp = pos + np.einsum("nji,nj->ni", m, np.array([[0.8 * (1 if wi % 2 else -1), -0.3 + 0.02 * np.sin(5 * t[0] + wi), 1.2 * (1 if wi < 2 else -1)]] * n))
        put(WHEELS + 0x40 * wi + 0x30, wp)
    found = report(None, 0.9, dump=dump, flips=np.arange(n))
    want = {"v_world": 0x400, "v_local": 0x500, "w_world": 0x600, "a_local": 0x700}
    bad = {k: (found.get(k), v) for k, v in want.items() if v not in found.get(k, [])}
    print("\nSELFTEST", "FAILED: %s" % bad if bad else "passed: all four planted vectors found")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("prefix", nargs="?")
    ap.add_argument("--min-corr", type=float, default=0.9)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        sys.exit(selftest())
    if not a.prefix:
        ap.error("a prefix (the BO3_TELEMETRY value) or --selftest")
    report(a.prefix, a.min_corr)


if __name__ == "__main__":
    main()
