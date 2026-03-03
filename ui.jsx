import { useState, useEffect, useRef, useMemo, useCallback } from "react";

/* ═══════════════════════════════ MATH ENGINE ══════════════════════════════ */

const _n  = v        => ({ t:'num', v });
const _v  = s        => ({ t:'var', name:s });
const _b  = (op,l,r) => ({ t:'bin', op, l, r });
const _f  = (nm,a)   => ({ t:'func', name:nm, args:a });
const _neg = a       => ({ t:'neg', a });

const GREEK = { alpha:'α',beta:'β',gamma:'γ',delta:'δ',epsilon:'ε',zeta:'ζ',eta:'η',theta:'θ',
  iota:'ι',kappa:'κ',lambda:'λ',mu:'μ',nu:'ν',xi:'ξ',pi:'π',rho:'ρ',sigma:'σ',tau:'τ',
  upsilon:'υ',phi:'φ',chi:'χ',psi:'ψ',omega:'ω',
  Gamma:'Γ',Delta:'Δ',Theta:'Θ',Lambda:'Λ',Xi:'Ξ',Pi:'Π',Sigma:'Σ',Phi:'Φ',Psi:'Ψ',Omega:'Ω' };
const GREEK_LATEX = Object.fromEntries(Object.entries(GREEK).map(([k,v])=>[k,`\\${k}`]));

const NUM_CONSTS = { pi:Math.PI, e:Math.E, phi:(1+Math.sqrt(5))/2, tau:2*Math.PI, inf:Infinity,
  Phi:(1+Math.sqrt(5))/2 };

function factorial(n){if(n<0||!Number.isInteger(n))return NaN;if(n===0||n===1)return 1;if(n>170)return Infinity;let r=1;for(let i=2;i<=n;i++)r*=i;return r;}
function nCr(n,k){if(k<0||k>n||!Number.isInteger(n)||!Number.isInteger(k))return 0;if(k===0||k===n)return 1;let r=1;for(let i=0;i<Math.min(k,n-k);i++){r=r*(n-i)/(i+1);}return Math.round(r);}
function nPr(n,r){if(r<0||r>n)return 0;let res=1;for(let i=0;i<r;i++)res*=(n-i);return res;}
function gcd(a,b){a=Math.abs(a);b=Math.abs(b);while(b){[a,b]=[b,a%b];}return a;}
function lcm(a,b){return a&&b?Math.abs(a*b)/gcd(a,b):0;}

// ── Tokenizer ────────────────────────────────────────────────────────────────
function tokenize(src){
  const toks=[];let i=0;
  while(i<src.length){
    if(/\s/.test(src[i])){i++;continue;}
    if(/[0-9]/.test(src[i])||(src[i]==='.'&&/[0-9]/.test(src[i+1]||''))){
      let n='';while(i<src.length&&/[0-9.]/.test(src[i]))n+=src[i++];
      toks.push({t:'NUM',v:parseFloat(n)});
    }else if(/[a-zA-Z_]/.test(src[i])){
      let id='';while(i<src.length&&/[a-zA-Z_0-9]/.test(src[i]))id+=src[i++];
      toks.push({t:'ID',v:id});
    }else{
      const c=src[i++];
      const M={'+':'PLUS','-':'MINUS','*':'STAR','/':'SLASH','^':'CARET','!':'BANG','(':'LP',')':'RP',',':'COMMA','|':'PIPE'};
      toks.push({t:M[c]||'UNK',v:c});
    }
  }
  toks.push({t:'EOF',v:''});return toks;
}

// ── Parser ───────────────────────────────────────────────────────────────────
function parseSrc(src){
  try{
    const toks=tokenize(src);let pos=0;
    const cur=()=>toks[pos];
    const eat=()=>toks[pos++];
    const expect=t=>{if(cur().t!==t)throw new Error(`Expected ${t}, got "${cur().v}"`);return eat();};
    const canImpl=()=>{const c=cur(),p=toks[pos-1];if(!p)return false;if(p.t==='RP'||p.t==='BANG')return c.t==='LP'||c.t==='ID';if(p.t==='NUM')return c.t==='LP'||c.t==='ID';return false;};
    function expr(){return add();}
    function add(){let l=mul();while(cur().t==='PLUS'||cur().t==='MINUS'){const op=eat().v;l=_b(op,l,mul());}return l;}
    function mul(){let l=unary();while(true){if(cur().t==='STAR'||cur().t==='SLASH'){const op=eat().v;l=_b(op,l,unary());}else if(canImpl()){l=_b('*',l,unary());}else break;}return l;}
    function unary(){if(cur().t==='MINUS'){eat();return _neg(unary());}if(cur().t==='PLUS'){eat();return unary();}return pow();}
    function pow(){const b=post();if(cur().t==='CARET'){eat();return _b('^',b,unary());}return b;}
    function post(){let n=primary();while(cur().t==='BANG'){eat();n=_f('fact',[n]);}return n;}
    function primary(){
      const c=cur();
      if(c.t==='NUM'){eat();return _n(c.v);}
      if(c.t==='PIPE'){eat();const e=expr();expect('PIPE');return _f('abs',[e]);}
      if(c.t==='LP'){eat();const e=expr();expect('RP');return e;}
      if(c.t==='ID'){
        eat();
        if(cur().t==='LP'){
          eat();const args=[];
          if(cur().t!=='RP'){args.push(expr());while(cur().t==='COMMA'){eat();args.push(expr());}}
          expect('RP');return _f(c.v,args);
        }
        return _v(c.v);
      }
      throw new Error(`Unexpected: "${c.v}"`);
    }
    const ast=expr();
    if(cur().t!=='EOF')throw new Error(`Unexpected: "${cur().v}"`);
    return{ok:true,ast};
  }catch(e){return{ok:false,err:e.message};}
}

// ── Helpers ──────────────────────────────────────────────────────────────────
function containsVar(ast,v){if(!ast)return false;if(ast.t==='var')return ast.name===v;if(ast.t==='num')return false;if(ast.t==='neg')return containsVar(ast.a,v);if(ast.t==='bin')return containsVar(ast.l,v)||containsVar(ast.r,v);if(ast.t==='func')return ast.args.some(a=>containsVar(a,v));return false;}
function substVar(ast,from,to){if(!ast)return ast;if(ast.t==='var')return ast.name===from?to:ast;if(ast.t==='num')return ast;if(ast.t==='neg')return{t:'neg',a:substVar(ast.a,from,to)};if(ast.t==='bin')return{t:'bin',op:ast.op,l:substVar(ast.l,from,to),r:substVar(ast.r,from,to)};if(ast.t==='func')return{t:'func',name:ast.name,args:ast.args.map(a=>substVar(a,from,to))};return ast;}
function tryEvalNum(ast){try{const v=evalAST(ast,{});return(isFinite(v)&&!isNaN(v))?v:null;}catch{return null;}}
function isConstIn(ast,v){const pts=[1.3,2.7,4.19];const vals=pts.map(p=>{try{return evalAST(ast,{vars:{[v]:p}});}catch{return NaN;}});if(vals.some(x=>isNaN(x)||!isFinite(x)))return null;if(Math.abs(vals[0]-vals[1])<1e-6&&Math.abs(vals[1]-vals[2])<1e-6)return vals[0];return null;}

