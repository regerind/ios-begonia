# Lists the buffer arguments (size, alignment, name) Metal declares for some QuartzCore shaders: default.metallib -> llvm-dis.
import re, struct, subprocess, os
lib = os.path.expanduser('~/begonia-ios/upstream/hakios-xnu-kernel/sysimage/metallibs/System/Library/Frameworks/QuartzCore.framework/default.metallib')
b = open(lib, 'rb').read()
w = [m.start() for m in re.finditer(b'\xde\xc0\x17\x0b', b)]
print('wrappers', len(w))
want = ['narrow_blur_27_frag_lph', 'single_pass_blur_3_lph', 'fixed_frag_lph_cpf', 'narrow_blur_7_frag_lph']
seen = set()
for o in w:
    magic, ver, off, size, cpu = struct.unpack_from('<5I', b, o)
    chunk = b[o:o + off + size]
    hit = [x for x in want if x.encode() in chunk and x not in seen]
    if not hit: continue
    open('/tmp/air/m.bc', 'wb').write(chunk)
    r = subprocess.run(['llvm-dis', '/tmp/air/m.bc', '-o', '/tmp/air/m.ll'], capture_output=True, text=True)
    if r.returncode:
        print(hit, 'failed:', r.stderr.strip()[-80:]); seen.update(hit); continue
    ll = open('/tmp/air/m.ll').read()
    seen.update(hit)
    bufs = re.findall(r'!"air\.buffer".*', ll)
    print(hit, len(bufs), 'buffer args')
    for l in bufs[:8]:
        g = lambda k: (re.search(r'"air\.%s", (?:i32 |!")([^,"}]*)' % k, l) or [None, None])[1]
        print('   loc', g('location_index'), 'size', g('arg_type_size'), 'align', g('arg_type_align_size'), 'name', g('arg_name'), 'type', g('arg_type_name'))
