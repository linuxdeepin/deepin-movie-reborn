# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

import sys, re
info = sys.argv[1]
target = sys.argv[2]  # basename to match
cur = None
covered = {}
with open(info) as f:
    for line in f:
        line=line.rstrip()
        if line.startswith("SF:"):
            cur = line[3:]
        elif line.startswith("DA:") and cur and target in cur:
            a = line[3:].split(",")
            ln = int(a[0]); cnt = int(a[1])
            covered[ln] = cnt
        elif line == "end_of_record":
            pass
# print uncovered line numbers
unc = sorted([ln for ln,c in covered.items() if c==0])
print(f"{target}: {len(unc)} uncovered / {len(covered)} instrumented")
# group into ranges
ranges=[]
for n in unc:
    if ranges and n==ranges[-1][1]+1:
        ranges[-1][1]=n
    else:
        ranges.append([n,n])
for r in ranges:
    print(f"  {r[0]}-{r[1]} ({r[1]-r[0]+1})")
