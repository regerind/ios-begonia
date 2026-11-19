# finds where a chain of return addresses (known only up to the load slide) lies in a binary: every return address
# follows a bl/blr, so the chain is a set of call sites with fixed distances.   chain.py BL-LINES f1 f2 f3 ...
import re, sys
calls = {}
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'\s*([0-9a-f]+):\s+(blr?a?[abz]*)\s+(.*)', line)
    if m:
        calls[int(m.group(1), 16)] = m.group(2) + ' ' + m.group(3).strip()
frames = [int(x, 16) for x in sys.argv[2:]]
first = frames[0]
found = 0
for site in calls:
    if (site + 4 - first) % 0x4000:
        continue
    slide = first - (site + 4)
    if all((f - slide - 4) in calls for f in frames):
        found += 1
        print('slide %#x:' % slide)
        for f in frames:
            print('   %#x -> %#x: %s' % (f, f - slide - 4, calls[f - slide - 4][:150]))
print(found, 'placements;', len(calls), 'call sites')
