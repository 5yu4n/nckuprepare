#define _POSIX_C_SOURCE 199309L
#include "sort.c"
#include "graph.c"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { fails++; if (fails < 20) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static uint32_t eu[MAXM], ev[MAXM], ew[MAXM], tmpa[MAXM], tmpb[MAXM];
static int cmpu(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
#define NN 60
static uint32_t nd[NN][NN];
static void naive_apsp(uint32_t n, uint32_t m) {   // Floyd on nd from eu/ev/ew (directed)
    for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) nd[i][j] = i == j ? 0 : INF;
    for (uint32_t i = 0; i < m; i++) if (ew[i] < nd[eu[i]][ev[i]]) nd[eu[i]][ev[i]] = ew[i];
    for (uint32_t k = 0; k < n; k++) for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++)
        if (nd[i][k] != INF && nd[k][j] != INF && nd[i][k] + nd[k][j] < nd[i][j]) nd[i][j] = nd[i][k] + nd[k][j];
}
static uint32_t ncomp_without(uint32_t n, uint32_t m, int skipv, int skipe) {   // undirected components
    dsu_init(n); uint32_t c = n - (skipv >= 0);
    for (uint32_t i = 0; i < m; i++) { if ((int)i == skipe || (int)eu[i] == skipv || (int)ev[i] == skipv) continue; if (dsu_union(eu[i], ev[i])) c--; }
    return c;
}
int main(void) {
    double t0, t1, t2;
    // ---- sort
    for (int tc = 0; tc < 300; tc++) {
        uint32_t n = (uint32_t)(rnd() % 3000) + 1;
        for (uint32_t i = 0; i < n; i++) { tmpa[i] = (uint32_t)rnd() >> (rnd() % 32); eu[i] = tmpa[i]; }
        radix_sort_u32(tmpa, tmpb, n); qsort(eu, n, 4, cmpu);
        CHECK(!memcmp(tmpa, eu, n * 4));
        for (uint32_t i = 0; i < n; i++) { ev[i] = i; ew[i] = (uint32_t)rnd() % 50; }
        radix_sort_idx(ev, tmpb, ew, n);
        for (uint32_t i = 1; i < n; i++) CHECK(ew[ev[i - 1]] < ew[ev[i]] || (ew[ev[i - 1]] == ew[ev[i]] && ev[i - 1] < ev[i]));
        for (uint32_t i = 0; i < n; i++) tmpa[i] = (uint32_t)rnd() % 100;
        memcpy(eu, tmpa, n * 4); counting_sort(tmpa, n, tmpb, 100); qsort(eu, n, 4, cmpu); CHECK(!memcmp(tmpa, eu, n * 4));
        uint32_t k = (uint32_t)(rnd() % n); for (uint32_t i = 0; i < n; i++) tmpa[i] = (uint32_t)rnd() % (tc % 3 ? 1000000 : 3);
        memcpy(eu, tmpa, n * 4); qsort(eu, n, 4, cmpu); CHECK(quickselect(tmpa, n, k) == eu[k]);
        if (n <= 40) { for (uint32_t i = 0; i < n; i++) tmpa[i] = (uint32_t)rnd(); memcpy(eu, tmpa, n * 4); insertion_sort(tmpa, n); qsort(eu, n, 4, cmpu); CHECK(!memcmp(tmpa, eu, n * 4)); }
    }
    { uint32_t a[1] = {5}; CHECK(quickselect(a, 1, 0) == 5); uint32_t b[2] = {7, 3}; CHECK(quickselect(b, 2, 0) == 3); uint32_t c[2] = {7, 3}; CHECK(quickselect(c, 2, 1) == 7); }
    { uint32_t n = 200000; for (uint32_t i = 0; i < n; i++) tmpa[i] = eu[i] = (uint32_t)rnd();
      t0 = now(); qsort(eu, n, 4, cmpu); t1 = now(); radix_sort_u32(tmpa, tmpb, n); t2 = now(); CHECK(!memcmp(tmpa, eu, n * 4));
      printf("sort 2e5: qsort %.4f radix %.4f x%.1f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1));
      for (uint32_t i = 0; i < n; i++) tmpa[i] = eu[i] = (uint32_t)rnd(); t0 = now(); qsort(eu, n, 4, cmpu); uint32_t med = eu[n / 2]; t1 = now(); CHECK(quickselect(tmpa, n, n / 2) == med); t2 = now();
      printf("median 2e5: sort %.4f quickselect %.4f x%.1f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // ---- random small graphs: BFS, Dijkstra, 0-1, Bellman, Floyd, closure, bitset bfs, topo, dfs, scc
    for (int tc = 0; tc < 400; tc++) {
        uint32_t n = (uint32_t)(rnd() % NN) + 1, m = (uint32_t)(rnd() % (n * 4 + 1));
        for (uint32_t i = 0; i < m; i++) { eu[i] = (uint32_t)(rnd() % n); ev[i] = (uint32_t)(rnd() % n); ew[i] = (uint32_t)(rnd() % 1000); }
        // unweighted
        uint32_t w1[MAXM / 64]; for (uint32_t i = 0; i < m; i++) w1[i] = ew[i], ew[i] = 1;
        naive_apsp(n, m); csr_build(n, m, eu, ev, ew);
        uint32_t s = (uint32_t)(rnd() % n);
        bfs(n, s); for (uint32_t v = 0; v < n; v++) CHECK(dist[v] == nd[s][v]);
        memset(abit, 0, sizeof abit); for (uint32_t i = 0; i < m; i++) abit[eu[i]][ev[i] >> 5] |= 1u << (ev[i] & 31);
        bfs_bitset(n, s); for (uint32_t v = 0; v < n; v++) CHECK(dist[v] == nd[s][v]);
        memset(reach, 0, sizeof reach); for (uint32_t i = 0; i < n; i++) { memcpy(reach[i], abit[i], sizeof reach[i]); reach[i][i >> 5] |= 1u << (i & 31); }
        closure(n); for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) CHECK(((reach[i][j >> 5] >> (j & 31)) & 1) == (nd[i][j] != INF));
        // scc
        uint32_t nc = scc_tarjan(n);
        for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) CHECK((comp[i] == comp[j]) == (nd[i][j] != INF && nd[j][i] != INF));
        for (uint32_t i = 0; i < m; i++) if (comp[eu[i]] != comp[ev[i]]) CHECK(comp[eu[i]] > comp[ev[i]]);   // reverse topo
        CHECK(nc <= n);
        // cycle / topo
        int cyc = 0; for (uint32_t i = 0; i < n && !cyc; i++) for (uint32_t j = 0; j < n; j++) if (i != j && nd[i][j] != INF && nd[j][i] != INF) cyc = 1;
        for (uint32_t i = 0; i < m; i++) if (eu[i] == ev[i]) cyc = 1;
        static uint32_t ord[MAXN], pos_[MAXN];
        CHECK(dfs_has_cycle(n, ord) == cyc);
        uint32_t tcount = topo_kahn(n, ord); CHECK((tcount == n) == !cyc);
        if (!cyc) { for (uint32_t i = 0; i < n; i++) pos_[ord[i]] = i; for (uint32_t i = 0; i < m; i++) CHECK(pos_[eu[i]] < pos_[ev[i]]); }
        // weighted
        for (uint32_t i = 0; i < m; i++) ew[i] = w1[i];
        naive_apsp(n, m); csr_build(n, m, eu, ev, ew);
        dijkstra(n, s); for (uint32_t v = 0; v < n; v++) CHECK(dist[v] == nd[s][v]);
        for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) fw[i][j] = i == j ? 0 : FINF;
        for (uint32_t i = 0; i < m; i++) if (ew[i] < fw[eu[i]][ev[i]]) fw[eu[i]][ev[i]] = ew[i];
        floyd(n); for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) CHECK((fw[i][j] == FINF ? INF : fw[i][j]) == nd[i][j]);
        if (n <= DN) { for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) W[i][j] = INF;
            for (uint32_t i = 0; i < m; i++) if (ew[i] < W[eu[i]][ev[i]]) W[eu[i]][ev[i]] = ew[i];
            dijkstra_dense(n, s, tmpa); for (uint32_t v = 0; v < n; v++) CHECK(tmpa[v] == nd[s][v]); }
        { int32_t iw[MAXM / 64]; for (uint32_t i = 0; i < m; i++) iw[i] = (int32_t)ew[i];
          CHECK(bellman_ford(n, m, eu, ev, iw, s) == 0); for (uint32_t v = 0; v < n; v++) CHECK((bdist[v] == INT64_MAX ? INF : (uint32_t)bdist[v]) == nd[s][v]);
          // negative weights, check against naive with relaxation n^2 rounds
          for (uint32_t i = 0; i < m; i++) iw[i] = (int32_t)(rnd() % 40) - 8;
          int neg = bellman_ford(n, m, eu, ev, iw, s);
          static int64_t d2[NN]; for (uint32_t i = 0; i < n; i++) d2[i] = INT64_MAX; d2[s] = 0; int ch = 0;
          for (uint32_t r = 0; r < 3 * n + 2; r++) { ch = 0; for (uint32_t i = 0; i < m; i++) if (d2[eu[i]] != INT64_MAX && d2[eu[i]] + iw[i] < d2[ev[i]]) { d2[ev[i]] = d2[eu[i]] + iw[i]; ch = 1; } }
          CHECK(neg == ch); if (!neg) for (uint32_t v = 0; v < n; v++) CHECK(bdist[v] == d2[v]); }
        // 0-1
        for (uint32_t i = 0; i < m; i++) ew[i] = (uint32_t)(rnd() & 1);
        naive_apsp(n, m); csr_build(n, m, eu, ev, ew); bfs01(n, s); for (uint32_t v = 0; v < n; v++) CHECK(dist[v] == nd[s][v]);
        // undirected: kruskal vs prim, bipartite, bridges, lca on tree
        uint32_t mu = m;
        for (uint32_t i = 0; i < mu; i++) ew[i] = (uint32_t)(rnd() % 100);
        uint64_t k1 = kruskal(n, mu, eu, ev, ew, tmpa, tmpb);
        for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) W[i][j] = INF;
        for (uint32_t i = 0; i < mu; i++) if (eu[i] != ev[i] && ew[i] < W[eu[i]][ev[i]]) W[eu[i]][ev[i]] = W[ev[i]][eu[i]] = ew[i];
        CHECK(prim_dense(n, tmpa) == k1);
        static uint32_t du[MAXM], dv[MAXM], de[MAXM];
        for (uint32_t i = 0; i < mu; i++) { du[2 * i] = eu[i]; dv[2 * i] = ev[i]; du[2 * i + 1] = ev[i]; dv[2 * i + 1] = eu[i]; de[2 * i] = de[2 * i + 1] = i; }
        csr_build(n, 2 * mu, du, dv, de);
        { // bipartite naive: 2-coloring brute via BFS on all is fine? use odd cycle via parity DSU
          int bip = 1; static uint32_t col[NN]; static int vis[NN]; memset(vis, 0, sizeof vis);
          for (uint32_t st = 0; st < n; st++) if (!vis[st]) { uint32_t q[NN], h = 0, t = 0; q[t++] = st; vis[st] = 1; col[st] = 0;
              while (h < t) { uint32_t u = q[h++]; for (uint32_t i = 0; i < mu; i++) { uint32_t a = eu[i], b = ev[i], o; if (a == u) o = b; else if (b == u) o = a; else continue;
                  if (!vis[o]) { vis[o] = 1; col[o] = col[u] ^ 1; q[t++] = o; } else if (col[o] == col[u]) bip = 0; } } }
          CHECK(is_bipartite(n) == bip); }
        bridges_arts(n, mu);
        uint32_t base = ncomp_without(n, mu, -1, -1);
        for (uint32_t i = 0; i < mu; i++) CHECK(is_bridge[i] == (ncomp_without(n, mu, -1, (int)i) > base));
        for (uint32_t v = 0; v < n; v++) CHECK(is_art[v] == (ncomp_without(n, mu, (int)v, -1) > base - 1 + 0 * v ? 1 : 0) || (ncomp_without(n, mu, (int)v, -1) > base) == is_art[v]);
    }
    // stricter articulation check
    for (int tc = 0; tc < 500; tc++) {
        uint32_t n = (uint32_t)(rnd() % 30) + 1, m = (uint32_t)(rnd() % (2 * n + 1));
        for (uint32_t i = 0; i < m; i++) { eu[i] = (uint32_t)(rnd() % n); ev[i] = (uint32_t)(rnd() % n); }
        static uint32_t du[MAXM], dv[MAXM], de[MAXM];
        for (uint32_t i = 0; i < m; i++) { du[2 * i] = eu[i]; dv[2 * i] = ev[i]; du[2 * i + 1] = ev[i]; dv[2 * i + 1] = eu[i]; de[2 * i] = de[2 * i + 1] = i; }
        csr_build(n, 2 * m, du, dv, de); bridges_arts(n, m);
        uint32_t base = ncomp_without(n, m, -1, -1);
        for (uint32_t v = 0; v < n; v++) { int isolated = 1; for (uint32_t i = 0; i < m; i++) if ((eu[i] == v) != (ev[i] == v)) isolated = 0;
            uint32_t c = ncomp_without(n, m, (int)v, -1); CHECK(is_art[v] == (isolated ? 0 : c > base)); }
        for (uint32_t i = 0; i < m; i++) CHECK(is_bridge[i] == (ncomp_without(n, m, -1, (int)i) > base));
    }
    // LCA on random trees
    for (int tc = 0; tc < 300; tc++) {
        uint32_t n = (uint32_t)(rnd() % 3000) + 1, m = 0;
        static uint32_t P[MAXN], D[MAXN];
        for (uint32_t v = 1; v < n; v++) { P[v] = (tc & 1) ? v - 1 - (uint32_t)(rnd() % (v < 3 ? v : 3)) : (uint32_t)(rnd() % v); eu[m] = v; ev[m] = P[v]; m++; eu[m] = P[v]; ev[m] = v; m++; }
        csr_build(n, m, eu, ev, NULL); lca_build(n, 0); D[0] = 0; for (uint32_t v = 1; v < n; v++) D[v] = D[P[v]] + 1;
        for (uint32_t v = 0; v < n; v++) CHECK(dep[v] == D[v]);
        for (int q = 0; q < 300; q++) { uint32_t a = (uint32_t)(rnd() % n), b = (uint32_t)(rnd() % n), x = a, y = b;
            while (D[x] > D[y]) x = P[x]; while (D[y] > D[x]) y = P[y]; while (x != y) { x = P[x]; y = P[y]; } CHECK(lca(a, b) == x);
            uint32_t d = (uint32_t)(rnd() % (D[a] + 1)), z = a; while (D[z] > d) z = P[z]; CHECK(lca_up(a, d) == z); }
    }
    { uint32_t n = 20000, m = 0; for (uint32_t v = 1; v < n; v++) { eu[m] = v; ev[m] = v - 1; m++; eu[m] = v - 1; ev[m] = v; m++; }
      csr_build(n, m, eu, ev, NULL); lca_build(n, 0); uint32_t maxsteps = 0;
      for (int q = 0; q < 2000; q++) { uint32_t a = (uint32_t)(rnd() % n), b = (uint32_t)(rnd() % n); uint32_t x = lca(a, b); CHECK(x == (a < b ? a : b)); }
      t0 = now(); uint64_t acc = 0; for (int q = 0; q < 200000; q++) acc += lca((uint32_t)(rnd() % n), (uint32_t)(rnd() % n)); t1 = now();
      for (int q = 0; q < 2000; q++) { uint32_t a = (uint32_t)(rnd() % n), b = (uint32_t)(rnd() % n); while (dep[a] > dep[b]) a = tpar[a]; while (dep[b] > dep[a]) b = tpar[b]; while (a != b) { a = tpar[a]; b = tpar[b]; } acc += a; } t2 = now();
      (void)maxsteps; printf("LCA path n=2e4: jump %.2e s/q, naive %.2e s/q x%.0f (sink %llu)\n", (t1 - t0) / 200000, (t2 - t1) / 2000, ((t2 - t1) / 2000) / ((t1 - t0) / 200000), (unsigned long long)(acc & 1)); }
    // Dinic vs brute-force max flow (Edmonds-Karp on matrix)
    for (int tc = 0; tc < 300; tc++) {
        uint32_t n = (uint32_t)(rnd() % 12) + 2, m = (uint32_t)(rnd() % 40);
        static int64_t C[16][16]; memset(C, 0, sizeof C); flow_init(n);
        for (uint32_t i = 0; i < m; i++) { uint32_t u = (uint32_t)(rnd() % n), v = (uint32_t)(rnd() % n), c = (uint32_t)(rnd() % 20); if (u == v) continue; flow_add(u, v, c); C[u][v] += c; }
        uint64_t f = dinic(n, 0, n - 1), g = 0;
        for (;;) { int pr[16]; for (int i = 0; i < 16; i++) pr[i] = -1; pr[0] = 0; int q[16], h = 0, t = 0; q[t++] = 0;
            while (h < t) { int u = q[h++]; for (uint32_t v = 0; v < n; v++) if (pr[v] < 0 && C[u][v] > 0) { pr[v] = u; q[t++] = (int)v; } }
            if (pr[n - 1] < 0) break; int64_t b = INT64_MAX; for (int v = (int)n - 1; v; v = pr[v]) if (C[pr[v]][v] < b) b = C[pr[v]][v];
            for (int v = (int)n - 1; v; v = pr[v]) { C[pr[v]][v] -= b; C[v][pr[v]] += b; } g += (uint64_t)b; }
        CHECK(f == g);
    }
    // bipartite matching via dinic vs brute
    for (int tc = 0; tc < 300; tc++) {
        uint32_t L = (uint32_t)(rnd() % 7) + 1, R = (uint32_t)(rnd() % 7) + 1, adjm[8] = {0};
        flow_init(L + R + 2); uint32_t s = L + R, t = s + 1;
        for (uint32_t i = 0; i < L; i++) flow_add(s, i, 1); for (uint32_t j = 0; j < R; j++) flow_add(L + j, t, 1);
        for (uint32_t i = 0; i < L; i++) for (uint32_t j = 0; j < R; j++) if (rnd() % 3 == 0) { adjm[i] |= 1u << j; flow_add(i, L + j, 1); }
        uint64_t f = dinic(L + R + 2, s, t);
        uint32_t best = 0; // brute force: dp over left with used-right mask
        static int8_t memo[9][256]; for (int a = 0; a < 9; a++) for (int b = 0; b < 256; b++) memo[a][b] = -1;
        for (int i = (int)L; i >= 0; i--) for (uint32_t mask = 0; mask < (1u << R); mask++) { if (i == (int)L) { memo[i][mask] = 0; continue; } int8_t bb = memo[i + 1][mask]; for (uint32_t j = 0; j < R; j++) if ((adjm[i] >> j & 1) && !(mask >> j & 1) && memo[i + 1][mask | 1u << j] + 1 > bb) bb = (int8_t)(memo[i + 1][mask | 1u << j] + 1); memo[i][mask] = bb; }
        best = (uint32_t)memo[0][0]; CHECK(f == best);
    }
    { // big matching timing
        uint32_t L = 900, R = 900; flow_init(L + R + 2); uint32_t s = L + R, t = s + 1;
        for (uint32_t i = 0; i < L; i++) flow_add(s, i, 1); for (uint32_t j = 0; j < R; j++) flow_add(L + j, t, 1);
        for (uint32_t i = 0; i < L; i++) for (int k = 0; k < 18; k++) flow_add(i, L + (uint32_t)(rnd() % R), 1);
        t0 = now(); uint64_t f = dinic(L + R + 2, s, t); t1 = now(); printf("matching 900x900, 16200 edges: %llu in %.4f s\n", (unsigned long long)f, t1 - t0); }
    // grid BFS vs A* vs naive BFS with dist
    for (int tc = 0; tc < 200; tc++) {
        uint32_t H = (uint32_t)(rnd() % 60) + 1, Wd = (uint32_t)(rnd() % 60) + 1, S_ = Wd + 2, dens = (uint32_t)(rnd() % 40);
        static uint8_t base[GMAX]; memset(base, 1, (H + 2) * S_);
        for (uint32_t r = 0; r < H; r++) for (uint32_t c = 0; c < Wd; c++) base[(r + 1) * S_ + c + 1] = (rnd() % 100) < dens;
        uint32_t sr = (uint32_t)(rnd() % H), sc = (uint32_t)(rnd() % Wd), tr = (uint32_t)(rnd() % H), tcc = (uint32_t)(rnd() % Wd);
        base[(sr + 1) * S_ + sc + 1] = 0;
        static uint32_t dd[GMAX]; memset(dd, 0xFF, sizeof dd); static uint32_t q[GMAX]; uint32_t h = 0, t = 0; q[t++] = (sr + 1) * S_ + sc + 1; dd[q[0]] = 0;
        while (h < t) { uint32_t u = q[h++]; int32_t dirs[4] = {1, -1, (int32_t)S_, -(int32_t)S_}; for (int d = 0; d < 4; d++) { uint32_t v = (uint32_t)((int32_t)u + dirs[d]); if (!base[v] && dd[v] == INF) { dd[v] = dd[u] + 1; q[t++] = v; } } }
        uint32_t want = dd[(tr + 1) * S_ + tcc + 1];
        memcpy(gwall, base, (H + 2) * S_); CHECK(grid_bfs(H, Wd, sr, sc, tr, tcc) == want);
        memcpy(gwall, base, (H + 2) * S_); CHECK(grid_astar(H, Wd, sr, sc, tr, tcc) == want);
    }
    { uint32_t H = 250, Wd = 250, S_ = Wd + 2; static uint8_t base[GMAX]; uint32_t r1 = INF, r2 = 0;
      while (r1 == INF) { memset(base, 1, (H + 2) * S_);
        for (uint32_t r = 0; r < H; r++) for (uint32_t c = 0; c < Wd; c++) base[(r + 1) * S_ + c + 1] = (rnd() % 100) < 20;
        base[S_ + 1] = 0; base[H * S_ + Wd] = 0; base[101 * S_ + 101] = 0; base[111 * S_ + 141] = 0;
        memcpy(gwall, base, (H + 2) * S_); r1 = grid_bfs(H, Wd, 0, 0, H - 1, Wd - 1);
        memcpy(gwall, base, (H + 2) * S_); if (grid_bfs(H, Wd, 100, 100, 110, 140) == INF) r1 = INF; }
      t0 = now(); for (int i = 0; i < 50; i++) { memcpy(gwall, base, (H + 2) * S_); r1 = grid_bfs(H, Wd, 0, 0, H - 1, Wd - 1); } t1 = now();
      for (int i = 0; i < 50; i++) { memcpy(gwall, base, (H + 2) * S_); r2 = grid_astar(H, Wd, 0, 0, H - 1, Wd - 1); } t2 = now();
      CHECK(r1 == r2); printf("grid 250x250 20%% walls corner-to-corner: bfs %.6f astar %.6f expanded %u/%u (dist %u)\n", (t1 - t0) / 50, (t2 - t1) / 50, astar_expanded, H * Wd, r1);
      t0 = now(); for (int i = 0; i < 50; i++) { memcpy(gwall, base, (H + 2) * S_); r1 = grid_bfs(H, Wd, 100, 100, 110, 140); } t1 = now();
      for (int i = 0; i < 50; i++) { memcpy(gwall, base, (H + 2) * S_); r2 = grid_astar(H, Wd, 100, 100, 110, 140); } t2 = now();
      CHECK(r1 == r2); printf("grid nearby target: bfs %.6f astar %.6f expanded %u (dist %u) x%.1f\n", (t1 - t0) / 50, (t2 - t1) / 50, astar_expanded, r1, (t1 - t0) / (t2 - t1)); }
    // big graph timings: dijkstra heap vs dense O(n^2) on sparse, bfs_bitset vs bfs on dense, closure vs floyd
    { uint32_t n = 20000, m = 100000; for (uint32_t i = 0; i < m; i++) { eu[i] = (uint32_t)(rnd() % n); ev[i] = (uint32_t)(rnd() % n); ew[i] = (uint32_t)(rnd() % 1000); }
      csr_build(n, m, eu, ev, ew); t0 = now(); dijkstra(n, 0); t1 = now(); printf("dijkstra heap n=2e4 m=1e5: %.4f s\n", t1 - t0);
      // naive O(n^2) select-min on CSR
      static uint32_t d2[MAXN]; static uint8_t u2[MAXN]; memset(d2, 0xFF, sizeof d2); memset(u2, 0, sizeof u2); d2[0] = 0; t1 = now();
      for (uint32_t it = 0; it < n; it++) { uint32_t u = INF, b = INF; for (uint32_t i = 0; i < n; i++) if (!u2[i] && d2[i] < b) { b = d2[i]; u = i; } if (u == INF) break; u2[u] = 1; for (uint32_t k = head[u]; k < head[u + 1]; k++) if (b + wt[k] < d2[adj[k]]) d2[adj[k]] = b + wt[k]; }
      t2 = now(); CHECK(!memcmp(d2, dist, n * 4)); printf("dijkstra O(n^2) same graph: %.4f s  heap x%.0f\n", t2 - t1, (t2 - t1) / (t1 - t0)); }
    { uint32_t n = 1000; memset(reach, 0, sizeof reach); for (uint32_t i = 0; i < n; i++) for (int k = 0; k < 2; k++) { uint32_t j = (uint32_t)(rnd() % n); reach[i][j >> 5] |= 1u << (j & 31); }
      static uint32_t R2[BN][BW]; memcpy(R2, reach, sizeof R2);
      static uint8_t B[BN][BN]; for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) B[i][j] = (reach[i][j >> 5] >> (j & 31)) & 1;
      t0 = now(); closure(n); t1 = now();
      for (uint32_t k = 0; k < n; k++) for (uint32_t i = 0; i < n; i++) if (B[i][k]) for (uint32_t j = 0; j < n; j++) B[i][j] |= B[k][j];
      t2 = now(); int same = 1; for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) if (B[i][j] != ((reach[i][j >> 5] >> (j & 31)) & 1)) same = 0; CHECK(same);
      printf("closure n=1000: bitset %.4f bytes %.4f x%.1f\n", t1 - t0, t2 - t1, (t2 - t1) / (t1 - t0)); (void)R2; }
    printf("fails=%d\n", fails);
    return fails != 0;
}
