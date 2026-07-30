#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Wrap whole never-entered function bodies with #ifndef USE_TEST.
Handles '{' on the signature line (same-line brace) by splitting at the brace char.
"""
import sys, json, re, argparse

SPECIFIERS = {'static','inline','virtual','explicit','constexpr','consteval','constinit',
    'friend','extern','mutable','register','thread_local','GLAPIENTRY','EGLAPIENTRY',
    'Q_DECL_EXPORT','Q_DECL_IMPORT','DLLEXPORT','__attribute__','__forceinline','__declspec',
    '_EXPORT','Q_REQUIRED_RESULT'}

def tokenize_sig(sig):
    return re.findall(r'[A-Za-z_][A-Za-z0-9_]*|::|\*|&|\(|\)', sig)

def is_void_return(sig):
    # destructors and constructors have no return type
    if '~' in sig.split('(')[0]:
        return True
    toks = tokenize_sig(sig)
    depth=0; name_span=None
    for i,t in enumerate(toks):
        if t=='(':
            depth+=1
            if depth==1:
                j=i-1
                while j>=0 and toks[j]=='::': j-=1
                k=j
                while k-2>=0 and toks[k-1]=='::': k-=2
                name_span=(k,j); break
        elif t==')': depth-=1
    if name_span is None: return False
    rt=[t for t in toks[:name_span[0]] if t not in SPECIFIERS and t!='::']
    return len(rt)==1 and rt[0]=='void'

def find_braces(text, start_pos):
    """Scan text (the whole file) from start_pos. Find body '{' = first '{' at paren-depth 0
       after the param list has closed. Return (open_pos, close_pos) char offsets, or None."""
    paren=0; seen_close=False; i=start_pos
    n=len(text)
    # find first '{' at paren depth 0 after params close
    open_pos=None
    st=None  # state for strings/comments
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
        if c=='"': st='str'; i+=1; continue
        if c=="'": st='char'; i+=1; continue
        if c=='(': paren+=1
        elif c==')':
            paren=max(0,paren-1)
            if paren==0: seen_close=True
        elif c=='{' and paren==0 and seen_close:
            open_pos=i; break
        i+=1
    if open_pos is None: return None
    # brace-match from open_pos
    depth=0; i=open_pos; st=None
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
        if c=='"': st='str'; i+=1; continue
        if c=="'": st='char'; i+=1; continue
        if c=='{': depth+=1
        elif c=='}':
            depth-=1
            if depth==0: return (open_pos, i)
        i+=1
    return None

def line_of(text, pos):
    return text.count("\n", 0, pos)

def process(path, start_lines, apply):
    text = open(path).read()
    lines = text.split("\n")
    # for each start line, compute char offset of line start
    # build list of edits as (abs_start, abs_end, replacement)
    edits=[]
    for sl in start_lines:
        si = sl-1
        if si<0 or si>=len(lines):
            print(f"  SKIP {path}:{sl} (out of range)"); continue
        # char offset of start of line si
        line_start=0
        for k in range(si): line_start += len(lines[k])+1
        res = find_braces(text, line_start)
        if res is None:
            print(f"  SKIP {path}:{sl} (braces not found)"); continue
        open_pos, close_pos = res
        sig = text[line_start:open_pos]
        void = is_void_return(sig)
        body = text[open_pos:close_pos+1]
        if 'R"''">' in body or 'R"(' in body or re.search(r'\bR"[^(]*\(', body):
            print(f"  SKIP {path}:{sl} (raw string in body)"); continue
        # whole-function wrap tolerates nested preprocessor (#ifdef) inside the body
        # indent: leading whitespace of the signature's first line
        indent = re.match(r'\s*', lines[si]).group()
        stub = "{ }" if void else "{ return {}; }"
        # ensure signature ends with newline (so #ifndef on fresh line)
        sig_text = sig.rstrip()
        # find trailing whitespace/newline we removed (preserve one newline)
        replacement = (sig_text + "\n"
                       f"#ifndef USE_TEST\n"
                       f"{body}"
                       f"\n#else // USE_TEST: cold function, stubbed out of test build\n"
                       f"{indent}{stub}\n"
                       f"#endif // USE_TEST")
        edits.append((line_start, close_pos+1, replacement, sl, void))
    # apply edits bottom-up (by start pos desc)
    edits.sort(key=lambda e:-e[0])
    new_text = text
    for start,end,rep,sl,void in edits:
        new_text = new_text[:start] + rep + new_text[end:]
        print(f"  WRAP {path}:{sl} {'void' if void else 'return{}'}")
    if apply:
        open(path,"w").write(new_text)
    return new_text

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('config'); ap.add_argument('--apply',action='store_true')
    a=ap.parse_args()
    cfg=json.load(open(a.config))
    for path, sls in cfg.items():
        print(f"== {path.split('/src/')[-1]}")
        process(path, sls, a.apply)

if __name__=='__main__': main()
