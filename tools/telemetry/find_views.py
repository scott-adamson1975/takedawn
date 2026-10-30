"""The race camera's views, from a BO3_TELEMETRY recording made while pressing change-view (Y).

  py -3 tools/telemetry/find_views.py <prefix>        reads <prefix>.csv and <prefix>-view.bin
  py -3 tools/telemetry/find_views.py --selftest

For every stretch where the camera sits still relative to the car (>= 1 s), it prints where: metres
right of, above and ahead of the car's centre, and how far. A chase camera is a stretch behind the car
(negative ahead); a bumper or bonnet view is ahead of the centre and low. It also lists each Y press
(the pad's Y button as the title read it) with the camera before and after, and the slots of the player's
view object (0x7397C8, 0x1CC bytes) that take only a few values and change around a press: the camera
mode, if the title keeps one.

The car's matrix rows are (left, up, forward): row 0 points to the car's LEFT (docs/wheel-force-feedback-proposal.md).
Needs numpy.
"""
import csv
import os
import struct
import sys

import numpy as np

FPS = 60.0
STILL_S = 1.0       # seconds a camera must keep its place relative to the car to count as a view
TOL = 0.15          # metres: "keeps its place"


def load(prefix):
    rows = list(csv.DictReader(open(prefix + ".csv")))
    g = lambda k: np.array([float(r[k]) for r in rows])
    d = {k: g(k) for k in rows[0].keys()}
    return d


def local_offsets(d):
    """Camera position in the car's frame: x right, y up, z ahead."""
    left = np.stack([d["rx"], d["ry"], d["rz"]], 1)
    up = np.stack([d["ux"], d["uy"], d["uz"]], 1)
    fwd = np.stack([d["ax"], d["ay"], d["az"]], 1)
    car = np.stack([d["px"], d["py"], d["pz"]], 1)
    cam = np.stack([d["cam_px"], d["cam_py"], d["cam_pz"]], 1)
    v = cam - car
    return np.stack([-(v * left).sum(1), (v * up).sum(1), (v * fwd).sum(1)], 1)


def stretches(off, valid):
    """[(start, end)] frame ranges where the offset stays within TOL of its running mean."""
    out = []
    n = len(off)
    i = 0
    need = int(STILL_S * FPS)
    while i < n:
        if not valid[i]:
            i += 1
            continue
        j = i + 1
        total = off[i].copy()
        while j < n and valid[j] and np.linalg.norm(off[j] - total / (j - i)) < TOL:
            total += off[j]
            j += 1
        if j - i >= need:
            out.append((i, j))
        i = max(j, i + 1)
    return out


def y_presses(d):
    y = d["pad_y"] > 127
    return [i for i in range(1, len(y)) if y[i] and not y[i - 1]]


