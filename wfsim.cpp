// wfsim.cpp — поведенческое сходство двух воркфлоу.
// Сборка:  g++ -O2 -std=c++17 -pthread wfsim.cpp -o wfsim
// Запуск:  ./wfsim A.json B.json [--tests 5000] [--len 8] [--seed 1] [--threads N]
//
// Идея: генерируем случайные последовательности ВНЕШНИХ событий с аргументами,
// прогоняем обе воркфлоу в одном и том же симуляторе и сравниваем НАБЛЮДАЕМЫЕ действия
// на каждом шаге. Любое неизвестное action = наблюдаемое (имя + разрешённые аргументы).
#include <bits/stdc++.h>
#include <thread>
using namespace std;
typedef uint64_t u64;

// ---------------------------------------------------------------- JSON
struct J { enum T { N, B, NUM, S, A, O } t = N; bool b = false; double n = 0; string s; vector<J> a; vector<pair<string, J>> o;
  const J* get(const string& k) const { for (auto& p : o) if (p.first == k) return &p.second; return nullptr; } };
struct P {
  const char *p, *e;
  void ws() { while (p < e && (unsigned char)*p <= ' ') p++; }
  string str() {
    string r; p++;
    while (p < e && *p != '"') {
      if (*p == '\\') { p++; char c = *p++;
        if (c == 'n') r += '\n'; else if (c == 't') r += '\t'; else if (c == 'r') r += '\r';
        else if (c == 'b') r += '\b'; else if (c == 'f') r += '\f';
        else if (c == 'u') { unsigned u = strtoul(string(p, 4).c_str(), 0, 16); p += 4;
          if (u < 0x80) r += (char)u; else if (u < 0x800) { r += (char)(0xC0 | u >> 6); r += (char)(0x80 | (u & 63)); }
          else { r += (char)(0xE0 | u >> 12); r += (char)(0x80 | ((u >> 6) & 63)); r += (char)(0x80 | (u & 63)); } }
        else r += c;
      } else r += *p++;
    }
    p++; return r;
  }
  J val() {
    ws(); J j;
    if (*p == '{') { p++; j.t = J::O; ws(); if (*p == '}') { p++; return j; }
      for (;;) { ws(); string k = str(); ws(); p++; J v = val(); j.o.emplace_back(k, move(v)); ws(); if (*p == ',') { p++; continue; } p++; break; } }
    else if (*p == '[') { p++; j.t = J::A; ws(); if (*p == ']') { p++; return j; }
      for (;;) { j.a.push_back(val()); ws(); if (*p == ',') { p++; continue; } p++; break; } }
    else if (*p == '"') { j.t = J::S; j.s = str(); }
    else if (!strncmp(p, "true", 4)) { j.t = J::B; j.b = true; p += 4; }
    else if (!strncmp(p, "false", 5)) { j.t = J::B; p += 5; }
    else if (!strncmp(p, "null", 4)) { p += 4; }
    else { char* en; j.t = J::NUM; j.n = strtod(p, &en); p = en; }
    return j;
  }
};
static J parse(const string& s) { P p{s.data(), s.data() + s.size()}; return p.val(); }
static string dump(const J& j) {
  switch (j.t) { case J::N: return "null"; case J::B: return j.b ? "true" : "false"; case J::NUM: return to_string(j.n);
    case J::S: return "\"" + j.s + "\"";
    case J::A: { string r = "["; for (auto& x : j.a) r += dump(x) + ","; return r + "]"; }
    default: { string r = "{"; for (auto& x : j.o) r += x.first + ":" + dump(x.second) + ","; return r + "}"; } }
}

// ---------------------------------------------------------------- values
struct Intern { unordered_map<string, int> m; vector<string> v;
  int get(const string& s) { auto it = m.find(s); if (it != m.end()) return it->second; int i = v.size(); m.emplace(s, i); v.push_back(s); return i; } };
