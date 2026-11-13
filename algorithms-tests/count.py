import sys, re, subprocess
exe = sys.argv[1]
dis = subprocess.run(['riscv64-unknown-elf-objdump', '-d', exe], capture_output=True, text=True).stdout
mn = {}
order = []
for line in dis.splitlines():
    m = re.match(r'\s+([0-9a-f]+):\s+[0-9a-f]+\s+(\S+)', line)
    if m:
        pc = int(m.group(1), 16); mn[pc] = m.group(2); order.append(pc)
# find nop groups
groups = []
i = 0
while i < len(order):
    if mn[order[i]] == 'nop':
        j = i
        while j < len(order) and mn[order[j]] == 'nop': j += 1
        groups.append((order[i], order[j-1], j - i)); i = j
    else: i += 1
start = [g for g in groups if g[2] == 3][0][1]
end = [g for g in groups if g[2] == 4][0][0]
subprocess.run(['qemu-riscv32', '-one-insn-per-tb', '-d', 'exec,nochain', '-D', exe + '.log', exe], capture_output=True)
on = False; n = 0; div = 0; mul = 0
with open(exe + '.log') as f:
    for line in f:
        m = re.search(r'\[[0-9a-f]+/([0-9a-f]+)/', line)
        if not m: continue
        pc = int(m.group(1), 16)
        if not on:
            if pc == start: on = True
            continue
        if pc == end: break
        n += 1
        op = mn.get(pc, '?')
        if op.startswith('div') or op.startswith('rem'): div += 1
        elif op.startswith('mul'): mul += 1
import os; os.remove(exe + '.log')
print(f"{exe}: insns={n} div/rem={div} mul={mul} est_cycles(div=18)={n + 17*div}")