// ── Simplifier ───────────────────────────────────────────────────────────────
function simplify(ast,n=8){
  function s(nd){
    if(!nd||nd.t==='num'||nd.t==='var')return nd;
    if(nd.t==='neg'){const a=s(nd.a);if(a.t==='num')return _n(-a.v);if(a.t==='neg')return a.a;return{t:'neg',a};}
    if(nd.t==='func')return{...nd,args:nd.args.map(s)};
    if(nd.t==='bin'){
      const l=s(nd.l),r=s(nd.r),{op}=nd;
      if(l.t==='num'&&r.t==='num'){
        try{const val=({'+':(a,b)=>a+b,'-':(a,b)=>a-b,'*':(a,b)=>a*b,'/':(a,b)=>a/b,'^':Math.pow}[op])(l.v,r.v);if(isFinite(val))return _n(val);}catch{}
      }
      if(op==='+'){if(l.t==='num'&&l.v===0)return r;if(r.t==='num'&&r.v===0)return l;}
      if(op==='-'){if(r.t==='num'&&r.v===0)return l;if(l.t==='num'&&l.v===0)return{t:'neg',a:r};}
      if(op==='*'){
        if((l.t==='num'&&l.v===0)||(r.t==='num'&&r.v===0))return _n(0);
        if(l.t==='num'&&l.v===1)return r;if(r.t==='num'&&r.v===1)return l;
        if(l.t==='num'&&l.v===-1)return{t:'neg',a:r};
        if(l.t==='bin'&&l.op==='*'&&l.l.t==='num'&&r.t==='num')return s(_b('*',_n(l.l.v*r.v),l.r));
      }
      if(op==='/')  {if(l.t==='num'&&l.v===0)return _n(0);if(r.t==='num'&&r.v===1)return l;}
      if(op==='^')  {if(r.t==='num'&&r.v===0)return _n(1);if(r.t==='num'&&r.v===1)return l;}
      return{t:'bin',op,l,r};
    }
    return nd;
  }
  let res=ast;for(let i=0;i<n;i++)res=s(res);return res;
}

// ── Evaluator ────────────────────────────────────────────────────────────────
function evalAST(ast,env={}){
  const vars={...NUM_CONSTS,...(env.vars||{})};
  const uFns=env.funcs||{};
  function ev(nd){
    if(!nd)throw new Error('null node');
    switch(nd.t){
      case 'num':return nd.v;
      case 'var':if(nd.name in vars)return vars[nd.name];throw new Error(`Unknown variable: ${nd.name}`);
      case 'neg':return -ev(nd.a);
      case 'bin':{
        if(nd.op==='+')return ev(nd.l)+ev(nd.r);if(nd.op==='-')return ev(nd.l)-ev(nd.r);
        if(nd.op==='*')return ev(nd.l)*ev(nd.r);if(nd.op==='/')return ev(nd.l)/ev(nd.r);
        if(nd.op==='^')return Math.pow(ev(nd.l),ev(nd.r));break;
      }
      case 'func':{
        const{name,args}=nd;
        // summation / product with index variable
        if((name==='sum'||name==='prod')&&args.length===4){
          const iv=args[0].t==='var'?args[0].name:'i';
          const fr=Math.round(ev(args[1])),to=Math.round(ev(args[2]));
          let acc=name==='sum'?0:1;
          for(let i=fr;i<=to;i++){const val=evalAST(args[3],{...env,vars:{...vars,[iv]:i}});name==='sum'?acc+=val:acc*=val;}
          return acc;
        }
        // user-defined
        if(uFns[name]){const{params,ast:body}=uFns[name];const lv={...vars};args.forEach((a,i)=>lv[params[i]]=ev(a));return evalAST(body,{...env,vars:lv});}
        const a=ev(args[0]),b=args[1]!==undefined?ev(args[1]):undefined;
        switch(name){
          case 'sin':return Math.sin(a);case 'cos':return Math.cos(a);case 'tan':return Math.tan(a);
          case 'cot':return 1/Math.tan(a);case 'sec':return 1/Math.cos(a);case 'csc':return 1/Math.sin(a);
          case 'arcsin':case 'asin':return Math.asin(a);case 'arccos':case 'acos':return Math.acos(a);case 'arctan':case 'atan':return Math.atan(a);
          case 'sinh':return Math.sinh(a);case 'cosh':return Math.cosh(a);case 'tanh':return Math.tanh(a);
          case 'arcsinh':case 'asinh':return Math.asinh(a);case 'arccosh':case 'acosh':return Math.acosh(a);case 'arctanh':case 'atanh':return Math.atanh(a);
          case 'ln':case 'log':return Math.log(a);case 'log2':return Math.log2(a);case 'log10':return Math.log10(a);
          case 'sqrt':return Math.sqrt(a);case 'cbrt':return Math.cbrt(a);
          case 'abs':return Math.abs(a);case 'exp':return Math.exp(a);
          case 'fact':return factorial(Math.round(a));
          case 'C':case 'nCr':case 'binom':return nCr(Math.round(a),Math.round(b));
          case 'P':case 'nPr':return nPr(Math.round(a),Math.round(b));
          case 'max':return Math.max(...args.map(ev));case 'min':return Math.min(...args.map(ev));
          case 'floor':return Math.floor(a);case 'ceil':return Math.ceil(a);case 'round':return Math.round(a);
          case 'mod':return a%b;case 'gcd':return gcd(Math.round(a),Math.round(b));case 'lcm':return lcm(Math.round(a),Math.round(b));
          case 'sign':return a>0?1:a<0?-1:0;
          default:throw new Error(`Unknown function: ${name}`);
        }
      }
    }
    throw new Error(`Bad node: ${nd.t}`);
  }
  return ev(ast);
}

// ── Differentiator ───────────────────────────────────────────────────────────
function diffAST(ast,v){
  function d(n){
    if(!n)return _n(0);
    switch(n.t){
      case 'num':return _n(0);
      case 'var':return _n(n.name===v?1:0);
      case 'neg':return _neg(d(n.a));
      case 'bin':{
        const{op,l,r}=n;
        if(op==='+')return _b('+',d(l),d(r));
        if(op==='-')return _b('-',d(l),d(r));
        if(op==='*')return _b('+',_b('*',d(l),r),_b('*',l,d(r)));
        if(op==='/')return _b('/',_b('-',_b('*',d(l),r),_b('*',l,d(r))),_b('^',r,_n(2)));
        if(op==='^'){
          if(!containsVar(r,v))return simplify(_b('*',_b('*',r,_b('^',l,_b('-',r,_n(1)))),d(l)));
          if(!containsVar(l,v))return simplify(_b('*',_b('*',n,_f('ln',[l])),d(r)));
          return simplify(_b('*',n,_b('+',_b('*',r,_b('/',d(l),l)),_b('*',d(r),_f('ln',[l])))));
        }
        break;
      }
      case 'func':{
        const{name,args}=n;const a=args[0],da=d(a);
        let outer;
        switch(name){
          case 'sin':outer=_f('cos',[a]);break;case 'cos':outer=_neg(_f('sin',[a]));break;
          case 'tan':outer=_b('^',_f('sec',[a]),_n(2));break;
          case 'cot':outer=_neg(_b('^',_f('csc',[a]),_n(2)));break;
          case 'sec':outer=_b('*',_f('sec',[a]),_f('tan',[a]));break;
          case 'csc':outer=_neg(_b('*',_f('csc',[a]),_f('cot',[a])));break;
          case 'arcsin':case 'asin':outer=_b('/',_n(1),_f('sqrt',[_b('-',_n(1),_b('^',a,_n(2)))]));break;
          case 'arccos':case 'acos':outer=_neg(_b('/',_n(1),_f('sqrt',[_b('-',_n(1),_b('^',a,_n(2)))])));break;
          case 'arctan':case 'atan':outer=_b('/',_n(1),_b('+',_n(1),_b('^',a,_n(2))));break;
          case 'sinh':outer=_f('cosh',[a]);break;case 'cosh':outer=_f('sinh',[a]);break;
          case 'tanh':outer=_b('-',_n(1),_b('^',_f('tanh',[a]),_n(2)));break;
          case 'arcsinh':case 'asinh':outer=_b('/',_n(1),_f('sqrt',[_b('+',_b('^',a,_n(2)),_n(1))]));break;
          case 'arccosh':case 'acosh':outer=_b('/',_n(1),_f('sqrt',[_b('-',_b('^',a,_n(2)),_n(1))]));break;
          case 'arctanh':case 'atanh':outer=_b('/',_n(1),_b('-',_n(1),_b('^',a,_n(2))));break;
          case 'ln':case 'log':outer=_b('/',_n(1),a);break;
          case 'log2':outer=_b('/',_n(1),_b('*',a,_f('ln',[_n(2)])));break;
          case 'log10':outer=_b('/',_n(1),_b('*',a,_f('ln',[_n(10)])));break;
          case 'exp':outer=_f('exp',[a]);break;
          case 'sqrt':outer=_b('/',_n(1),_b('*',_n(2),_f('sqrt',[a])));break;
          case 'abs':outer=_b('/',a,_f('abs',[a]));break;
          case 'fact':case 'C':case 'P':case 'nCr':case 'nPr':return _n(0);
          default:return _n(0);
        }
        return simplify(_b('*',outer,da));
      }
    }
    return _n(0);
  }
  return simplify(d(ast));
}

