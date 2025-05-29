#include "bits/stdc++.h"

using namespace std;

void input(int &n, vector<vector<int> > &edges, vector<int> &color) {
    int m;
    cin >> n;
    cin >> m;
    edges.resize(n);
    color.resize(n);
    for (int i = 0; i < m; i++) {
        int a, b;
        cin >> a >> b;
        a--, b--;
        edges[a].push_back(b);
        edges[b].push_back(a);
    }
}

void outputGraph(int &n, vector<vector<int> > &edges) {
    cout << n << endl;
    for (int i = 0; i < n; i++) {
        for (auto to: edges[i]) {
            if (i < to) {
                cout << i << " " << to << endl;
            }
        }
    }
}

void coloring4(int &n, vector<vector<int> > &edges, vector<int> &color) {
    vector<bool> used(n);
    vector<int> nodes, deg(n);
    set<pair<int, int> > st;
    for (int i = 0; i < n; i++) {
        deg[i] = edges[i].size();
        st.insert({deg[i], i});
    }
    while (!st.empty()) {
        int v = (*st.begin()).second;
        st.erase(st.begin());
        nodes.push_back(v);
        used[v] = 1;
        for (auto to: edges[v]) {
            if (used[to] == 1) {
                continue;
            }
            st.erase({deg[to], to});
            deg[to]--;
            st.insert({deg[to], to});
        }
    }
    for (int i = nodes.size() - 1; i >= 0; i--) {
        int v = nodes[i];
        set<int> c = {1, 2, 3, 4, 5};
        for (auto to: edges[v]) {
            if (used[to] == 1) {
                continue;
            }
            c.erase(color[to]);
        }
        used[v] = 0;
        color[v] = *c.begin();
        //
        if (color[v] == 5) {
            color[v] = 4;
        }
        //
    }
}

void mergeColor(int &n, vector<vector<int> > &edges, vector<int> &color) {
    int cnt[5][5];
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            cnt[i][j] = 0;
        }
    }
    for (int i = 0; i < n; i++) {
        for (auto to: edges[i]) {
            int mnC = min(color[i], color[to]), mxC = max(color[i], color[to]);
            cnt[mnC][mxC]++;
        }
    }
    int mx = 3 * n, color1 = 0, color2 = 0;
    for (int i = 1; i <= 4; i++) {
        for (int j = i + 1; j <= 4; j++) {
            if (mx > cnt[i][j]) {
                mx = cnt[i][j];
                color1 = i;
                color2 = j;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (color[i] == color2) {
            color[i] = color1;
        }
        if (color[i] > color2) {
            color[i]--;
        }
    }
}

void coloring3(int &n,  vector<vector<int> > &edges, vector<int> &color) {
    coloring4(n, edges, color);
    mergeColor(n, edges, color);
}

void randPlanar(int &n,  vector<vector<int> > &edges, vector<int> &color) {
    n = (rand() % 11) + 3;
    color.clear();
    color.resize(n);
    edges.clear();
    edges.resize(n);
    vector<int> circle = {0, 1, 2};
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 3; j++) {
            edges[i].push_back(j);
            edges[j].push_back(i);
        }
    }
    for (int i = 3; i < n; i++) {
        int sz = circle.size();
        int l = rand() % sz, r = rand() % sz;
        if (l == r) {
            r++;
            r %= sz;
        }
        int cntE;
        if (r > l) {
            cntE = (r - l - 1);
        } else {
            cntE = {(n + r) - l - 1};
        }
        int cnt = rand() % (cntE + 1);

        vector<int> forRand;
        for (int j = 0; j < circle.size(); j++) {
            if ((l < j && j < r) || (r < l && (j > l || j < r))) {
                forRand.push_back(circle[j]);
            }
        }
        for (int j = 0; j < cnt; j++) {
            if (forRand.size() == 0) {
                break;
            }
            int x = rand() % forRand.size();
            edges[i].push_back(forRand[x]);
            edges[forRand[x]].push_back(i);
            vector<int> nw;
            for (int k = 0; k < forRand.size(); k++) {
                if (k == x) {
                    continue;
                }
                nw.push_back(forRand[k]);
            }
            forRand = nw;
        }
        vector<int> nw;
        edges[i].push_back(circle[l]);
        edges[circle[l]].push_back(i);
        edges[i].push_back(circle[r]);
        edges[circle[r]].push_back(i);

        for (int j = 0; j < circle.size(); j++) {
            if (!((l < j && j < r) || (r < l && (j > l || j < r)))) {
                nw.push_back(circle[j]);
            }
            if (j == l) {
                nw.push_back(i);
            }
        }
        circle = nw;
    }
}


void bruteForce(int &n, vector<vector<int> > &edges, vector<int> &color) {
    int mn = 3 * n;
    for (int mask = 0; mask < (1 << n); mask++) {
        int subMask = mask;
        while (subMask > 0) {
            vector<int> ans(n);
            for (int i = 0; i < n; i++) {
                if (!(mask & (1 << i))) {
                    ans[i] = 1;
                } else
                if (subMask & (1 << i)) {
                    ans[i] = 2;
                } else {
                    ans[i] = 3;
                }
            }
            int res = 0;
            for (int v = 0; v < n; v++) {
                for (auto to: edges[v]) {
                    if (ans[v] == ans[to]) {
                        res++;
                    }
                }
            }
            res /= 2;
            if (mn > res) {
                mn = res;
                color = ans;
            }
            subMask = (subMask - 1) & mask;
        }
    }
}

// compare Main Solution and Brute Force in 100%.   (ansMain - ansBruteForces) / (|E| / 100)
double compare(int &n, vector<vector<int> > &edges) {
    vector<int> colorMain(n), colorBrute(n);
    coloring3(n, edges, colorMain);
    bruteForce(n, edges, colorBrute);
    double E = 0, unicolorMain = 0, unicolorBrute = 0;
    for (int v = 0; v < n; v++) {
        for (auto to: edges[v]) {
            E++;
            if (colorMain[v] == colorMain[to]) {
                unicolorMain++;
            }
            if (colorBrute[v] == colorBrute[to]) {
                unicolorBrute++;
            }
        }
    }
    E /= 2;
    unicolorBrute /= 2;
    unicolorMain /= 2;

    cout << "M color = ";
    for (int i = 0; i < n; i++) {
        cout << colorMain[i] << " ";
    }
    cout << endl << "B color = ";
    for (int i = 0; i < n; i++) {
        cout << colorBrute[i] << " ";
    } cout << endl;
    cout << "E = " << E << " " << "bruteForces unicolor = " << unicolorBrute << " " << "Main solution unicolor = " << unicolorMain << endl;
    if (unicolorMain > E / 6) {
        cout << "count unicolor > E/6" << endl;
        assert(unicolorMain > E / 6);
    }
    return (unicolorMain - unicolorBrute) * (100 / E);
}

double printRandTest() {
    int n;
    vector<int> color;
    vector<vector<int> > edges;
    randPlanar(n, edges, color);
    outputGraph(n, edges);
    double d = compare(n, edges);
    cout << fixed << setprecision(6) << d << "% is diff" << endl;
    return d;
}

void test() {
    double av = 0;
    for (int i = 0; i < 100; i++) {
        av += printRandTest();
    }
    cout << av / 100 << endl;
}

int main() {
    srand(time(NULL));
    printRandTest();
    // test();
    return 0;
}
// 0.827251