static Intern STR, KEY;  // строки/устройства/события и ключи state/flow
enum { TN, TB, TNUM, TS, TOP };
struct V { uint8_t t = 0; u64 x = 0; bool operator==(const V& o) const { return t == o.t && x == o.x; } };
static V vb(bool b) { return {TB, (u64)b}; }
static V vs(const string& s) { return {TS, (u64)STR.get(s)}; }
static u64 mix(u64 h, u64 x) { h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); h *= 0xff51afd7ed558ccdULL; h ^= h >> 32; return h; }
static u64 vh(V v) { return mix(v.t + 1, v.x); }
static u64 SEED = 1;
static bool truthy(V v, u64 seed) {
  switch (v.t) { case TN: return false; case TB: return v.x; case TNUM: { double d; memcpy(&d, &v.x, 8); return d != 0; }
    case TS: return !STR.v[v.x].empty(); default: return mix(v.x, seed) & 1; }  // неизвестный результат: псевдослучайный, но согласованный в A и B
}
static V toV(const J& j) {
  switch (j.t) { case J::N: return V{}; case J::B: return vb(j.b);
    case J::NUM: { double d = j.n; if (d == 0) d = 0; u64 x; memcpy(&x, &d, 8); return {TNUM, x}; }
    case J::S: return vs(j.s); default: return vs(dump(j)); }
}
static string vstr(V v) {
  switch (v.t) { case TN: return "null"; case TB: return v.x ? "true" : "false";
    case TNUM: { double d; memcpy(&d, &v.x, 8); return to_string(d); } case TS: return "\"" + STR.v[v.x] + "\""; default: return "<opaque>"; }
}

// ---------------------------------------------------------------- compiled workflow
struct Ref { uint8_t k = 0; V lit; int key = -1; };  // 0 literal, 1 state, 2 flow
static Ref mkRef(const J& j) {
  Ref r;
  if (j.t == J::O) {
    if (auto l = j.get("literal")) { r.lit = toV(*l); return r; }
    auto st = j.get("state"); auto fl = j.get("flow"); const J* p = st ? st : fl;
    if (p && p->t == J::A) { string k; for (size_t i = 0; i < p->a.size(); i++) { if (i) k += '.'; k += p->a[i].t == J::S ? p->a[i].s : dump(p->a[i]); }
      r.k = st ? 1 : 2; r.key = KEY.get(k); return r; }
  }
  r.lit = toV(j); return r;
}
enum Op { O_SET, O_EQ, O_EMIT, O_EMITIF, O_PURE, O_OBS };
// "чистые" вычисления: не наблюдаемы, результат — неинтерпретируемая функция от аргументов.
// Добавьте сюда префиксы своих чистых action (math., logic., string. ...).
static vector<string> PURE_EXTRA;  // --pure a.,b.
static const char* PURE_PREFIX[] = {"set.", "comparison.", "logic.", "math.", "string.", "array.", "object."};
struct Step { Op op; int act; vector<pair<int, Ref>> args, res; };
struct Blk { int type = 0; int ev = -1; vector<int> auth; vector<Step> steps; };  // 0 init, 1 main, 2 if-then
struct Ev { int name = -1, src = -1; V payload; vector<pair<int, V>> fields; };
struct QI { int dev; Ev ev; };
struct WF { vector<vector<Blk>> dev; vector<vector<V>> snap; vector<u64> startOut; };

static struct { int value, left, right, condition, scope, name, payload, local; } ID;
static int K_NAME, K_SRC, K_PAY;
static vector<int> DEVSTR;                 // индекс устройства -> id строки
static unordered_map<int, int> DEVIDX;     // id строки -> индекс устройства