// ── Integrator ───────────────────────────────────────────────────────────────
function intAST(ast,v,depth=0){
  if(depth>7)return null;
  function liate(n){if(n.t==='func'){const nm=n.name;if(['ln','log'].includes(nm))return 0;if(['arcsin','arccos','arctan','arcsinh','arccosh','arctanh'].includes(nm))return 1;if(['sin','cos','tan','sec','csc','cot'].includes(nm))return 3;if(['exp','sinh','cosh','tanh'].includes(nm))return 4;}return 2;}
  function ti(n,dep){
    if(dep>7)return null;
    if(!containsVar(n,v))return _b('*',n,_v(v));
    if(n.t==='var'&&n.name===v)return _b('/',_b('^',_v(v),_n(2)),_n(2));
    if(n.t==='neg'){const i=ti(n.a,dep);return i?_neg(i):null;}
    if(n.t==='bin'){
      const{op,l,r}=n;
      if(op==='+'||op==='-'){const li=ti(l,dep),ri=ti(r,dep);if(li&&ri)return _b(op,li,ri);}
      if(op==='*'){
        if(!containsVar(l,v)){const i=ti(r,dep);if(i)return simplify(_b('*',l,i));}
        if(!containsVar(r,v)){const i=ti(l,dep);if(i)return simplify(_b('*',r,i));}
        const us=tryUSub(n,dep);if(us)return us;
        const bp=tryParts(n,dep);if(bp)return bp;
      }
      if(op==='/')return tryDiv(l,r,dep);
      if(op==='^'){
        if(l.t==='var'&&l.name===v&&!containsVar(r,v)){
          const nv=tryEvalNum(r);
          if(nv!==null){if(Math.abs(nv+1)<1e-9)return _f('ln',[_v(v)]);return _b('/',_b('^',_v(v),_n(nv+1)),_n(nv+1));}
          const np1=_b('+',r,_n(1));return _b('/',_b('^',_v(v),np1),np1);
        }
        if(!containsVar(l,v)){const k=isConstIn(diffAST(n.r,v),v);const lv=tryEvalNum(l);if(k&&lv&&lv>0)return simplify(_b('/',n,_n(k*Math.log(lv))));}
      }
    }
    if(n.t==='func'){
      const{name,args}=n;const a=args[0];
      if(!containsVar(a,v))return _b('*',n,_v(v));
      if(a.t==='var'&&a.name===v){
        switch(name){
          case 'sin':return _neg(_f('cos',[_v(v)]));case 'cos':return _f('sin',[_v(v)]);
          case 'tan':return _neg(_f('ln',[_f('cos',[_v(v)])]));case 'cot':return _f('ln',[_f('sin',[_v(v)])]);
          case 'sec':return _f('ln',[_b('+',_f('sec',[_v(v)]),_f('tan',[_v(v)]))]);
          case 'csc':return _f('ln',[_b('-',_f('csc',[_v(v)]),_f('cot',[_v(v)]))]);
          case 'sinh':return _f('cosh',[_v(v)]);case 'cosh':return _f('sinh',[_v(v)]);
          case 'tanh':return _f('ln',[_f('cosh',[_v(v)])]);case 'coth':return _f('ln',[_f('sinh',[_v(v)])]);
          case 'sech':return _f('arctan',[_f('sinh',[_v(v)])]);
          case 'exp':return _f('exp',[_v(v)]);
          case 'ln':case 'log':return simplify(_b('-',_b('*',_v(v),_f(name,[_v(v)])),_v(v)));
          case 'sqrt':return simplify(_b('*',_b('/',_n(2),_n(3)),_b('^',_v(v),_b('/',_n(3),_n(2)))));
          case 'arcsin':return simplify(_b('+',_b('*',_v(v),_f('arcsin',[_v(v)])),_f('sqrt',[_b('-',_n(1),_b('^',_v(v),_n(2)))])));
          case 'arccos':return simplify(_b('-',_b('*',_v(v),_f('arccos',[_v(v)])),_f('sqrt',[_b('-',_n(1),_b('^',_v(v),_n(2)))])));
          case 'arctan':return simplify(_b('-',_b('*',_v(v),_f('arctan',[_v(v)])),_b('*',_n(0.5),_f('ln',[_b('+',_n(1),_b('^',_v(v),_n(2)))]))));
          case 'arcsinh':case 'asinh':return simplify(_b('-',_b('*',_v(v),_f(name,[_v(v)])),_f('sqrt',[_b('+',_b('^',_v(v),_n(2)),_n(1))])));
          case 'arccosh':case 'acosh':return simplify(_b('-',_b('*',_v(v),_f(name,[_v(v)])),_f('sqrt',[_b('-',_b('^',_v(v),_n(2)),_n(1))])));
          case 'arctanh':case 'atanh':return simplify(_b('+',_b('*',_v(v),_f(name,[_v(v)])),_b('*',_n(0.5),_f('ln',[_b('-',_n(1),_b('^',_v(v),_n(2)))]))));
        }
      }
      const k=isConstIn(diffAST(a,v),v);
      if(k&&Math.abs(k)>1e-9){
        const dummy='__u__';const fu=ti(_f(name,[_v(dummy)]),dep+1);
        if(fu)return simplify(_b('/',substVar(fu,dummy,a),_n(k)));
      }
    }
    return null;
  }
  function tryDiv(top,bot,dep){
    if(!containsVar(bot,v))return null;
    const cv=isConstIn(top,v);if(cv===null)return null;
    const k=isConstIn(diffAST(bot,v),v);
    if(k&&Math.abs(k)>1e-9)return simplify(_b('*',_n(cv/k),_f('ln',[bot])));
    return null;
  }
  function tryUSub(n,dep){
    if(n.t!=='bin'||n.op!=='*')return null;
    const attempt=(comp,mult)=>{
      if(comp.t==='func'&&comp.args.length>=1){
        const g=comp.args[0];if(!containsVar(g,v))return null;
        const gp=diffAST(g,v);const ratio=isConstIn(_b('/',mult,gp),v);if(ratio===null)return null;
        const dummy='__u__';const Fu=ti(_f(comp.name,[_v(dummy)]),dep+1);if(!Fu)return null;
        return simplify(Math.abs(ratio-1)<1e-9?substVar(Fu,dummy,g):_b('*',_n(ratio),substVar(Fu,dummy,g)));
      }
      if(comp.t==='bin'&&comp.op==='^'){
        const g=comp.l;if(!containsVar(g,v))return null;
        const gp=diffAST(g,v);const ratio=isConstIn(_b('/',mult,gp),v);if(ratio===null)return null;
        const nv=tryEvalNum(comp.r);if(nv===null)return null;
        if(Math.abs(nv+1)<1e-9)return simplify(_b('*',_n(ratio),_f('ln',[g])));
        return simplify(_b('*',_n(ratio),_b('/',_b('^',g,_n(nv+1)),_n(nv+1))));
      }
      return null;
    };
    return attempt(n.l,n.r)||attempt(n.r,n.l);
  }
  function tryParts(n,dep){
    if(n.t!=='bin'||n.op!=='*'||dep>4)return null;
    const attempt=(u,dv)=>{
      const vi=ti(dv,dep+1);if(!vi)return null;
      const du=diffAST(u,v);
      const vdu=simplify(_b('*',vi,du));
      const ivdu=ti(vdu,dep+1);if(!ivdu)return null;
      return simplify(_b('-',_b('*',u,vi),ivdu));
    };
    const ol=liate(n.l),or=liate(n.r);
    if(ol<=or)return attempt(n.l,n.r)||attempt(n.r,n.l);
    return attempt(n.r,n.l)||attempt(n.l,n.r);
  }
  return simplify(ti(ast,depth));
}