def view_slots(prefix, presses, flips):
    """Slots of the view object that take few values and change near a press."""
    p = prefix + "-view.bin"
    if not os.path.exists(p):
        return []
    raw = open(p, "rb").read()
    magic, ver, size = struct.unpack("<3I", raw[:12])
    if magic != 0x56334F42:
        return []
    rec = 4 + size
    n = (len(raw) - 12) // rec
    a = np.frombuffer(raw[12:12 + n * rec], dtype=np.uint8).reshape(n, rec)
    vf = np.frombuffer(a[:, 4:].tobytes(), dtype="<u4").reshape(n, size // 4)
    vflip = np.frombuffer(a[:, :4].tobytes(), dtype="<u4")
    # the view-record index of each press (by flip)
    near = []
    for i in presses:
        k = int(np.searchsorted(vflip, flips[i]))
        near.append(k)
    cands = []
    for s in range(size // 4):
        col = vf[:, s]
        vals = np.unique(col)
        if len(vals) < 2 or len(vals) > 8:
            continue
        changes = np.nonzero(col[1:] != col[:-1])[0] + 1
        if not len(changes):
            continue
        hit = sum(1 for c in changes if any(0 <= c - k <= 40 for k in near))
        cands.append((hit, len(changes), s * 4, vals))
    cands.sort(key=lambda t: (-t[0], t[1]))
    return cands


def report(prefix):
    d = load(prefix)
    off = local_offsets(d)
    valid = (d["cam_ptr"] != 0) & (d["phase"] == 2)
    st = stretches(off, valid)
    print("%d frames, %d with a camera" % (len(off), int(valid.sum())))
    print("\ncamera views (still relative to the car for >= %.0f s):" % STILL_S)
    print("  from-to (s)     right   up   ahead   distance")
    for a, b in st:
        m = off[a:b].mean(0)
        print("  %6.1f-%6.1f  %+6.2f %+6.2f %+7.2f  %6.2f m" % (d["race_time"][a], d["race_time"][b - 1], m[0], m[1],
                                                                  m[2], np.linalg.norm(m)))
    distinct = []
    for a, b in st:
        m = off[a:b].mean(0)
        if not any(np.linalg.norm(m - q) < 0.5 for q in distinct):
            distinct.append(m)
    print("\n%d distinct camera positions held for a second or more" % len(distinct))
    pr = y_presses(d)
    print("\n%d presses of Y:" % len(pr))
    for i in pr:
        b0, b1 = max(i - 5, 0), min(i + 45, len(off) - 1)
        print("  at %.1f s: camera %s -> %s" % (d["race_time"][i], np.round(off[b0], 1), np.round(off[b1], 1)))
    cands = view_slots(prefix, pr, d["flip"])
    if cands:
        print("\nslots of the player's view object that take few values and change near a press (offset, values):")
        for hit, ch, s, vals in cands[:12]:
            print("  +0x%03X  changes near presses %d of %d  values %s" % (s, hit, ch,
                                                                         ", ".join("0x%X" % v for v in vals[:8])))


def selftest():
    # a made-up recording: the camera 6 m behind and 2 m above the car for 3 s, then at the bumper (0, 0.6, 2.2) for
    # 3 s, with a press of Y between; the car turns, so the camera must be taken in the car's frame
    n = 7 * 60
    rows = []
    for i in range(n):
        t = i / FPS
        h = 0.3 * t
        fwd = np.array([np.sin(h), 0, np.cos(h)])
        left = np.array([-np.cos(h), 0, np.sin(h)])        # x points left of the heading
        up = np.array([0, 1.0, 0])
        car = np.array([5 * t, 0.4, 3 * t])
        if t < 3.3:
            loc = np.array([0, 2.0, -6.0])
        else:
            loc = np.array([0, 0.6, 2.2])
        cam = car + (-loc[0]) * left + loc[1] * up + loc[2] * fwd
        y = 255 if 3.0 <= t < 3.2 else 0
        rows.append(dict(flip=i, race_time=t, phase=2, rx=left[0], ry=left[1], rz=left[2], ux=up[0], uy=up[1], uz=up[2],
                         ax=fwd[0], ay=fwd[1], az=fwd[2], px=car[0], py=car[1], pz=car[2], cam_ptr=1,
                         cam_px=cam[0], cam_py=cam[1], cam_pz=cam[2], pad_y=y))
    d = {k: np.array([r[k] for r in rows], dtype=float) for k in rows[0]}
    off = local_offsets(d)
    valid = d["cam_ptr"] != 0
    st = stretches(off, valid)
    assert len(st) == 2, st
    m0, m1 = off[st[0][0]:st[0][1]].mean(0), off[st[1][0]:st[1][1]].mean(0)
    assert np.allclose(m0, [0, 2.0, -6.0], atol=0.05), m0
    assert np.allclose(m1, [0, 0.6, 2.2], atol=0.05), m1
    assert y_presses(d) == [180], y_presses(d)
    print("SELFTEST passed: two views found in the car's frame (chase and bumper) and one Y press")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        selftest()
    elif len(sys.argv) > 1:
        report(sys.argv[1])
    else:
        print(__doc__)