static Step mkStep(const J& j) {
  Step s; string a = j.get("action") ? j.get("action")->s : "?"; s.act = STR.get(a);
  if (auto ar = j.get("arguments")) for (auto& kv : ar->o) s.args.emplace_back(STR.get(kv.first), mkRef(kv.second));
  sort(s.args.begin(), s.args.end(), [](auto& x, auto& y) { return x.first < y.first; });
  if (auto rs = j.get("results")) for (auto& kv : rs->o) s.res.emplace_back(STR.get(kv.first), mkRef(kv.second));
  if (a == "set.value") s.op = O_SET; else if (a == "comparison.equals") s.op = O_EQ;
  else if (a == "events.emit") s.op = O_EMIT; else if (a == "events.emitIf") s.op = O_EMITIF;
  else { s.op = O_OBS; for (auto pf : PURE_PREFIX) if (a.rfind(pf, 0) == 0) s.op = O_PURE;
          for (auto& pf : PURE_EXTRA) if (a.rfind(pf, 0) == 0) s.op = O_PURE; }
  return s;
}
static J loadFile(const char* path) {
  ifstream f(path, ios::binary); if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
  stringstream ss; ss << f.rdbuf(); J j = parse(ss.str());
  if (auto w = j.get("workflowJson")) { if (w->t == J::S) return parse(w->s); return *w; }
  return j;
}
static void compile(const J& j, WF& w, int ndev) {
  w.dev.assign(ndev, {});
  auto ws = j.get("workflows"); if (!ws) return;
  for (auto& kv : ws->o) {
    int d = DEVIDX[STR.get(kv.first)];
    for (auto& bj : kv.second.a) {
      Blk b; string t = bj.get("type") ? bj.get("type")->s : "";
      b.type = t == "init" ? 0 : t == "main" ? 1 : 2;
      if (b.type == 2) {
        if (auto i = bj.get("if")) b.ev = STR.get(i->s);
        if (auto au = bj.get("authors")) for (auto& x : au->a) b.auth.push_back(STR.get(x.s));
      }
      const J* st = bj.get("then"); if (!st) st = bj.get("steps");
      if (st) for (auto& sj : st->a) b.steps.push_back(mkStep(sj));
      w.dev[d].push_back(move(b));
    }
  }
}

// ---------------------------------------------------------------- simulator
struct Ctx {
  const WF* W; int nd, nk; vector<vector<V>> st; vector<V> flow; vector<u64> out; deque<QI> q; int n = 0;
  vector<string>* tr = nullptr; u64 seed = 0;
};
static V res(const Ref& r, int d, Ctx& c) { return r.k == 0 ? r.lit : r.k == 1 ? c.st[d][r.key] : c.flow[r.key]; }
static void wr(const Ref& r, int d, Ctx& c, V v) { if (r.k == 1) c.st[d][r.key] = v; else if (r.k == 2) c.flow[r.key] = v; }
static void observe(Ctx& c, u64 h, const string& d) { c.out.push_back(h); if (c.tr) c.tr->push_back(d); }