// ── AST → LaTeX ──────────────────────────────────────────────────────────────
function toLatex(ast){
  function needsParens(n,pop,side){
    if(!n||n.t==='num'||n.t==='var'||n.t==='func')return false;
    if(n.t==='neg')return true;
    if(n.t==='bin'){const p={'+':1,'-':1,'*':2,'/':3,'^':4};const pp=p[pop]||0,cp=p[n.op]||0;if(pop==='^'&&side==='base')return cp<4;if(pop==='*')return cp<2;if(pop==='-'&&side==='right')return n.op==='+'||n.op==='-';}
    return false;
  }
  const LFNS={sin:'\\sin',cos:'\\cos',tan:'\\tan',cot:'\\cot',sec:'\\sec',csc:'\\csc',
    arcsin:'\\arcsin',arccos:'\\arccos',arctan:'\\arctan',
    arcsinh:'\\operatorname{arcsinh}',arccosh:'\\operatorname{arccosh}',arctanh:'\\operatorname{arctanh}',
    sinh:'\\sinh',cosh:'\\cosh',tanh:'\\tanh',sech:'\\operatorname{sech}',csch:'\\operatorname{csch}',
    coth:'\\coth',ln:'\\ln',log:'\\ln',exp:'\\exp',max:'\\max',min:'\\min',gcd:'\\gcd'};
  const CLAT={pi:'\\pi',e:'e',phi:'\\varphi',tau:'\\tau',inf:'\\infty',Phi:'\\Phi'};
  function L(n){
    if(!n)return '?';
    switch(n.t){
      case 'num':{if(Number.isInteger(n.v))return String(n.v);const s=n.v.toPrecision(6).replace(/\.?0+$/,'');return s;}
      case 'var':return CLAT[n.name]||(GREEK_LATEX[n.name]||n.name);
      case 'neg':{const a=L(n.a);return n.a.t==='bin'?`-\\left(${a}\\right)`:`-${a}`;}
      case 'bin':{
        const{op,l,r}=n;
        const wl=(nd,side)=>needsParens(nd,op,side)?`\\left(${L(nd)}\\right)`:L(nd);
        if(op==='/')return `\\dfrac{${L(l)}}{${L(r)}}`;
        if(op==='^')return `${wl(l,'base')}^{${L(r)}}`;
        if(op==='*')return `${wl(l,'left')} \\cdot ${wl(r,'right')}`;
        if(op==='+')return `${L(l)}+${L(r)}`;
        if(op==='-')return `${L(l)}-${wl(r,'right')}`;
        return `${L(l)}${op}${L(r)}`;
      }
      case 'func':{
        const{name,args}=n;
        if(name==='fact')return `${L(args[0])}!`;
        if(name==='C'||name==='nCr'||name==='binom')return `\\dbinom{${L(args[0])}}{${L(args[1])}}`;
        if(name==='P'||name==='nPr')return `{}^{${L(args[0])}}P_{${L(args[1])}}`;
        if(name==='sqrt')return `\\sqrt{${L(args[0])}}`;
        if(name==='cbrt')return `\\sqrt[3]{${L(args[0])}}`;
        if(name==='abs')return `\\left|${L(args[0])}\\right|`;
        if(name==='floor')return `\\lfloor ${L(args[0])} \\rfloor`;
        if(name==='ceil')return `\\lceil ${L(args[0])} \\rceil`;
        if(name==='log2')return `\\log_{2}\\!\\left(${L(args[0])}\\right)`;
        if(name==='log10')return `\\log_{10}\\!\\left(${L(args[0])}\\right)`;
        if(name==='mod')return `${L(args[0])} \\bmod ${L(args[1])}`;
        if(name==='lcm')return `\\operatorname{lcm}\\!\\left(${args.map(L).join(',')}\\right)`;
        if(name==='sign')return `\\operatorname{sgn}\\!\\left(${L(args[0])}\\right)`;
        if(name==='sum'&&args.length===4){const iv=args[0].t==='var'?L(args[0]):'i';return `\\displaystyle\\sum_{${iv}=${L(args[1])}}^{${L(args[2])}}\\left(${L(args[3])}\\right)`;}
        if(name==='prod'&&args.length===4){const iv=args[0].t==='var'?L(args[0]):'i';return `\\displaystyle\\prod_{${iv}=${L(args[1])}}^{${L(args[2])}}\\left(${L(args[3])}\\right)`;}
        if(LFNS[name])return `${LFNS[name]}\\!\\left(${args.map(L).join(', ')}\\right)`;
        return `\\operatorname{${name}}\\!\\left(${args.map(L).join(', ')}\\right)`;
      }
    }
    return '?';
  }
  return L(ast);
}

// ── User function parsing ─────────────────────────────────────────────────────
function parseFuncDef(src){
  const m=src.match(/^\s*([a-zA-Z_]\w*)\s*\(([^)]*)\)\s*=\s*(.+)$/);
  if(!m)return null;
  const name=m[1],params=m[2].split(',').map(s=>s.trim()).filter(Boolean),body=m[3].trim();
  const parsed=parseSrc(body);
  if(!parsed.ok)return{err:parsed.err};
  return{name,params,ast:parsed.ast};
}

/* ═══════════════════════════════ KATEX ════════════════════════════════════ */
function useKaTeX(){
  const[loaded,setLoaded]=useState(!!window.katex);
  useEffect(()=>{
    if(window.katex){setLoaded(true);return;}
    const lnk=document.createElement('link');lnk.rel='stylesheet';lnk.href='https://cdnjs.cloudflare.com/ajax/libs/KaTeX/0.16.9/katex.min.css';document.head.appendChild(lnk);
    const sc=document.createElement('script');sc.src='https://cdnjs.cloudflare.com/ajax/libs/KaTeX/0.16.9/katex.min.js';sc.onload=()=>setLoaded(true);document.head.appendChild(sc);
  },[]);
  return loaded;
}

function KTX({latex,display=false,cls=''}){
  const ref=useRef(null);const loaded=useKaTeX();
  useEffect(()=>{
    if(!ref.current)return;
    if(!window.katex){ref.current.textContent=latex;return;}
    try{window.katex.render(latex,ref.current,{displayMode:display,throwOnError:false,strict:false});}
    catch{if(ref.current)ref.current.textContent=latex;}
  },[latex,display,loaded]);
  return <span ref={ref} className={cls}/>;
}

