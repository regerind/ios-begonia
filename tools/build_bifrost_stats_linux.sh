# Offline Bifrost v7 statistics tool (tests/bifrost_stats.c) against the host Mesa build in out/panvk-linux/host.
# Needs /tmp/shader_cc.txt: the compile command of panvk_vX_shader.c (v7) from compile_commands.json.
# Use: BIFROST_MESA_DEBUG=shaderdb [BISTAT_ALIGN16=1] /tmp/bistat frag.spv   (frag.spv from sh/qc_from_key.py)
set -e
cd ~/begonia-ios/out/panvk-linux/host
FL=$(python3 - <<'P'
import shlex
c = shlex.split(open('/tmp/shader_cc.txt').read())
print(' '.join(shlex.quote(x) for x in c if x.startswith('-I') or x.startswith('-D') or x.startswith('-std')))
P
)
LIBS=$(find . -name "*.a" | grep -v "arch-v\(4\|5\|6\|9\|10\|12\|13\|14\)\|format-v\(5\|6\|9\|10\|12\|13\|14\)\|libpanvk_v\(6\|9\|10\|12\|13\|14\)" | tr '\n' ' ')
eval cc $FL -O1 -w -I ~/begonia-ios/tests ~/begonia-ios/tests/bifrost_stats.c -o /tmp/bistat -Wl,--start-group $LIBS -Wl,--end-group -lm -lpthread -lz -ldrm -ldl -lSPIRV-Tools -lstdc++ 2>&1 | grep -v "^$" | head -40
ls -la /tmp/bistat