static void exec(const Step& s, int d, Ctx& c) {
  auto A = [&](int nm) { for (auto& a : s.args) if (a.first == nm) return res(a.second, d, c); return V{}; };
  switch (s.op) {
    case O_SET: { V v = A(ID.value); for (auto& r : s.res) wr(r.second, d, c, v); break; }
    case O_EQ: {
      V l = A(ID.left), r = A(ID.right); bool eq;
      if ((l.t == TOP) != (r.t == TOP)) { V p = l.t == TOP ? l : r, o = l.t == TOP ? r : l; eq = mix(p.x, vh(o) ^ c.seed) & 1; }  // opaque vs литерал
      else eq = l == r;
      V v = vb(eq); for (auto& rs : s.res) wr(rs.second, d, c, v); break; }
    case O_EMITIF: if (!truthy(A(ID.condition), c.seed)) break; [[fallthrough]];
    case O_EMIT: {
      V sc = A(ID.scope), nm = A(ID.name), pl = A(ID.payload); if (nm.t != TS) break;
      Ev e; e.name = nm.x; e.src = DEVSTR[d]; e.payload = pl;
      if (sc.t == TS && (int)sc.x == ID.local) c.q.push_back({d, e});
      else {
        for (int t = 0; t < c.nd; t++) c.q.push_back({t, e});
        u64 h = mix(mix(mix(0xE111, d), nm.x), vh(pl));
        observe(c, h, STR.v[DEVSTR[d]] + " emit(global) " + STR.v[nm.x] + " payload=" + vstr(pl));
      }
      break;
    }
    case O_PURE: case O_OBS: {
      u64 h = mix(s.op == O_OBS ? 0x0B5 : 0x9E, s.act); h = mix(h, d); string desc;
      if (c.tr) desc = STR.v[DEVSTR[d]] + " " + STR.v[s.act] + "(";
      for (auto& a : s.args) { V v = res(a.second, d, c); h = mix(mix(h, a.first), vh(v)); if (c.tr) desc += STR.v[a.first] + "=" + vstr(v) + " "; }
      for (auto& r : s.res) wr(r.second, d, c, V{TOP, mix(h, r.first)});  // результат = неинтерпретируемая функция
      if (s.op == O_OBS) observe(c, h, desc + ")");
      break;
    }
  }
}
static void runBlock(const Blk& b, int d, Ctx& c, const Ev* e) {
  fill(c.flow.begin(), c.flow.end(), V{});
  if (e) { c.flow[K_NAME] = {TS, (u64)e->name}; c.flow[K_SRC] = {TS, (u64)e->src}; c.flow[K_PAY] = e->payload;
           for (auto& f : e->fields) c.flow[f.first] = f.second; }
  for (auto& s : b.steps) exec(s, d, c);
}
static void drain(Ctx& c) {
  c.n = 0;
  while (!c.q.empty() && c.n < 4000) {
    QI qi = move(c.q.front()); c.q.pop_front(); c.n++;
    for (auto& b : c.W->dev[qi.dev]) {
      if (b.type != 2 || b.ev != qi.ev.name) continue;
      if (!b.auth.empty() && find(b.auth.begin(), b.auth.end(), qi.ev.src) == b.auth.end()) continue;
      runBlock(b, qi.dev, c, &qi.ev);
    }
  }
  if (!c.q.empty()) { c.out.push_back(0xDEADBEEF); c.q.clear(); }
}
static void startup(WF& W, int nd, int nk) {
  Ctx c; c.seed = SEED; c.W = &W; c.nd = nd; c.nk = nk; c.st.assign(nd, vector<V>(nk)); c.flow.assign(nk, V{});
  for (int t = 0; t < 2; t++) for (int d = 0; d < nd; d++) for (auto& b : W.dev[d]) if (b.type == t) runBlock(b, d, c, nullptr);
  drain(c); sort(c.out.begin(), c.out.end()); W.snap = c.st; W.startOut = c.out;
}

// ---------------------------------------------------------------- input generation
struct Cyc { vector<V> v; size_t i = 0; mt19937_64* g;
  V next() { if (i == 0) shuffle(v.begin(), v.end(), *g); V r = v[i]; i = (i + 1) % v.size(); return r; } };
struct ExtEv { int name; vector<int> authors; vector<int> keys; map<int, set<pair<int, u64>>> dom; };

static bool isPayKey(int k) { return KEY.v[k].rfind("event.payload", 0) == 0; }
static void collect(const WF& w, map<int, ExtEv>& listen, set<int>& emitted) {
  for (auto& blks : w.dev) for (auto& b : blks) {
    for (auto& s : b.steps) if (s.op == O_EMIT || s.op == O_EMITIF) for (auto& a : s.args)
      if (a.first == ID.name && a.second.k == 0 && a.second.lit.t == TS) emitted.insert(a.second.lit.x);
    if (b.type != 2) continue;
    auto& e = listen[b.ev]; e.name = b.ev; for (int a : b.auth) e.authors.push_back(a);
    for (auto& s : b.steps) {
      for (auto& a : s.args) if (a.second.k == 2 && isPayKey(a.second.key)) e.keys.push_back(a.second.key);
      if (s.op == O_EQ) { const Ref *l = nullptr, *r = nullptr;
        for (auto& a : s.args) { if (a.first == ID.left) l = &a.second; if (a.first == ID.right) r = &a.second; }
        if (l && r) for (int sw = 0; sw < 2; sw++) { auto f = sw ? r : l; auto o = sw ? l : r;
          if (f->k == 2 && isPayKey(f->key) && o->k == 0) e.dom[f->key].insert({o->lit.t, o->lit.x}); } }
    }
  }
}

