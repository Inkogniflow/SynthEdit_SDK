#include <cstdio>
#include <cmath>
#include "../FormulaEngine.h"
using namespace sxws;
static int fails = 0;
static void t(const char* f, float x, const float* v, float want) {
  auto p = Program::compile(f);
  float y = p.evalOne(x, v);
  bool ok = p.ok() && std::fabs(y - want) < 1e-4f * (1 + std::fabs(want));
  if (!ok) ++fails;
  printf("%s  %-40s x=%5.2f -> %9.5f (want %9.5f) %s\n", ok ? "ok  " : "FAIL", f, x, y, want, p.error().c_str());
}
static void e(const char* f) { auto p = Program::compile(f); bool ok = !p.ok(); if (!ok) ++fails; printf("%s  error: %-30s -> '%s' @%d\n", ok?"ok  ":"FAIL", f, p.error().c_str(), p.errorPos()); }
int main() {
  float v0[4] = {0,0,0,0}, vp[4] = {1,-1,0.5f,1}, vneg[4]={-1,1,-1,-1};
  t("x", 0.3f, v0, 0.3f);
  t("A(-1.5,3.3)", 0, vneg, -1.5f);      // -5V
  t("A(-1.5,3.3)", 0, vp, 3.3f);         // +5V
  t("A(-1.5,3.3)", 0, v0, 0.9f);         // 0V
  t("a(-1.5, 3.3)", 0, v0, 0.9f);
  t("B(0,10)*x", 0.5f, vp, 0.f);         // B=-1 -> 0
  t("C(2,4)", 0, vp, 3.5f);
  t("D", 0, vp, 1.f);
  t("A(-pi,pi)", 0, vp, 3.14159265f);
  t("2x", 0.25f, v0, 0.5f);
  t("3(x+1)", 1, v0, 6);
  t("x(1-x)", 0.5f, v0, 0.25f);
  t("-x^2", 2, v0, -4);
  t("2^-1", 0, v0, 0.5f);
  t("2^3^2", 0, v0, 512);
  t("1+2*3-4/2", 0, v0, 5);
  t("tanh(x*A(1,9))", 0.5f, v0, tanhf(2.5f));
  t("if(x>0, 1, -1)", -0.3f, v0, -1);
  t("x>0 && x<1", 0.5f, v0, 1);
  t("!(x>0) || x==0", 0, v0, 1);
  t("fold(x)", 1.5f, v0, 0.5f);
  t("fold(x)", 3, v0, -1);
  t("wrap(x)", 1.25f, v0, -0.75f);
  t("cheb(3,x)", 0.5f, v0, 4*0.125f-1.5f);
  t("quant(x,4)", 0.3f, v0, 0.25f);
  t("spow(x,0.5)", -0.25f, v0, -0.5f);
  t("clamp(x,1,-1)", 3, v0, 1);
  t("lerp(0,10,x)", 0.3f, v0, 3);
  t("mod(x,1)", -0.25f, v0, 0.75f);
  t("1.5e1+.5", 0, v0, 15.5f);
  t("2e*x", 1, v0, 2*2.71828182846f);  // "2e" -> 2*e
  t("sinc(0)", 0, v0, 1);
  t("max(min(x,1),-1)", 7, v0, 1);
  t("x = 0", 0, v0, 1);
  auto p = Program::compile("A(0,1)*x + C");
  printf("varmask %u (want 5), depth %d\n", p.varMask(), p.stackDepth()); if (p.varMask()!=5) ++fails;
  auto pc = Program::compile("sin(pi/2)*2+1");
  printf("folded to %zu instr (want 1)\n", pc.code().size()); if (pc.code().size()!=1) ++fails;
  e(""); e("x+"); e("sin x"); e("foo(x)"); e("A(x,1)"); e("A(1)"); e("(x"); e("x)"); e("min(x)"); e("x $ 2"); e("clamp(x,1)");
  // block eval with vars
  float xs[300], a[300], y[300];
  for (int i=0;i<300;++i){ xs[i]=i/300.f; a[i]= (i%2)?1.f:-1.f; }
  const float* vars[4] = {a,nullptr,nullptr,nullptr};
  auto pb = Program::compile("x*A(0,2)");
  pb.eval(xs, vars, y, 300);
  bool bok=true; for(int i=0;i<300;++i) if (std::fabs(y[i]-xs[i]*((i%2)?2.f:0.f))>1e-6f) bok=false;
  printf("%s  block eval 300 samples with audio-rate A\n", bok?"ok  ":"FAIL"); if(!bok)++fails;
  for (int b=0;b<NumBuiltIns;++b){ auto s=getBuiltIn(b); for (auto& f: s->functions) if(!f.program.ok()){++fails; printf("FAIL builtin %d %s: %s\n",b,f.name.c_str(),f.program.error().c_str());} printf("builtin %d: %zu functions, mask %u\n", b, s->functions.size(), s->varMask);}
  auto l = parseFunctionList("# comment\nOne: x ; tanh(3x)\r\n\n// c\nBad: sin(");
  printf("list: %zu entries: '%s' '%s' '%s' err='%s'\n", l->functions.size(), l->functions[0].name.c_str(), l->functions[1].name.c_str(), l->functions[2].name.c_str(), l->functions[2].program.error().c_str());
  if (l->functions.size()!=3) ++fails;
  printf("\n%s (%d)\n", fails?"FAILED":"ALL PASSED", fails); return fails;
}