/* ═══════════════════════════════ CANVAS PLOTTER ══════════════════════════ */
function Plotter({userFuncs}){
  const canvasRef=useRef(null);
  const[plotExpr,setPlotExpr]=useState('sin(x)');
  const[xMin,setXMin]=useState('-6.28');
  const[xMax,setXMax]=useState('6.28');
  const[yMin,setYMin]=useState('-2');
  const[yMax,setYMax]=useState('2');
  const[err,setErr]=useState('');

  const draw=useCallback(()=>{
    const canvas=canvasRef.current;if(!canvas)return;
    const ctx=canvas.getContext('2d');
    const W=canvas.width,H=canvas.height;
    const xlo=parseFloat(xMin),xhi=parseFloat(xMax),ylo=parseFloat(yMin),yhi=parseFloat(yMax);
    if(isNaN(xlo)||isNaN(xhi)||isNaN(ylo)||isNaN(yhi))return;
    const toX=x=>(x-xlo)/(xhi-xlo)*W;
    const toY=y=>(1-(y-ylo)/(yhi-ylo))*H;

    // background
    ctx.fillStyle='#080c18';ctx.fillRect(0,0,W,H);

    // grid
    ctx.strokeStyle='#1a2235';ctx.lineWidth=1;
    const xStep=(xhi-xlo)/10,yStep=(yhi-ylo)/8;
    for(let x=Math.ceil(xlo/xStep)*xStep;x<=xhi;x+=xStep){ctx.beginPath();ctx.moveTo(toX(x),0);ctx.lineTo(toX(x),H);ctx.stroke();}
    for(let y=Math.ceil(ylo/yStep)*yStep;y<=yhi;y+=yStep){ctx.beginPath();ctx.moveTo(0,toY(y));ctx.lineTo(W,toY(y));ctx.stroke();}

    // axes
    ctx.strokeStyle='#2a3a5a';ctx.lineWidth=1.5;
    if(xlo<=0&&0<=xhi){ctx.beginPath();ctx.moveTo(toX(0),0);ctx.lineTo(toX(0),H);ctx.stroke();}
    if(ylo<=0&&0<=yhi){ctx.beginPath();ctx.moveTo(0,toY(0));ctx.lineTo(W,toY(0));ctx.stroke();}

    // axis labels
    ctx.fillStyle='#4a5a7a';ctx.font='10px monospace';
    for(let x=Math.ceil(xlo);x<=xhi;x++){if(x===0)continue;ctx.fillText(x,toX(x)+2,toY(0)>H-12?H-3:toY(0)+12);}
    for(let y=Math.ceil(ylo);y<=yhi;y++){if(y===0)continue;ctx.fillText(y,toX(0)<4?4:toX(0)+2,toY(y)+4);}

    // plot
    const parsed=parseSrc(plotExpr);
    if(!parsed.ok){setErr(parsed.err);return;}
    setErr('');
    const N=W*2;
    const COLORS=['#f59e0b','#60a5fa','#34d399','#f87171','#c084fc'];
    ctx.strokeStyle=COLORS[0];ctx.lineWidth=2;ctx.beginPath();
    let started=false;
    for(let i=0;i<=N;i++){
      const x=xlo+(xhi-xlo)*i/N;
      let y;
      try{y=evalAST(parsed.ast,{vars:{...NUM_CONSTS,x},funcs:userFuncs});}catch{started=false;continue;}
      if(!isFinite(y)||isNaN(y)){started=false;continue;}
      const px=toX(x),py=toY(y);
      if(!started){ctx.moveTo(px,py);started=true;}else ctx.lineTo(px,py);
    }
    ctx.stroke();
  },[plotExpr,xMin,xMax,yMin,yMax,userFuncs]);

  useEffect(()=>{draw();},[draw]);

  return(
    <div style={{padding:12}}>
      <div style={{display:'flex',gap:6,marginBottom:8,flexWrap:'wrap'}}>
        <input value={plotExpr} onChange={e=>setPlotExpr(e.target.value)}
          placeholder="f(x), e.g. sin(x)*cos(2x)"
          style={{flex:1,minWidth:160,background:'#0d1120',border:'1px solid #1e2a40',borderRadius:4,padding:'6px 10px',color:'#e2e8f0',fontFamily:'inherit',fontSize:12}}/>
        <div style={{display:'flex',gap:4,alignItems:'center'}}>
          <span style={{fontSize:10,color:'#4a5a7a'}}>x:</span>
          <input value={xMin} onChange={e=>setXMin(e.target.value)} style={{width:52,...inputStyle}}/>
          <span style={{color:'#4a5a7a',fontSize:11}}>to</span>
          <input value={xMax} onChange={e=>setXMax(e.target.value)} style={{width:52,...inputStyle}}/>
        </div>
        <div style={{display:'flex',gap:4,alignItems:'center'}}>
          <span style={{fontSize:10,color:'#4a5a7a'}}>y:</span>
          <input value={yMin} onChange={e=>setYMin(e.target.value)} style={{width:52,...inputStyle}}/>
          <span style={{color:'#4a5a7a',fontSize:11}}>to</span>
          <input value={yMax} onChange={e=>setYMax(e.target.value)} style={{width:52,...inputStyle}}/>
        </div>
      </div>
      {err&&<div style={{fontSize:11,color:'#f87171',marginBottom:6}}>⚠ {err}</div>}
      <canvas ref={canvasRef} width={620} height={300}
        style={{width:'100%',height:300,borderRadius:6,border:'1px solid #1a2035',display:'block'}}/>
      <div style={{fontSize:10,color:'#2a3550',marginTop:5}}>
        tip: use x as the variable · constants: pi, e, phi, tau · functions: sin, cos, exp, ln, sqrt, abs…
      </div>
    </div>
  );
}

const inputStyle={background:'#0d1120',border:'1px solid #1e2a40',borderRadius:4,padding:'5px 6px',color:'#94a3b8',fontFamily:'inherit',fontSize:12,outline:'none'};

/* ═══════════════════════════════ SYMBOL DATA ══════════════════════════════ */
const TABS=[
  {id:'greek',label:'Gk',symbols:[
    {d:'α',s:'alpha'},{d:'β',s:'beta'},{d:'γ',s:'gamma'},{d:'δ',s:'delta'},
    {d:'ε',s:'epsilon'},{d:'ζ',s:'zeta'},{d:'θ',s:'theta'},{d:'λ',s:'lambda'},
    {d:'μ',s:'mu'},{d:'ν',s:'nu'},{d:'ξ',s:'xi'},{d:'π',s:'pi'},
    {d:'ρ',s:'rho'},{d:'σ',s:'sigma'},{d:'τ',s:'tau'},{d:'φ',s:'phi'},
    {d:'χ',s:'chi'},{d:'ψ',s:'psi'},{d:'ω',s:'omega'},
    {d:'Γ',s:'Gamma'},{d:'Δ',s:'Delta'},{d:'Θ',s:'Theta'},{d:'Λ',s:'Lambda'},
    {d:'Ξ',s:'Xi'},{d:'Π',s:'Pi'},{d:'Σ',s:'Sigma'},{d:'Φ',s:'Phi'},{d:'Ψ',s:'Psi'},{d:'Ω',s:'Omega'},
  ]},
  {id:'ops',label:'∑∏',symbols:[
    {d:'∑',s:'sum(i,1,n,',tip:'sum(i,start,end,expr)'},{d:'∏',s:'prod(i,1,n,',tip:'prod(i,start,end,expr)'},
    {d:'√',s:'sqrt('},{d:'∛',s:'cbrt('},{d:'|x|',s:'abs('},{d:'n!',s:'fact('},
    {d:'⌊⌋',s:'floor('},{d:'⌈⌉',s:'ceil('},
    {d:'nCr',s:'C(n,r)',tip:'C(n,r) — combinations'},{d:'nPr',s:'P(n,r)',tip:'P(n,r) — permutations'},
    {d:'gcd',s:'gcd('},{d:'lcm',s:'lcm('},{d:'mod',s:'mod('},
    {d:'sgn',s:'sign('},{d:'max',s:'max('},{d:'min',s:'min('},
  ]},
  {id:'trig',label:'sin',symbols:[
    {d:'sin',s:'sin('},{d:'cos',s:'cos('},{d:'tan',s:'tan('},
    {d:'cot',s:'cot('},{d:'sec',s:'sec('},{d:'csc',s:'csc('},
    {d:'sin⁻¹',s:'arcsin('},{d:'cos⁻¹',s:'arccos('},{d:'tan⁻¹',s:'arctan('},
  ]},
  {id:'hyp',label:'sinh',symbols:[
    {d:'sinh',s:'sinh('},{d:'cosh',s:'cosh('},{d:'tanh',s:'tanh('},
    {d:'sech',s:'sech('},{d:'csch',s:'csch('},{d:'coth',s:'coth('},
    {d:'sinh⁻¹',s:'arcsinh('},{d:'cosh⁻¹',s:'arccosh('},{d:'tanh⁻¹',s:'arctanh('},
  ]},
  {id:'funcs',label:'fn',symbols:[
    {d:'eˣ',s:'exp('},{d:'ln',s:'ln('},{d:'log₂',s:'log2('},{d:'log₁₀',s:'log10('},
    {d:'x²',s:'^2'},{d:'x³',s:'^3'},{d:'x^n',s:'^'},{d:'1/x',s:'1/'},
    {d:'x^½',s:'^(1/2)'},{d:'x^(1/3)',s:'^(1/3)'},
  ]},
  {id:'const',label:'π e',symbols:[
    {d:'π',s:'pi'},{d:'e',s:'e'},{d:'φ',s:'phi'},{d:'τ',s:'tau'},{d:'∞',s:'inf'},
    {d:'i',s:'i',tip:'imaginary unit'},{d:'√2',s:'sqrt(2)'},{d:'√3',s:'sqrt(3)'},
  ]},
];

