// Planificador Dieciochero - Tarea 1 Sistemas Operativos (UDP)
// Uso: ./planificador plan.txt K
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct Nodo {
    string id, nombre;
    long ms = 0;
    vector<string> deps;  
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
        string ds = c[3];
        for (char& ch : ds) if (ch == '[' || ch == ']') ch = ' ';
        stringstream ss(ds);
        string tok;
        while (getline(ss, tok, ',')) {
            tok = trim(tok);
            if (!tok.empty()) n.deps.push_back(tok);
        }
        idx[n.id] = (int)nodos.size();
        nodos.push_back(n);
    }
    if (nodos.empty()) error("el plan esta vacio");
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
        for (const string& d : n.deps) printf(" %s", d.c_str());
        printf("\n");
    }
    return 0;
}