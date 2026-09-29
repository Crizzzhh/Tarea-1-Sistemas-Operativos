#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct Nodo {
    string id, nombre;
    long ms = 0;
    vector<int> deps, sucs;
    int pendientes = 0;
};

static vector<Nodo> nodos;

static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

[[noreturn]] static void error(const string& m) {
    fprintf(stderr, "Error: %s\n", m.c_str());
    exit(2);
}

static void parsear(const string& ruta) {
    ifstream f(ruta);
    if (!f) error("no se puede abrir " + ruta);
    mt19937 rng(random_device{}());
    uniform_int_distribution<long> dist(100, 5000);

    unordered_map<string, int> idx;
    vector<vector<string>> deps_txt;
    string linea;
    int nlin = 0;
    while (getline(f, linea)) {
        nlin++;
        string l = trim(linea);
        if (l.empty() || l[0] == '#') continue;

        vector<string> c(4);
        size_t pos = 0;
        for (int k = 0; k < 3 && pos != string::npos; k++) {
            size_t p = l.find(':', pos);
            if (p == string::npos) { c[k] = trim(l.substr(pos)); pos = string::npos; }
            else { c[k] = trim(l.substr(pos, p - pos)); pos = p + 1; }
        }
        if (pos != string::npos) c[3] = trim(l.substr(pos));

        Nodo n;
        n.id = c[0];
        n.nombre = c[1];
        if (n.id.empty()) error("linea " + to_string(nlin) + ": ID vacio");
        if (idx.count(n.id)) error("linea " + to_string(nlin) + ": ID duplicado '" + n.id + "'");
        if (c[2].empty()) {
            n.ms = dist(rng);
        } else {
            char* fin = nullptr;
            long v = strtol(c[2].c_str(), &fin, 10);
            if (*fin != '\0' || v <= 0)
                error("linea " + to_string(nlin) + ": tiempo invalido '" + c[2] + "'");
            n.ms = v;
        }
        vector<string> d;
        string ds = c[3];
        for (char& ch : ds) if (ch == '[' || ch == ']') ch = ' ';
        stringstream ss(ds);
        string tok;
        while (getline(ss, tok, ',')) {
            tok = trim(tok);
            if (!tok.empty()) d.push_back(tok);
        }
        idx[n.id] = (int)nodos.size();
        nodos.push_back(n);
        deps_txt.push_back(d);
    }
    if (nodos.empty()) error("el plan esta vacio");

    for (size_t i = 0; i < nodos.size(); i++) {
        set<int> u;
        for (auto& d : deps_txt[i]) {
            auto it = idx.find(d);
            if (it == idx.end())
                error("actividad '" + nodos[i].id + "' depende de '" + d + "' que no existe");
            u.insert(it->second);
        }
        nodos[i].deps.assign(u.begin(), u.end());
        nodos[i].pendientes = (int)u.size();
        for (int d : u) nodos[d].sucs.push_back((int)i);
    }

    vector<int> pend(nodos.size()), cola;
    for (size_t i = 0; i < nodos.size(); i++) {
        pend[i] = nodos[i].pendientes;
        if (!pend[i]) cola.push_back((int)i);
    }
    size_t vistos = 0;
    while (vistos < cola.size()) {
        int u = cola[vistos++];
        for (int s : nodos[u].sucs)
            if (--pend[s] == 0) cola.push_back(s);
    }
    if (vistos != nodos.size()) error("el plan contiene un ciclo: no es un DAG");
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 2;
    }
    parsear(argv[1]);
    printf("[plan] %zu actividades leidas\n", nodos.size());
    for (const Nodo& n : nodos) {
        printf("  %s | %s | %ld ms | deps:", n.id.c_str(), n.nombre.c_str(), n.ms);
        for (int d : n.deps) printf(" %s", nodos[d].id.c_str());
        printf("\n");
    }
    return 0;
}