struct Result { double sum = 0; int steps = 0, firstBad = INT_MAX; bool full = true; };
static double jacc(const vector<u64>& a, const vector<u64>& b) {
  size_t i = 0, j = 0, in = 0;
  while (i < a.size() && j < b.size()) { if (a[i] == b[j]) { in++; i++; j++; } else if (a[i] < b[j]) i++; else j++; }
  size_t un = a.size() + b.size() - in; return un ? (double)in / un : 1.0;
}
static vector<u64> runStep(Ctx& c, const Ev& e, vector<string>* tr = nullptr) {
  c.out.clear(); c.tr = tr;
  for (int d = 0; d < c.nd; d++) c.q.push_back({d, e});
  drain(c); vector<u64> o = c.out; sort(o.begin(), o.end()); return o;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s A.json B.json [--tests N] [--len L] [--seed S] [--threads T]\n", argv[0]); return 2; }
  int T = 5000, L = 8, thr = max(1u, thread::hardware_concurrency()); u64 seed = 1;
  for (int i = 3; i + 1 < argc; i += 2) { string o = argv[i]; long v = atol(argv[i + 1]);
    if (o == "--pure") { stringstream ss(argv[i + 1]); string t; while (getline(ss, t, ',')) PURE_EXTRA.push_back(t); }
    if (o == "--tests") T = v; else if (o == "--len") L = v; else if (o == "--seed") seed = v; else if (o == "--threads") thr = v; }
  SEED = seed;
  auto t0 = chrono::steady_clock::now();

  ID = {STR.get("value"), STR.get("left"), STR.get("right"), STR.get("condition"), STR.get("scope"), STR.get("name"), STR.get("payload"), STR.get("local")};
  K_NAME = KEY.get("event.name"); K_SRC = KEY.get("event.sourceDeviceId"); K_PAY = KEY.get("event.payload");

  J ja = loadFile(argv[1]), jb = loadFile(argv[2]);
  for (J* j : {&ja, &jb}) if (auto w = j->get("workflows")) for (auto& kv : w->o) { int s = STR.get(kv.first); if (!DEVIDX.count(s)) { DEVIDX[s] = DEVSTR.size(); DEVSTR.push_back(s); } }
  int nd = DEVSTR.size();
  WF A, B; compile(ja, A, nd); compile(jb, B, nd);

  map<int, ExtEv> listen; set<int> emitted; collect(A, listen, emitted); collect(B, listen, emitted);
  vector<ExtEv> ext; for (auto& kv : listen) if (!emitted.count(kv.first)) ext.push_back(kv.second);
  if (ext.empty()) { fprintf(stderr, "нет внешних входных событий — нечего подавать\n"); }

  mt19937_64 rng(seed);
  // домены аргументов + фиксированный набор «чужих» значений для веток по умолчанию
  vector<Ev> dummy; vector<pair<int, int>> trig;  // (индекс события, автор)
  vector<map<int, Cyc>> cyc(ext.size());
  for (size_t i = 0; i < ext.size(); i++) {
    auto& e = ext[i]; sort(e.authors.begin(), e.authors.end()); e.authors.erase(unique(e.authors.begin(), e.authors.end()), e.authors.end());
    sort(e.keys.begin(), e.keys.end()); e.keys.erase(unique(e.keys.begin(), e.keys.end()), e.keys.end());
    vector<int> au = e.authors; if (au.empty()) au = DEVSTR; au.push_back(STR.get("ext:unknown"));
    for (int a : au) trig.push_back({(int)i, a});
    for (int k : e.keys) {
      set<pair<int, u64>> s = e.dom[k];
      for (int j = 0; j < 3; j++) { V f = vs("~other" + to_string(j)); s.insert({f.t, f.x}); }
      { double z = 0, o1 = 1; u64 zx, ox; memcpy(&zx, &z, 8); memcpy(&ox, &o1, 8);   // тип аргумента неизвестен
        s.insert({TB, 0}); s.insert({TB, 1}); s.insert({TNUM, zx}); s.insert({TNUM, ox}); }
      if (e.dom[k].empty()) for (int d : DEVSTR) s.insert({TS, (u64)d});
      Cyc c; c.g = &rng; for (auto& p : s) c.v.push_back({(uint8_t)p.first, p.second}); cyc[i][k] = c;
    }
  }
  Cyc tc; tc.g = &rng; for (size_t i = 0; i < trig.size(); i++) tc.v.push_back({0, i});

  int nk = KEY.v.size();
  startup(A, nd, nk); startup(B, nd, nk);

  vector<vector<Ev>> tests(T);
  if (!trig.empty()) for (auto& t : tests) for (int s = 0; s < L; s++) {
    auto tr = trig[tc.next().x]; Ev e; e.name = ext[tr.first].name; e.src = tr.second;
    for (int k : ext[tr.first].keys) { V v = cyc[tr.first][k].next(); if (k == K_PAY) e.payload = v; else e.fields.push_back({k, v}); }
    t.push_back(e);
  }

  vector<Result> R(T);
  auto work = [&](int lo, int hi) {
    Ctx ca, cb; ca.W = &A; cb.W = &B; ca.nd = cb.nd = nd; ca.nk = cb.nk = nk; ca.flow.assign(nk, V{}); cb.flow = ca.flow;
    for (int i = lo; i < hi; i++) {
      ca.st = A.snap; cb.st = B.snap; ca.seed = cb.seed = mix(SEED, i + 1); Result& r = R[i];
      double s0 = jacc(A.startOut, B.startOut); r.sum += s0; r.steps++; if (s0 < 1) { r.full = false; r.firstBad = 0; }
      for (size_t k = 0; k < tests[i].size(); k++) {
        double s = jacc(runStep(ca, tests[i][k]), runStep(cb, tests[i][k]));
        r.sum += s; r.steps++; if (s < 1) { r.full = false; r.firstBad = min<int>(r.firstBad, k + 1); }
      }
    }
  };
  vector<thread> th; for (int t = 0; t < thr; t++) th.emplace_back(work, T * t / thr, T * (t + 1) / thr);
  for (auto& t : th) t.join();

  double sim = 0, startEq = jacc(A.startOut, B.startOut); int fullN = 0, worst = -1, bestBad = INT_MAX;
  for (int i = 0; i < T; i++) { sim += R[i].sum / R[i].steps; fullN += R[i].full;
    if (!R[i].full && R[i].firstBad < bestBad) { bestBad = R[i].firstBad; worst = i; } }
  sim /= max(T, 1);
  double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();

  printf("devices=%d external_events=%zu trigger_kinds=%zu tests=%d len=%d threads=%d time=%.1fms\n", nd, ext.size(), trig.size(), T, L, thr, ms);
  printf("startup_match      = %.4f\n", startEq);
  printf("tests_fully_equal  = %.4f\n", (double)fullN / max(T, 1));
  printf("SIMILARITY         = %.4f%s\n", sim, fullN == T ? "   (различий не найдено)" : "");
  if (worst >= 0) {  // кратчайший контрпример
    printf("\nконтрпример (шаг %d):\n", bestBad);
    Ctx ca, cb; ca.W = &A; cb.W = &B; ca.nd = cb.nd = nd; ca.nk = cb.nk = nk; ca.flow.assign(nk, V{}); cb.flow = ca.flow; ca.st = A.snap; cb.st = B.snap; ca.seed = cb.seed = mix(SEED, worst + 1);
    int upto = bestBad;
    if (bestBad == 0) { printf("  отличаются действия при старте\n"); }
    for (int k = 0; k < upto && k < (int)tests[worst].size(); k++) {
      auto& e = tests[worst][k]; string in = "  вход " + to_string(k + 1) + ": " + STR.v[e.name] + " от " + STR.v[e.src];
      if (e.payload.t) in += " payload=" + vstr(e.payload); for (auto& f : e.fields) in += " " + KEY.v[f.first] + "=" + vstr(f.second);
      printf("%s\n", in.c_str());
      vector<string> ta, tb; auto oa = runStep(ca, e, &ta), ob = runStep(cb, e, &tb);
      if (k + 1 == upto) { printf("  A делает:"); if (ta.empty()) printf(" —"); printf("\n"); for (auto& s : ta) printf("    %s\n", s.c_str());
                          printf("  B делает:"); if (tb.empty()) printf(" —"); printf("\n"); for (auto& s : tb) printf("    %s\n", s.c_str()); }
    }
  }
  return fullN == T ? 0 : 1;
}
