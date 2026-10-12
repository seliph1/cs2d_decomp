"""Compares two light engine traces line by line (ours from light_trace.exe, the original's from emu_light.py).

usage: python -I tools/agent/light_diff.py <ours.txt> <original.txt> [max shown]

Numbers match within 0.01 + 1e-4 * |value| (float rounding between the two). "call noop.*" lines (the original's
profiler hooks) are skipped. Prints the first differences with context and a summary; exit code 1 on a mismatch.
"""
import sys


def load(path):
    out = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('call noop'):
            continue
        out.append(line)
    return out


def same(a, b):
    ta, tb = a.split(), b.split()
    if len(ta) != len(tb):
        return False
    for x, y in zip(ta, tb):
        if x == y:
            continue
        try:
            fx, fy = float(x), float(y)
        except ValueError:
            return False
        if abs(fx - fy) > 0.01 + 1e-4 * max(abs(fx), abs(fy)):
            return False
    return True


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    shown, limit = 0, int(sys.argv[3]) if len(sys.argv) > 3 else 5
    bad = 0
    for i in range(max(len(a), len(b))):
        la = a[i] if i < len(a) else '<end>'
        lb = b[i] if i < len(b) else '<end>'
        if not same(la, lb):
            bad += 1
            if shown < limit:
                shown += 1
                print('--- line %d' % (i + 1))
                for k in range(max(0, i - 3), i):
                    print('    %s' % (a[k] if k < len(a) else ''))
                print('  ours: %s' % la)
                print('  orig: %s' % lb)
            if bad == 1:
                first = i
    print('lines: ours %d, original %d, mismatching %d%s' % (len(a), len(b), bad,
                                                              '' if not bad else ' (first at %d)' % (first + 1)))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
