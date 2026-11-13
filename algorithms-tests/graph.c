#include <stdint.h>
#include <string.h>
//@@ csr
// CSR（壓縮鄰接表）：沒有 malloc、沒有指標，u 的鄰居是 adj[head[u] .. head[u+1]-1]
// RAM = 4*(N + 2M) bytes（含權重）。無向圖每條邊放兩次，M 要算兩倍
#define MAXN 20000
#define MAXM 200000
#define INF 0xFFFFFFFFu
static uint32_t head[MAXN + 1], adj[MAXM], wt[MAXM];
static void csr_build(uint32_t n, uint32_t m, const uint32_t *eu, const uint32_t *ev, const uint32_t *ew) {
    static uint32_t pos[MAXN];
    memset(head, 0, (n + 1) * sizeof head[0]);
    for (uint32_t i = 0; i < m; i++) head[eu[i] + 1]++;
    for (uint32_t i = 0; i < n; i++) head[i + 1] += head[i];
    memcpy(pos, head, n * sizeof pos[0]);
    for (uint32_t i = 0; i < m; i++) {
        uint32_t k = pos[eu[i]]++;
        adj[k] = ev[i];
        if (ew) wt[k] = ew[i];
    }
}
//@@ bfs
// 無權最短路 / 可達性，O(N+M)，佇列就是陣列
static uint32_t dist[MAXN], que[MAXN];
static void bfs(uint32_t n, uint32_t s) {
    uint32_t qh = 0, qt = 0;
    memset(dist, 0xFF, n * sizeof dist[0]);
    dist[s] = 0; que[qt++] = s;
    while (qh < qt) {
        uint32_t u = que[qh++];
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k];
            if (dist[v] == INF) { dist[v] = dist[u] + 1; que[qt++] = v; }
        }
    }
}
//@@ dfs
// 迭代 DFS（不遞迴，MCU 堆疊只有幾 KB）：有向圖找環 + 後序。order 反過來就是拓樸序
static uint8_t color[MAXN];                  // 0 沒走過、1 在堆疊上、2 完成
static uint32_t stk[MAXN], it_[MAXN];
static int dfs_has_cycle(uint32_t n, uint32_t *order) {
    uint32_t cnt = 0;
    memset(color, 0, n);
    for (uint32_t s = 0; s < n; s++) {
        if (color[s]) continue;
        uint32_t sp = 0;
        stk[sp++] = s; color[s] = 1; it_[s] = head[s];
        while (sp) {
            uint32_t u = stk[sp - 1];
            if (it_[u] < head[u + 1]) {
                uint32_t v = adj[it_[u]++];
                if (color[v] == 1) return 1;            // 回邊 → 有環
                if (!color[v]) { color[v] = 1; it_[v] = head[v]; stk[sp++] = v; }
            } else {
                color[u] = 2; sp--;
                if (order) order[cnt++] = u;
            }
        }
    }
    return 0;
}
//@@ topo
// Kahn 拓樸排序 O(N+M)。回傳排進 out 的個數；< n 代表有環（DAG 判定）
static uint32_t indeg[MAXN];
static uint32_t topo_kahn(uint32_t n, uint32_t *out) {
    uint32_t h = 0, t = 0;
    memset(indeg, 0, n * sizeof indeg[0]);
    for (uint32_t k = 0; k < head[n]; k++) indeg[adj[k]]++;
    for (uint32_t u = 0; u < n; u++) if (!indeg[u]) out[t++] = u;
    while (h < t) {                               // out 本身就是佇列
        uint32_t u = out[h++];
        for (uint32_t k = head[u]; k < head[u + 1]; k++)
            if (--indeg[adj[k]] == 0) out[t++] = adj[k];
    }
    return t;
}
// DAG 上的最長/最短路、路徑數：照 out 的順序做一次 DP 就好，O(N+M)
//@@ dijkstra
// Dijkstra + 二元堆積，O((N+M) log M)。(距離<<32 | 節點) 打包成一個 uint64：比較只要一次
// 權重非負、距離總和 < 2^32。延遲刪除：彈出的距離比 dist 舊就跳過
static uint64_t heap[MAXM + 1];
static uint32_t hn;
static void hpush(uint64_t x) {
    uint32_t i = hn++;
    while (i) { uint32_t p = (i - 1) >> 1; if (heap[p] <= x) break; heap[i] = heap[p]; i = p; }
    heap[i] = x;
}
static uint64_t hpop(void) {
    uint64_t top = heap[0], x = heap[--hn];
    uint32_t i = 0;
    for (;;) {
        uint32_t c = 2 * i + 1;
        if (c >= hn) break;
        if (c + 1 < hn && heap[c + 1] < heap[c]) c++;
        if (x <= heap[c]) break;
        heap[i] = heap[c]; i = c;
    }
    heap[i] = x;
    return top;
}
static void dijkstra(uint32_t n, uint32_t s) {
    memset(dist, 0xFF, n * sizeof dist[0]);
    dist[s] = 0; hn = 0; hpush(s);
    while (hn) {
        uint64_t top = hpop();
        uint32_t u = (uint32_t)top, d = (uint32_t)(top >> 32);
        if (d != dist[u]) continue;
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k], nd = d + wt[k];
            if (nd < dist[v]) { dist[v] = nd; hpush((uint64_t)nd << 32 | v); }
        }
    }
}
//@@ bfs01
// 0-1 BFS：權重只有 0 或 1，用雙端佇列，O(N+M)，比 Dijkstra 少一個 log
#define DQ (1u << 18)                             // 2 的冪，>= M+1
static uint32_t dq[DQ];
static uint8_t done01[MAXN];
static void bfs01(uint32_t n, uint32_t s) {
    uint32_t h = 0, t = 0;                        // 無號數自然溢位，用 & (DQ-1) 取位置
    memset(dist, 0xFF, n * sizeof dist[0]);
    memset(done01, 0, n);
    dist[s] = 0; dq[t++ & (DQ - 1)] = s;
    while (h != t) {
        uint32_t u = dq[h++ & (DQ - 1)];
        if (done01[u]) continue;
        done01[u] = 1;
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k], nd = dist[u] + wt[k];
            if (nd < dist[v]) {
                dist[v] = nd;
                if (wt[k]) dq[t++ & (DQ - 1)] = v; else dq[--h & (DQ - 1)] = v;
            }
        }
    }
}
//@@ bellman
// Bellman-Ford（可有負權）O(NM)，一輪沒變就提早結束。回傳 1 = 從 s 可達負環
static int64_t bdist[MAXN];
static int bellman_ford(uint32_t n, uint32_t m, const uint32_t *eu, const uint32_t *ev, const int32_t *ew, uint32_t s) {
    for (uint32_t i = 0; i < n; i++) bdist[i] = INT64_MAX;
    bdist[s] = 0;
    for (uint32_t round = 0; round < n; round++) {
        int changed = 0;
        for (uint32_t i = 0; i < m; i++)
            if (bdist[eu[i]] != INT64_MAX && bdist[eu[i]] + ew[i] < bdist[ev[i]]) {
                bdist[ev[i]] = bdist[eu[i]] + ew[i]; changed = 1;
            }
        if (!changed) return 0;
    }
    return 1;                                     // 第 n 輪還在變
}
//@@ floyd
// Floyd–Warshall 全點對 O(N^3)。N=200 → 160 KB。INF 取 0x3FFFFFFF 讓兩個相加不溢位
#define FW 200
#define FINF 0x3FFFFFFFu
static uint32_t fw[FW][FW];
static void floyd(uint32_t n) {                   // 先設 fw[i][i]=0、邊權、其餘 FINF
    for (uint32_t k = 0; k < n; k++)
        for (uint32_t i = 0; i < n; i++) {
            uint32_t dik = fw[i][k];
            if (dik == FINF) continue;            // 這一列整列跳過
            for (uint32_t j = 0; j < n; j++) {
                uint32_t t = dik + fw[k][j];
                if (t < fw[i][j]) fw[i][j] = t;
            }
        }
}
//@@ closure
// 遞移閉包（誰能到誰）用位元集合：O(N^3/32)，比 Floyd 快 32 倍。N=1024 → 128 KB
#define BN 1024
#define BW (BN / 32)
static uint32_t reach[BN][BW];                    // 先放直接邊；要「自己到自己」就設 bit i
static void closure(uint32_t n) {
    uint32_t w = (n + 31) >> 5;
    for (uint32_t k = 0; k < n; k++)
        for (uint32_t i = 0; i < n; i++)
            if ((reach[i][k >> 5] >> (k & 31)) & 1)
                for (uint32_t j = 0; j < w; j++) reach[i][j] |= reach[k][j];
}
//@@ bfs_bitset
// 稠密圖 BFS：鄰接矩陣用位元，一次處理 32 個鄰居，O(N^2/32)
static uint32_t abit[BN][BW], visb[BW];
static void bfs_bitset(uint32_t n, uint32_t s) {
    uint32_t w = (n + 31) >> 5, qh = 0, qt = 0;
    memset(visb, 0, sizeof visb);
    memset(dist, 0xFF, n * sizeof dist[0]);
    dist[s] = 0; que[qt++] = s; visb[s >> 5] |= 1u << (s & 31);
    while (qh < qt) {
        uint32_t u = que[qh++];
        for (uint32_t j = 0; j < w; j++) {
            uint32_t nb = abit[u][j] & ~visb[j];   // 還沒走過的鄰居
            visb[j] |= nb;
            while (nb) {
                uint32_t v = (j << 5) + (uint32_t)__builtin_ctz(nb);
                nb &= nb - 1;
                dist[v] = dist[u] + 1; que[qt++] = v;
            }
        }
    }
}
//@@ dsu
// 並查集：路徑減半 + 依大小合併，近乎 O(1)。動態連通、合併群組、無向圖找環、Kruskal 都用它
static uint32_t par[MAXN], sz[MAXN];
static void dsu_init(uint32_t n) { for (uint32_t i = 0; i < n; i++) { par[i] = i; sz[i] = 1; } }
static uint32_t dsu_find(uint32_t x) {
    while (par[x] != x) { par[x] = par[par[x]]; x = par[x]; }
    return x;
}
static int dsu_union(uint32_t a, uint32_t b) {    // 回傳 0 = 本來就同一群（無向圖：這條邊造成環）
    a = dsu_find(a); b = dsu_find(b);
    if (a == b) return 0;
    if (sz[a] < sz[b]) { uint32_t t = a; a = b; b = t; }
    par[b] = a; sz[a] += sz[b];
    return 1;
}
//@@ kruskal
// Kruskal MST：基數排序邊（不用 qsort）+ 並查集，O(M α)。不連通回傳 UINT64_MAX。需要 radix_sort_idx
static uint64_t kruskal(uint32_t n, uint32_t m, const uint32_t *eu, const uint32_t *ev,
                        const uint32_t *ew, uint32_t *idx, uint32_t *tmp) {
    uint64_t total = 0;
    uint32_t used = 0;
    for (uint32_t i = 0; i < m; i++) idx[i] = i;
    radix_sort_idx(idx, tmp, ew, m);
    dsu_init(n);
    for (uint32_t i = 0; i < m && used + 1 < n; i++) {
        uint32_t e = idx[i];
        if (dsu_union(eu[e], ev[e])) { total += ew[e]; used++; }
    }
    return (n <= 1 || used + 1 == n) ? total : UINT64_MAX;
}
//@@ dense
// 稠密圖（M ≈ N^2）：不用堆積，O(N^2) 的 Dijkstra / Prim 反而比較快。N=300 → 360 KB
#define DN 300
static uint32_t W[DN][DN];                        // INF = 沒有邊
static uint8_t used_[DN];
static void dijkstra_dense(uint32_t n, uint32_t s, uint32_t *d) {
    memset(used_, 0, n);
    for (uint32_t i = 0; i < n; i++) d[i] = INF;
    d[s] = 0;
    for (uint32_t it = 0; it < n; it++) {
        uint32_t u = INF, best = INF;
        for (uint32_t i = 0; i < n; i++) if (!used_[i] && d[i] < best) { best = d[i]; u = i; }
        if (u == INF) break;
        used_[u] = 1;
        for (uint32_t v = 0; v < n; v++)
            if (W[u][v] != INF && best + W[u][v] < d[v]) d[v] = best + W[u][v];
    }
}
static uint64_t prim_dense(uint32_t n, uint32_t *key) {      // 無向，W 對稱；不連通回傳 UINT64_MAX
    uint64_t total = 0;
    memset(used_, 0, n);
    for (uint32_t i = 0; i < n; i++) key[i] = INF;
    key[0] = 0;
    for (uint32_t it = 0; it < n; it++) {
        uint32_t u = INF, best = INF;
        for (uint32_t i = 0; i < n; i++) if (!used_[i] && key[i] < best) { best = key[i]; u = i; }
        if (u == INF) return UINT64_MAX;
        used_[u] = 1; total += best;
        for (uint32_t v = 0; v < n; v++) if (!used_[v] && W[u][v] < key[v]) key[v] = W[u][v];
    }
    return total;
}
//@@ bipartite
// 二分圖判定（無向）：BFS 著色，O(N+M)。side[] 得到兩邊
static int8_t side[MAXN];
static int is_bipartite(uint32_t n) {
    memset(side, -1, n);
    for (uint32_t s = 0; s < n; s++) {
        if (side[s] >= 0) continue;
        uint32_t qh = 0, qt = 0;
        side[s] = 0; que[qt++] = s;
        while (qh < qt) {
            uint32_t u = que[qh++];
            for (uint32_t k = head[u]; k < head[u + 1]; k++) {
                uint32_t v = adj[k];
                if (side[v] < 0) { side[v] = (int8_t)(side[u] ^ 1); que[qt++] = v; }
                else if (side[v] == side[u]) return 0;   // 奇環
            }
        }
    }
    return 1;
}
//@@ scc
// 強連通分量（Tarjan，迭代版）O(N+M)。comp[v] = 分量編號；編號是縮點後 DAG 的「反」拓樸序
static uint32_t tin[MAXN], low[MAXN], comp[MAXN], tstk[MAXN];
static uint8_t onst[MAXN];
static uint32_t scc_tarjan(uint32_t n) {
    uint32_t timer = 0, ncomp = 0, tsp = 0;
    memset(tin, 0xFF, n * sizeof tin[0]);
    memset(onst, 0, n);
    for (uint32_t s = 0; s < n; s++) {
        if (tin[s] != INF) continue;
        uint32_t sp = 0;
        stk[sp++] = s; tin[s] = low[s] = timer++; tstk[tsp++] = s; onst[s] = 1; it_[s] = head[s];
        while (sp) {
            uint32_t u = stk[sp - 1];
            if (it_[u] < head[u + 1]) {
                uint32_t v = adj[it_[u]++];
                if (tin[v] == INF) {
                    tin[v] = low[v] = timer++; tstk[tsp++] = v; onst[v] = 1; it_[v] = head[v]; stk[sp++] = v;
                } else if (onst[v] && tin[v] < low[u]) low[u] = tin[v];
            } else {
                sp--;
                if (sp && low[u] < low[stk[sp - 1]]) low[stk[sp - 1]] = low[u];
                if (low[u] == tin[u]) {
                    uint32_t w;
                    do { w = tstk[--tsp]; onst[w] = 0; comp[w] = ncomp; } while (w != u);
                    ncomp++;
                }
            }
        }
    }
    return ncomp;
}
// 2-SAT：變數 x 用節點 2x（真）、2x+1（假）；子句 (a∨b) 加邊 ¬a→b、¬b→a。
// 有解 ⇔ 沒有 comp[2x]==comp[2x+1]；x 取真 ⇔ comp[2x] < comp[2x+1]
//@@ bridges
// 橋與割點（無向，迭代版）O(N+M)。建 CSR 時把 wt[k] 設成「原始邊編號」，重邊也正確
static uint32_t pe[MAXN];                         // 走進這個點用的邊編號
static uint8_t is_art[MAXN], is_bridge[MAXM / 2];
static void bridges_arts(uint32_t n, uint32_t m_undirected) {
    uint32_t timer = 0;
    memset(tin, 0xFF, n * sizeof tin[0]);
    memset(is_art, 0, n);
    memset(is_bridge, 0, m_undirected);
    for (uint32_t s = 0; s < n; s++) {
        if (tin[s] != INF) continue;
        uint32_t sp = 0, rootkids = 0;
        stk[sp++] = s; tin[s] = low[s] = timer++; pe[s] = INF; it_[s] = head[s];
        while (sp) {
            uint32_t u = stk[sp - 1];
            if (it_[u] < head[u + 1]) {
                uint32_t k = it_[u]++, v = adj[k], e = wt[k];
                if (e == pe[u]) continue;         // 不走回同一條邊
                if (tin[v] == INF) {
                    tin[v] = low[v] = timer++; pe[v] = e; it_[v] = head[v]; stk[sp++] = v;
                    if (u == s) rootkids++;
                } else if (tin[v] < low[u]) low[u] = tin[v];
            } else {
                sp--;
                if (sp) {
                    uint32_t p = stk[sp - 1];
                    if (low[u] < low[p]) low[p] = low[u];
                    if (low[u] > tin[p]) is_bridge[pe[u]] = 1;
                    if (p != s && low[u] >= tin[p]) is_art[p] = 1;
                }
            }
        }
        if (rootkids >= 2) is_art[s] = 1;
    }
}
//@@ lca
// LCA：跳躍指標（jump pointer）版，O(N) 記憶體、每次查詢 O(log N)
// 二進位提升要 N*logN 個整數（N=1e5 就 6.8 MB），MCU 放不下；這個只要 3 個陣列
static uint32_t tpar[MAXN], jmp[MAXN], dep[MAXN];
static void lca_build(uint32_t n, uint32_t root) {   // 樹存成無向 CSR
    uint32_t qh = 0, qt = 0;
    memset(dep, 0xFF, n * sizeof dep[0]);
    dep[root] = 0; tpar[root] = jmp[root] = root; que[qt++] = root;
    while (qh < qt) {                                 // BFS：父節點一定先處理
        uint32_t u = que[qh++];
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k];
            if (dep[v] != INF) continue;
            dep[v] = dep[u] + 1; tpar[v] = u;
            uint32_t j = jmp[u];
            jmp[v] = (dep[u] - dep[j] == dep[j] - dep[jmp[j]]) ? jmp[j] : u;
            que[qt++] = v;
        }
    }
}
static uint32_t lca_up(uint32_t v, uint32_t d) {      // v 往上到深度 d 的祖先
    while (dep[v] > d) v = (dep[jmp[v]] >= d) ? jmp[v] : tpar[v];
    return v;
}
static uint32_t lca(uint32_t u, uint32_t v) {
    if (dep[u] > dep[v]) u = lca_up(u, dep[v]); else v = lca_up(v, dep[u]);
    while (u != v) {
        if (jmp[u] != jmp[v]) { u = jmp[u]; v = jmp[v]; }
        else { u = tpar[u]; v = tpar[v]; }
    }
    return u;
}
// 樹上兩點距離 = dep[u] + dep[v] - 2*dep[lca(u,v)]
//@@ dinic
// 最大流 Dinic O(V^2 E)，單位容量（二分匹配）時是 O(E sqrt V) = Hopcroft–Karp
// 迭代版 DFS（沒有遞迴）。邊 e 與 e^1 互為反向邊
#define FN_ 2048
#define FM_ 40000                                  // 邊數 × 2
#define NIL 0xFFFFFFFFu
static uint32_t fh[FN_], fnx[FM_], fto[FM_], fcap[FM_], fit[FN_], flv[FN_], fq[FN_], fpath[FN_], fne;
static void flow_init(uint32_t n) { memset(fh, 0xFF, n * sizeof fh[0]); fne = 0; }
static void flow_add(uint32_t u, uint32_t v, uint32_t c) {
    fto[fne] = v; fcap[fne] = c; fnx[fne] = fh[u]; fh[u] = fne++;
    fto[fne] = u; fcap[fne] = 0; fnx[fne] = fh[v]; fh[v] = fne++;
}
static uint64_t dinic(uint32_t n, uint32_t s, uint32_t t) {
    uint64_t flow = 0;
    for (;;) {
        uint32_t qh = 0, qt = 0;                     // BFS 分層
        memset(flv, 0xFF, n * sizeof flv[0]);
        flv[s] = 0; fq[qt++] = s;
        while (qh < qt) {
            uint32_t u = fq[qh++];
            for (uint32_t e = fh[u]; e != NIL; e = fnx[e])
                if (fcap[e] && flv[fto[e]] == INF) { flv[fto[e]] = flv[u] + 1; fq[qt++] = fto[e]; }
        }
        if (flv[t] == INF) return flow;
        memcpy(fit, fh, n * sizeof fh[0]);
        for (;;) {                                   // 一次找一條增廣路
            uint32_t u = s, plen = 0;
            while (u != t) {
                uint32_t e = fit[u];
                while (e != NIL && !(fcap[e] && flv[fto[e]] == flv[u] + 1)) e = fnx[e];
                fit[u] = e;
                if (e == NIL) {                      // 死路：標記並退一步
                    flv[u] = INF;
                    if (!plen) break;
                    u = fto[fpath[--plen] ^ 1];
                } else { fpath[plen++] = e; u = fto[e]; }
            }
            if (u != t) break;
            uint32_t f = INF;
            for (uint32_t i = 0; i < plen; i++) if (fcap[fpath[i]] < f) f = fcap[fpath[i]];
            for (uint32_t i = 0; i < plen; i++) { fcap[fpath[i]] -= f; fcap[fpath[i] ^ 1] += f; }
            flow += f;
        }
    }
}
// 二分匹配：s→左邊每點 cap 1，左→右 cap 1，右邊每點→t cap 1，答案 = dinic(s,t)
// 最小割 = 最大流；最後一次 BFS 中 flv[v] != INF 的點就是割的 s 那一側
//@@ grid
// 方格最短路：外圈補一圈牆，就不用檢查邊界；四個方向 = 索引 ±1、±stride
// 不存距離陣列，一層一層 BFS 數層數 → RAM 只要 1 byte/格 + 佇列 2 bytes/格（uint16 索引，(H+2)(W+2) <= 65536）
#define GMAX 65536u
static uint8_t gwall[GMAX];                       // 1 = 牆或已走過（會被改掉，重跑要重設）
static uint16_t gq[GMAX];
static uint32_t grid_bfs(uint32_t H, uint32_t W, uint32_t sr, uint32_t sc, uint32_t tr, uint32_t tc) {
    int32_t S = (int32_t)W + 2;                   // 格子 (r,c) 在 (r+1)*S + (c+1)
    const int32_t dir[4] = {1, -1, S, -S};
    uint32_t s = (sr + 1) * S + sc + 1, t = (tr + 1) * S + tc + 1, qh = 0, qt = 0, layer = 0;
    (void)H;
    if (gwall[s]) return INF;
    gwall[s] = 1; gq[qt++] = (uint16_t)s;
    while (qh < qt) {
        uint32_t end = qt;                        // 這一層到 end 為止
        for (; qh < end; qh++) {
            uint32_t u = gq[qh];
            if (u == t) return layer;
            for (int d = 0; d < 4; d++) {
                uint32_t v = (uint32_t)((int32_t)u + dir[d]);
                if (!gwall[v]) { gwall[v] = 1; gq[qt++] = (uint16_t)v; }
            }
        }
        layer++;
    }
    return INF;
}
//@@ astar
// A*（方格，曼哈頓距離當估計）：只往終點方向展開，空曠地圖展開的格子遠少於 BFS
// 需要上面的 heap/hpush/hpop 與 gwall；g 存已知距離
static uint32_t gdist[GMAX];
static uint32_t astar_expanded;
static uint32_t grid_astar(uint32_t H, uint32_t W, uint32_t sr, uint32_t sc, uint32_t tr, uint32_t tc) {
    int32_t S = (int32_t)W + 2;
    const int32_t dir[4] = {1, -1, S, -S};
    uint32_t s = (sr + 1) * S + sc + 1, t = (tr + 1) * S + tc + 1;
    memset(gdist, 0xFF, (H + 2) * (uint32_t)S * sizeof gdist[0]);
    astar_expanded = 0;
    if (gwall[s]) return INF;
    gdist[s] = 0; hn = 0;
    hpush((uint64_t)((sr > tr ? sr - tr : tr - sr) + (sc > tc ? sc - tc : tc - sc)) << 32 | s);
    while (hn) {
        uint32_t u = (uint32_t)hpop();
        if (u == t) return gdist[t];
        if (gwall[u] == 2) continue;              // 已關閉
        gwall[u] = 2; astar_expanded++;
        uint32_t ur = u / S, uc = u % S;          // 只在展開時算一次
        for (int d = 0; d < 4; d++) {
            uint32_t v = (uint32_t)((int32_t)u + dir[d]);
            if (gwall[v]) continue;
            uint32_t g = gdist[u] + 1;
            if (g < gdist[v]) {
                gdist[v] = g;
                uint32_t vr = ur + (d == 2) - (d == 3), vc = uc + (d == 0) - (d == 1);
                uint32_t h = (vr > tr + 1 ? vr - tr - 1 : tr + 1 - vr) + (vc > tc + 1 ? vc - tc - 1 : tc + 1 - vc);
                hpush((uint64_t)(g + h) << 32 | v);
            }
        }
    }
    return INF;
}