/* ═══════════════════════════════ APP ══════════════════════════════════════ */
const fmtNum=v=>{if(!isFinite(v))return v>0?'∞':'-∞';if(isNaN(v))return'NaN';if(Number.isInteger(v)&&Math.abs(v)<1e12)return String(v);return v.toPrecision(10).replace(/\.?0+$/,'');};

export default function App(){
  const[input,setInput]=useState('');
  const[diffVar,setDiffVar]=useState('x');
  const[intVar,setIntVar]=useState('x');
  const[evalVarsStr,setEvalVarsStr]=useState('x=1');
  const[userFuncs,setUserFuncs]=useState({});
  const[funcInput,setFuncInput]=useState('');
  const[funcErr,setFuncErr]=useState('');
  const[history,setHistory]=useState([]);
  const[symTab,setSymTab]=useState('greek');
  const[activePanel,setActivePanel]=useState('calc'); // 'calc' | 'plot'
  const[histFilter,setHistFilter]=useState('all'); // 'all' | 'eval' | 'diff' | 'int'
  const inputRef=useRef(null);
  const katexLoaded=useKaTeX();

  const parsed=useMemo(()=>{if(!input.trim())return null;return parseSrc(input.trim());},[input]);
  const inputLatex=useMemo(()=>{if(!parsed||!parsed.ok)return null;return toLatex(parsed.ast);},[parsed]);

  function insertAt(text){
    const el=inputRef.current;if(!el)return;
    const s=el.selectionStart,e=el.selectionEnd;
    const nv=input.slice(0,s)+text+input.slice(e);
    setInput(nv);
    setTimeout(()=>{el.selectionStart=el.selectionEnd=s+text.length;el.focus();},0);
  }

  function parseEvalVars(){
    const vars={};
    evalVarsStr.split(',').forEach(part=>{
      const m=part.trim().match(/^([a-zA-Z_]\w*)\s*=\s*(.+)$/);
      if(m){const parsed=parseSrc(m[2].trim());if(parsed.ok){try{vars[m[1]]=evalAST(parsed.ast,{});}catch{}}}
    });
    return vars;
  }

  function addEntry(op,inputLx,resultLx,numeric=null){
    setHistory(h=>[{op,inputLx,resultLx,numeric,id:Date.now()},...h.slice(0,79)]);
  }

  function doEval(){
    if(!parsed||!parsed.ok)return;
    const vars=parseEvalVars();
    try{
      const val=evalAST(parsed.ast,{vars:{...NUM_CONSTS,...vars},funcs:userFuncs});
      addEntry('eval',inputLatex,`=${fmtNum(val)}`,val);
    }catch(e){addEntry('eval',inputLatex,`\\text{Error: ${e.message.replace(/[{}]/g,'')}}`);}
  }

  function doDiff(){
    if(!parsed||!parsed.ok)return;
    try{
      const res=diffAST(parsed.ast,diffVar);
      const rlx=toLatex(res);
      const dv=GREEK_LATEX[diffVar]||diffVar;
      addEntry('diff',`\\dfrac{d}{d${dv}}\\!\\left(${inputLatex}\\right)`,rlx);
    }catch(e){addEntry('diff',inputLatex,`\\text{Error: ${e.message.replace(/[{}]/g,'')}}`);}
  }

  function doInt(){
    if(!parsed||!parsed.ok)return;
    try{
      const res=intAST(parsed.ast,intVar);
      const iv=GREEK_LATEX[intVar]||intVar;
      const rlx=res?toLatex(res)+'\\;+\\;C':'\\text{no closed form found}';
      addEntry('int',`\\displaystyle\\int ${inputLatex}\\,d${iv}`,rlx);
    }catch(e){addEntry('int',inputLatex,`\\text{Error: ${e.message.replace(/[{}]/g,'')}}`);}
  }

  function doDefFunc(){
    setFuncErr('');
    const r=parseFuncDef(funcInput);
    if(!r){setFuncErr('Format: f(x) = expr  or  a(x,y,z) = x^2+y^2+z');return;}
    if(r.err){setFuncErr(r.err);return;}
    setUserFuncs(u=>({...u,[r.name]:{params:r.params,ast:r.ast}}));
    setFuncInput('');
  }

  // keyboard shortcut: Enter in input → eval, shift+Enter → newline
  function handleInputKey(e){
    if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();doEval();}
  }

  function copyLatex(latex){
    navigator.clipboard?.writeText(latex).catch(()=>{});
  }

  const filteredHistory=histFilter==='all'?history:history.filter(e=>e.op===histFilter);

  const opColor={eval:'#60a5fa',diff:'#34d399',int:'#c084fc'};
  const opBg={eval:'#0d1e30',diff:'#0d1e18',int:'#180d28'};
  const opLabel={eval:'EVAL',diff:'DIFF',int:'INT'};

  return(
    <div style={{minHeight:'100vh',background:'#060910',color:'#dde1ed',fontFamily:'"JetBrains Mono","Fira Code",Consolas,monospace',display:'flex',flexDirection:'column'}}>
      <style>{`
        @import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@300;400;500;600&display=swap');
        *{box-sizing:border-box;margin:0;padding:0;}
        ::-webkit-scrollbar{width:4px;height:4px}::-webkit-scrollbar-track{background:#0a0e1a}::-webkit-scrollbar-thumb{background:#1e2c42;border-radius:2px}
        .katex{font-size:1.05em!important;}.katex-display{margin:0!important;}
        .sym{transition:all .1s;}.sym:hover{background:#1a2845!important;color:#f59e0b!important;transform:scale(1.1);}
        .sym:active{transform:scale(.93);}
        .abtn{transition:all .15s;}.abtn:hover{filter:brightness(1.2);transform:translateY(-1px);}.abtn:active{transform:translateY(1px);}
        .tab{transition:all .15s;border-bottom:2px solid transparent;}
        .tab.on{color:#f59e0b;border-bottom-color:#f59e0b;}
        .tab:not(.on):hover{color:#7a8fad;}
        .hcard{animation:fsin .22s ease;}.hcard:hover .delbtn{opacity:1!important;}
        @keyframes fsin{from{opacity:0;transform:translateY(-6px)}to{opacity:1;transform:none}}
        input,textarea{outline:none;}textarea:focus,input:focus{border-color:#f59e0b!important;}
        .iarea:focus-within{border-color:#f59e0b!important;box-shadow:0 0 0 2px rgba(245,158,11,.1)!important;}
        .ftab{transition:all .12s;border-radius:3px;}.ftab.on{background:#1a2845;color:#f59e0b;}.ftab:not(.on):hover{background:#111828;color:#7a8fad;}
        .copyb{opacity:0;transition:opacity .15s;}.hcard:hover .copyb{opacity:1;}
      `}</style>

      {/* ── Header ───────────────────────────────────────────────────────── */}
      <header style={{background:'#080b16',borderBottom:'1px solid #141c2e',padding:'8px 20px',display:'flex',alignItems:'center',gap:14,userSelect:'none'}}>
        <div style={{display:'flex',alignItems:'baseline',gap:6}}>
          <span style={{color:'#f59e0b',fontWeight:700,fontSize:17,letterSpacing:'-0.5px'}}>SYM</span>
          <span style={{color:'#4a5a7a',fontWeight:300,fontSize:17}}>CALC</span>
        </div>
        <div style={{width:1,height:18,background:'#1a2035'}}/>
        <span style={{fontSize:10,color:'#2e3f5a',letterSpacing:'.06em'}}>SYMBOLIC MATH ENGINE</span>
        <div style={{marginLeft:'auto',display:'flex',gap:6}}>
          {['calc','plot'].map(p=>(
            <button key={p} className={`abtn ftab${activePanel===p?' on':''}`}
              onClick={()=>setActivePanel(p)}
              style={{background:'none',border:'none',cursor:'pointer',padding:'4px 12px',fontSize:11,color:activePanel===p?'#f59e0b':'#3d4f6e',fontFamily:'inherit',letterSpacing:'.04em'}}>
              {p==='calc'?'CALCULATOR':'PLOTTER'}
            </button>
          ))}
        </div>
        {!katexLoaded&&<span style={{fontSize:10,color:'#f59e0b',marginLeft:8}}>loading KaTeX…</span>}
      </header>

      {/* ── Main ─────────────────────────────────────────────────────────── */}
      <div style={{flex:1,display:'grid',gridTemplateColumns:'360px 1fr',overflow:'hidden',height:'calc(100vh - 41px)'}}>

        {/* ── LEFT PANEL ─────────────────────────────────────────────────── */}
        <div style={{borderRight:'1px solid #141c2e',display:'flex',flexDirection:'column',overflow:'hidden',background:'#070a14'}}>

          {activePanel==='calc'&&<>
            {/* Symbol palette */}
            <div style={{borderBottom:'1px solid #141c2e',background:'#080b16',padding:'6px 10px 0'}}>
              <div style={{display:'flex',gap:1,marginBottom:5}}>
                {TABS.map(tb=>(
                  <button key={tb.id} className={`tab${symTab===tb.id?' on':''}`}
                    onClick={()=>setSymTab(tb.id)}
                    style={{background:'none',border:'none',cursor:'pointer',padding:'3px 8px',fontSize:10,color:symTab===tb.id?'#f59e0b':'#3d4f6e',fontFamily:'inherit',letterSpacing:'.04em'}}>
                    {tb.label}
                  </button>
                ))}
              </div>
              <div style={{display:'flex',flexWrap:'wrap',gap:3,paddingBottom:7}}>
                {TABS.find(t=>t.id===symTab)?.symbols.map((sym,i)=>(
                  <button key={i} className="sym" title={sym.tip||sym.s}
                    onClick={()=>insertAt(sym.s)}
                    style={{background:'#0e1520',border:'1px solid #1a2438',borderRadius:4,padding:'3px 7px',cursor:'pointer',fontSize:12,color:'#6a80a0',fontFamily:'inherit',minWidth:34}}>
                    {sym.d}
                  </button>
                ))}
              </div>
            </div>

            {/* Input area */}
            <div style={{padding:10,flex:'0 0 auto'}}>
              <div className="iarea" style={{background:'#0c1020',border:'1px solid #172035',borderRadius:6,overflow:'hidden'}}>
                <textarea ref={inputRef} value={input}
                  onChange={e=>setInput(e.target.value)}
                  onKeyDown={handleInputKey}
                  placeholder="enter expression…  e.g.  sin(x)^2 + C(8,3) + sum(i,1,10,i^2)"
                  style={{width:'100%',background:'transparent',border:'none',color:'#e2e8f0',fontFamily:'inherit',fontSize:12.5,padding:'9px 11px',resize:'none',height:70,lineHeight:1.65}}/>
                {inputLatex&&(
                  <div style={{borderTop:'1px solid #141c2e',padding:'7px 12px',minHeight:40,display:'flex',alignItems:'center',background:'#090d1a'}}>
                    <KTX latex={inputLatex} display={false}/>
                  </div>
                )}
                {parsed&&!parsed.ok&&input.trim()&&(
                  <div style={{borderTop:'1px solid #2a1a1a',padding:'4px 10px',fontSize:10.5,color:'#f87171'}}>⚠ {parsed.err}</div>
                )}
              </div>
            </div>

            {/* Action buttons */}
            <div style={{padding:'0 10px 8px',display:'flex',gap:6,flexWrap:'wrap',alignItems:'center'}}>
              <button className="abtn" onClick={doEval} disabled={!parsed?.ok}
                style={{background:parsed?.ok?'#122030':'#0d1520',border:'1px solid #1e3650',borderRadius:5,padding:'7px 14px',cursor:'pointer',color:parsed?.ok?'#60a5fa':'#2a3a4a',fontFamily:'inherit',fontSize:11.5}}>
                = Eval
              </button>
              <div style={{display:'flex',gap:3,alignItems:'center'}}>
                <button className="abtn" onClick={doDiff} disabled={!parsed?.ok}
                  style={{background:parsed?.ok?'#0e2018':'#0d1520',border:'1px solid #1e4030',borderRadius:5,padding:'7px 12px',cursor:'pointer',color:parsed?.ok?'#34d399':'#2a3a2a',fontFamily:'inherit',fontSize:11.5}}>
                  d/d
                </button>
                <input value={diffVar} onChange={e=>setDiffVar(e.target.value)} maxLength={8}
                  style={{width:32,...inputStyle,color:'#f59e0b',textAlign:'center'}}/>
              </div>
              <div style={{display:'flex',gap:3,alignItems:'center'}}>
                <button className="abtn" onClick={doInt} disabled={!parsed?.ok}
                  style={{background:parsed?.ok?'#180e28':'#0d1520',border:'1px solid #35204a',borderRadius:5,padding:'7px 12px',cursor:'pointer',color:parsed?.ok?'#c084fc':'#2a1a3a',fontFamily:'inherit',fontSize:11.5}}>
                  ∫ d
                </button>
                <input value={intVar} onChange={e=>setIntVar(e.target.value)} maxLength={8}
                  style={{width:32,...inputStyle,color:'#f59e0b',textAlign:'center'}}/>
              </div>
              {input&&<button className="abtn" onClick={()=>setInput('')}
                style={{background:'none',border:'none',cursor:'pointer',color:'#2a3a50',fontFamily:'inherit',fontSize:11,marginLeft:'auto'}}>
                clear
              </button>}
            </div>

            {/* Eval vars */}
            <div style={{padding:'0 10px 8px',display:'flex',gap:6,alignItems:'center'}}>
              <span style={{fontSize:10,color:'#2e3f5a',whiteSpace:'nowrap'}}>vars:</span>
              <input value={evalVarsStr} onChange={e=>setEvalVarsStr(e.target.value)} placeholder="x=1, y=2, n=10"
                style={{flex:1,...inputStyle,color:'#94a3b8'}}/>
            </div>

            {/* Function registry */}
            <div style={{borderTop:'1px solid #141c2e',flex:1,overflow:'auto',padding:10}}>
              <div style={{fontSize:9.5,color:'#2e3f5a',letterSpacing:'.07em',marginBottom:7}}>FUNCTION REGISTRY</div>
              <div style={{display:'flex',gap:5,marginBottom:6}}>
                <input value={funcInput} onChange={e=>setFuncInput(e.target.value)}
                  onKeyDown={e=>e.key==='Enter'&&doDefFunc()}
                  placeholder="f(x) = sin(x)·x²   or   a(x,y,z) = x²+y²+z"
                  style={{flex:1,...inputStyle,color:'#e2e8f0'}}/>
                <button className="abtn" onClick={doDefFunc}
                  style={{background:'#102030',border:'1px solid #1e3a55',borderRadius:4,padding:'5px 10px',cursor:'pointer',color:'#60a5fa',fontFamily:'inherit',fontSize:11}}>
                  Define
                </button>
              </div>
              {funcErr&&<div style={{fontSize:10.5,color:'#f87171',marginBottom:5}}>⚠ {funcErr}</div>}
              {Object.keys(userFuncs).length===0&&(
                <div style={{fontSize:10.5,color:'#1e2c3a',fontStyle:'italic',padding:'4px 0'}}>no functions defined yet</div>
              )}
              {Object.entries(userFuncs).map(([name,{params,ast}])=>{
                const lx=`${name}\\!\\left(${params.map(p=>GREEK_LATEX[p]||p).join(',\\,')}\\right)=${toLatex(ast)}`;
                return(
                  <div key={name} className="hcard" style={{display:'flex',alignItems:'center',gap:7,padding:'5px 9px',background:'#0c1520',borderRadius:4,marginBottom:4,border:'1px solid #172035',overflow:'hidden'}}>
                    <KTX latex={lx} display={false}/>
                    <div style={{marginLeft:'auto',display:'flex',gap:4}}>
                      <button className="copyb" onClick={()=>copyLatex(lx)}
                        style={{background:'none',border:'none',cursor:'pointer',color:'#2a3a55',fontSize:12}} title="Copy LaTeX">⎘</button>
                      <button className="delbtn" onClick={()=>setUserFuncs(u=>{const n={...u};delete n[name];return n;})}
                        style={{background:'none',border:'none',cursor:'pointer',color:'#2a3a55',fontSize:14,opacity:0}} title="Remove">×</button>
                    </div>
                  </div>
                );
              })}
              <div style={{marginTop:12,padding:'8px 0',borderTop:'1px solid #111a28'}}>
                <div style={{fontSize:9.5,color:'#2e3f5a',letterSpacing:'.07em',marginBottom:5}}>QUICK REFERENCE</div>
                <div style={{fontSize:10,color:'#2a3850',lineHeight:1.9}}>
                  <div><span style={{color:'#3d5272'}}>sum(i,a,b,f(i))</span> — Σ summation</div>
                  <div><span style={{color:'#3d5272'}}>prod(i,a,b,f(i))</span> — Π product</div>
                  <div><span style={{color:'#3d5272'}}>C(n,k)</span> — binomial ·&nbsp;<span style={{color:'#3d5272'}}>P(n,k)</span> — permutations</div>
                  <div><span style={{color:'#3d5272'}}>fact(n)</span> — n! &nbsp;·&nbsp; <span style={{color:'#3d5272'}}>abs(x)</span> — |x|</div>
                  <div><span style={{color:'#3d5272'}}>pi e phi tau inf</span> — constants</div>
                  <div style={{marginTop:3,color:'#273445'}}>press <span style={{color:'#3d5272'}}>Enter</span> to evaluate</div>
                </div>
              </div>
            </div>
          </>}

          {activePanel==='plot'&&(
            <div style={{flex:1,overflow:'auto'}}>
              <Plotter userFuncs={userFuncs}/>
            </div>
          )}
        </div>

        {/* ── RIGHT PANEL ────────────────────────────────────────────────── */}
        <div style={{display:'flex',flexDirection:'column',overflow:'hidden',background:'#060910'}}>
          {/* History filter bar */}
          <div style={{padding:'7px 14px',borderBottom:'1px solid #141c2e',background:'#080b16',display:'flex',alignItems:'center',gap:8}}>
            <span style={{fontSize:9.5,color:'#2e3f5a',letterSpacing:'.07em',marginRight:4}}>OUTPUT</span>
            {['all','eval','diff','int'].map(f=>(
              <button key={f} className={`abtn ftab${histFilter===f?' on':''}`}
                onClick={()=>setHistFilter(f)}
                style={{background:'none',border:'none',cursor:'pointer',padding:'3px 9px',fontSize:10,color:histFilter===f?'#f59e0b':'#2e3f5a',fontFamily:'inherit',letterSpacing:'.04em'}}>
                {f==='all'?'ALL':f.toUpperCase()}
              </button>
            ))}
            {history.length>0&&(
              <button onClick={()=>setHistory([])} className="abtn"
                style={{marginLeft:'auto',background:'none',border:'none',cursor:'pointer',color:'#2e3f5a',fontSize:10,fontFamily:'inherit'}}>
                clear all
              </button>
            )}
          </div>

          <div style={{flex:1,overflow:'auto',padding:'10px 12px',display:'flex',flexDirection:'column',gap:7}}>
            {filteredHistory.length===0&&(
              <div style={{flex:1,display:'flex',flexDirection:'column',alignItems:'center',justifyContent:'center',gap:14,color:'#131c28',pointerEvents:'none'}}>
                <div style={{fontSize:52,opacity:.25,lineHeight:1}}>∑</div>
                <div style={{fontSize:11,letterSpacing:'.08em',color:'#1a2535'}}>results appear here</div>
                <div style={{fontSize:10,color:'#111b28',textAlign:'center',lineHeight:2,maxWidth:260}}>
                  <div>sin(x)^2 + cos(x)^2</div>
                  <div>sum(i, 1, 100, i^2)</div>
                  <div>C(52, 5) * P(4, 2)</div>
                  <div>fact(10) / fact(5)</div>
                  <div>f(x) = arctan(x^2 + 1)</div>
                </div>
              </div>
            )}
            {filteredHistory.map(entry=>(
              <div key={entry.id} className="hcard" style={{background:opBg[entry.op],border:`1px solid ${entry.op==='eval'?'#122030':entry.op==='diff'?'#0e2a1a':'#1a0e2a'}`,borderRadius:7,overflow:'hidden'}}>
                <div style={{padding:'5px 10px',borderBottom:'1px solid #0d1525',display:'flex',alignItems:'center',gap:7}}>
                  <span style={{fontSize:9,padding:'2px 7px',borderRadius:2,fontWeight:600,letterSpacing:'.06em',color:opColor[entry.op],background:`${opColor[entry.op]}18`}}>
                    {opLabel[entry.op]}
                  </span>
                  <div style={{marginLeft:'auto',display:'flex',gap:6,alignItems:'center'}}>
                    <button className="copyb" onClick={()=>copyLatex(entry.resultLx)}
                      title="Copy LaTeX" style={{background:'none',border:'none',cursor:'pointer',color:'#2a3a55',fontSize:13}}>⎘</button>
                    <button className="delbtn" onClick={()=>setHistory(h=>h.filter(e=>e.id!==entry.id))}
                      style={{background:'none',border:'none',cursor:'pointer',color:'#2a3a55',fontSize:15,opacity:0}}>×</button>
                  </div>
                </div>
                <div style={{padding:'6px 12px 3px',color:'#3d5070',fontSize:11.5,borderBottom:'1px solid #0d1525',overflow:'auto'}}>
                  <KTX latex={entry.inputLx} display={false}/>
                </div>
                <div style={{padding:'10px 14px',overflowX:'auto',display:'flex',alignItems:'center',gap:12}}>
                  <KTX latex={entry.resultLx} display={entry.op!=='eval'}/>
                  {entry.numeric!==null&&entry.numeric!==undefined&&(
                    <span style={{fontSize:10.5,color:'#2e4060',whiteSpace:'nowrap'}}>≈ {fmtNum(entry.numeric)}</span>
                  )}
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}