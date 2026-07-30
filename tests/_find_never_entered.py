# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

import sys, json
info = sys.argv[1]
files=[]
cur=None
with open(info) as f:
    for line in f:
        line=line.rstrip()
        if line.startswith("SF:"):
            cur={"sf":line[3:],"fns":[],"da":{}}; files.append(cur)
        elif line.startswith("FN:") and cur is not None:
            parts=line[3:].split(","); sl=int(parts[0]); nm=parts[-1]
            cur["fns"].append((sl,nm))
        elif line.startswith("DA:") and cur is not None:
            a=line[3:].split(","); ln=int(a[0]); cnt=int(a[1]); cur["da"][ln]=cnt
        elif line=="end_of_record": cur=None
res={}
for rec in files:
    sf=rec["sf"]
    if "/src/" not in sf: continue
    if sf.endswith(".h") or sf.endswith(".hpp"): continue
    fns=sorted(rec["fns"],key=lambda x:x[0])
    for i,(sl,nm) in enumerate(fns):
        el=fns[i+1][0]-1 if i+1<len(fns) else sl+200
        z=0; nz=0
        for ln in range(sl,el+1):
            if ln in rec["da"]:
                if rec["da"][ln]==0: z+=1
                else: nz+=1
        if z>0 and nz==0 and z>=3:
            res.setdefault(sf,[]).append({"sl":sl,"nm":nm,"z":z})
json.dump(res,sys.stdout,indent=0)
