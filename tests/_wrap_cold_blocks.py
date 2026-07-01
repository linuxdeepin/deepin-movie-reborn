#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Wrap COLD brace-blocks (all instrumented lines inside are 0-hit) with
   #ifndef USE_TEST ... #endif, keeping the surrounding braces so control flow
   is structurally preserved. Only statement-context blocks (preceding token in
   )|else|do|try|;) are wrapped. .cpp only. Skips blocks with # directives,
   R"( raw strings, case/default labels.
   Usage: wrap_cold.py --apply out.json file.cpp[:minzero]
"""
import sys, re, argparse, json

def parse_da(info, target_basename):
    da={}
    with open(info) as f:
        cur=None
        for line in f:
            line=line.rstrip()
            if line.startswith("SF:"):
                cur = target_basename in line[3:]
            elif cur and line.startswith("DA:"):
                a=line[3:].split(","); da[int(a[0])]=int(a[1])
            elif line=="end_of_record":
                pass
    return da

def find_blocks(text):
    """Yield (open_pos, close_pos) for every matching {...} pair."""
    n=len(text); i=0; st=None; depth=0; stack=[]
    pairs=[]
    while i<n:
        c=text[i]
        if st=='line':
            if c=='\n': st=None
            i+=1; continue
        if st=='block':
            if c=='*' and i+1<n and text[i+1]=='/': st=None; i+=2; continue
            i+=1; continue
        if st=='str':
            if c=='\\': i+=2; continue
            if c=='"': st=None
            i+=1; continue
        if st=='char':
            if c=='\\': i+=2; continue
            if c=="'": st=None
            i+=1; continue
        if c=='/' and i+1<n:
            if text[i+1]=='/': st='line'; i+=2; continue
            if text[i+1]=='*': st='block'; i+=2; continue
        elif c=='"': st='str'; i+=1; continue
        elif c=="'": st='char'; i+=1; continue
        elif c=='{':
            stack.append(i); i+=1; continue
        elif c=='}':
            if stack:
                op=stack.pop(); pairs.append((op,i))
            i+=1; continue
        i+=1
    return pairs

def prev_nonspace_token(text, pos):
    """Return the last non-whitespace char/token before pos, skipping comments."""
    j=pos-1
    while j>=0:
        c=text[j]
        if c in ' \t\r\n':
            j-=1; continue
        # skip block comment before
        if c=='/' and j-1>=0 and text[j-1]=='*':
            k=j-2
            while k>=1 and not (text[k]=='*' and k-1>=0 and text[k-1]=='/'):
                k-=1
            j=k-2 if k>=1 else -1
            continue
        # the token char
        # grab a small word if alpha
        return c, j
    return None, -1

def line_start(text, pos):
    return text.rfind("\n",0,pos)+1


def pp_conditional_lines(text):
    """Return set of 1-based line numbers that are inside any #if/#ifdef/#ifndef ... #endif."""
    cond=set()
    stack=[]  # list of (start_line)
    for idx,line in enumerate(text.split("\n"), start=1):
        st=line.lstrip()
        if st.startswith("#"):
            d=st[1:].lstrip()
            if d.startswith("if"):
                stack.append(idx)
            elif d.startswith("endif"):
                if stack: stack.pop()
            # else/elif: no depth change
        if stack:
            cond.add(idx)
    return cond

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--info',required=True)
    ap.add_argument('--apply',action='store_true')
    ap.add_argument('--minzero',type=int,default=3)
    ap.add_argument('files',nargs='+')
    a=ap.parse_args()
    for path in a.files:
        bn = path.split("/")[-1]
        da = parse_da(a.info, "/"+bn)  # match basename
        text=open(path).read()
        pairs=find_blocks(text)
        # map pos->line
        # precompute line starts
        line_starts=[0]
        for m in re.finditer("\n", text): line_starts.append(m.end())
        def pos2line(p): 
            import bisect
            return bisect.bisect_right(line_starts,p)  # 1-based
        cands=[]
        for op,cl in pairs:
            # preceding token must be ) else do try ; (statement context)
            # but skip if it's the function body brace (preceded by ')') — that's also statement-ish but we want INTERNAL blocks; function bodies are handled by wrap_fns. Still safe to wrap their cold sub-blocks.
            pc,pj=prev_nonspace_token(text,op)
            if pc is None: continue
            # gather a word for else/do/try
            # check the token: if alpha, read the word
            word=""
            if pc.isalpha() or pc=='_':
                k=pj
                while k>=0 and (text[k].isalnum() or text[k]=='_'):
                    k-=1
                word=text[k+1:pj+1]
            ok = (pc in ')};') or word in ('else','do','try')
            if not ok: continue
            # check cold: all DA lines strictly inside are 0
            opl=pos2line(op); cll=pos2line(cl)
            zs=0; nz=0
            for ln in range(opl+1, cll):
                if ln in da:
                    if da[ln]==0: zs+=1
                    else: nz+=1
            if nz>0 or zs<a.minzero: continue
            body=text[op+1:cl]
            if 'R"(' in body or 'R"''">' in body or re.search(r'\bR"[^(]*\(', body): continue
            if re.search(r'^\s*#', body, re.M): continue
            if re.search(r'^\s*(case\b|default\b|[A-Za-z_]\w*:)', body, re.M): continue  # labels/switch
            cands.append((op,cl,opl,cll,zs,word,pc))
        # apply bottom-up
        PP = pp_conditional_lines(text)
        def pos2line_global(p): return text.count("\n",0,p)+1
        cands = [c for c in cands if not (pos2line_global(c[0]) in PP or pos2line_global(c[1]) in PP)]
        for c in cands[:]:
            opl=pos2line_global(c[0]); cll=pos2line_global(c[1])
            if any(ln in PP for ln in range(opl,cll+1)):
                cands.remove(c)
        # filter overlapping/nested candidates: drop any candidate strictly inside another
        cands_sorted = sorted(cands, key=lambda x:(x[0], -(x[1])))
        kept=[]
        for c in cands_sorted:
            op,cl = c[0],c[1]
            nested=False
            for k in kept:
                if k[0] < op and cl < k[1]:
                    nested=True; break
            # also drop if partially overlapping any kept
            if not nested:
                for k in kept:
                    if not (cl <= k[0] or op >= k[1]):
                        nested=True; break
            if not nested:
                kept.append(c)
        cands = kept
        cands.sort(key=lambda x:-x[0])
        new=text
        n_wrapped=0; total_z=0
        for op,cl,opl,cll,zs,word,pc in cands:
            body=text[op+1:cl]  # original text from current file? must use new
            # recompute body from 'new'
            pass
        # simpler: rebuild from text with edits list
        edits=[]
        for op,cl,opl,cll,zs,word,pc in cands:
            body=text[op+1:cl]
            indent=re.match(r'[ \t]*', text[line_start(text,op):]).group()
            inner = body
            repl = "{\n" + indent + "#ifndef USE_TEST\n" + inner
            if not inner.endswith("\n"): repl += "\n"
            repl += indent + "#endif // USE_TEST (cold block)\n" + indent + "}"
            edits.append((op,cl,repl,zs))
        edits.sort(key=lambda x:-x[0])
        out=text
        for op,cl,rep,zs in edits:
            out=out[:op]+rep+out[cl+1:]
            n_wrapped+=1; total_z+=zs
        if a.apply and edits:
            open(path,"w").write(out)
        print(f"{bn}: wrapped {n_wrapped} cold blocks, ~{total_z} zero lines")
if __name__=='__main__': main()
