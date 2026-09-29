#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

enum Estado { ESPERA, LISTA, EJECUTANDO, TERMINADA, FALLIDA, ABORT_RAMA, ABORT_SEREMI };

struct Nodo {
    string id, nombre;
    long ms = 0;
    vector<int> deps, sucs;
    int pendientes = 0;
    Estado estado = ESPERA;
    pid_t pid = -1;
    int fd_out = -1;
    string msg;
};

static vector<Nodo> nodos;
static volatile sig_atomic_t seremi = 0;
static const size_t MAX_MSG = 200;
static const size_t MAX_INSUMO = 3500;

static void on_sigint(int) { seremi = 1; }

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

static bool debe_fallar(const string& id) {
    const char* lista = getenv("FALLAR");
    if (lista) {
        stringstream ss(lista);
        string t;
        while (getline(ss, t, ','))
            if (trim(t) == id) return true;
    }
    const char* pct = getenv("FALLA_PCT");
    if (pct) {
        mt19937 r((unsigned)getpid() ^ (unsigned)chrono::steady_clock::now().time_since_epoch().count());
        if ((int)(r() % 100) < atoi(pct)) return true;
    }
    return false;
}

[[noreturn]] static void hijo(const Nodo& n, int fd_in, int fd_out) {
    signal(SIGINT, SIG_IGN);
    sigset_t vacio;
    sigemptyset(&vacio);
    sigprocmask(SIG_SETMASK, &vacio, nullptr);

    char in[MAX_INSUMO + 1];
    ssize_t r = read(fd_in, in, MAX_INSUMO);
    if (r < 0) r = 0;
    in[r] = '\0';
    close(fd_in);

    char buf[MAX_MSG + MAX_INSUMO + 128];
    int len = snprintf(buf, sizeof buf, "  [pid %d] INICIA %s (%s) %ld ms | insumos: %s\n",
                       getpid(), n.id.c_str(), n.nombre.c_str(), n.ms, r ? in : "(ninguno)");
    if (len > 0) { ssize_t w = write(STDOUT_FILENO, buf, (size_t)min<int>(len, (int)sizeof buf - 1)); (void)w; }

    struct timespec ts = {n.ms / 1000, (n.ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}

    if (debe_fallar(n.id)) {
        len = snprintf(buf, sizeof buf, "  [pid %d] FALLA %s\n", getpid(), n.id.c_str());
        if (len > 0) { ssize_t w = write(STDOUT_FILENO, buf, (size_t)len); (void)w; }
        _exit(1);
    }

    char msg[MAX_MSG];
    int m = snprintf(msg, sizeof msg, "%s:%s listo;", n.id.c_str(), n.nombre.c_str());
    if (m < 0) m = 0;
    if ((size_t)m >= sizeof msg) m = sizeof msg - 1;
    ssize_t w = write(fd_out, msg, (size_t)m);
    (void)w;
    close(fd_out);
    _exit(0);
}

static vector<int> activos;
static deque<int> listos;
static long n_ok = 0, n_fail = 0, n_rama = 0, n_seremi = 0;

static void marcar_rama(int origen) {
    vector<int> pila = {origen};
    while (!pila.empty()) {
        int u = pila.back();
        pila.pop_back();
        for (int s : nodos[u].sucs)
            if (nodos[s].estado == ESPERA) {
                nodos[s].estado = ABORT_RAMA;
                n_rama++;
                printf("[plan] %s abortada (rama de %s)\n", nodos[s].id.c_str(), nodos[origen].id.c_str());
                pila.push_back(s);
            }
    }
}

static void lanzar(int i) {
    Nodo& n = nodos[i];
    string insumo;
    for (int d : n.deps) {
        if (insumo.size() + nodos[d].msg.size() > MAX_INSUMO) break;
        insumo += nodos[d].msg;
    }
    int pin[2], pout[2];
    if (pipe(pin) < 0) { perror("pipe"); n.estado = FALLIDA; n_fail++; marcar_rama(i); return; }
    if (pipe(pout) < 0) {
        perror("pipe");
        close(pin[0]); close(pin[1]);
        n.estado = FALLIDA; n_fail++; marcar_rama(i); return;
    }
    ssize_t w = write(pin[1], insumo.data(), insumo.size());
    (void)w;
    close(pin[1]);
    fflush(stdout);
    pid_t p = fork();
    if (p < 0) {
        perror("fork");
        close(pin[0]); close(pout[0]); close(pout[1]);
        n.estado = FALLIDA; n_fail++; marcar_rama(i); return;
    }
    if (p == 0) {
        close(pout[0]);
        for (int a : activos) close(nodos[a].fd_out);
        hijo(n, pin[0], pout[1]);
    }
    close(pin[0]);
    close(pout[1]);
    n.pid = p;
    n.fd_out = pout[0];
    n.estado = EJECUTANDO;
    activos.push_back(i);
}

static void finalizar(int pos) {
    int i = activos[pos];
    Nodo& n = nodos[i];
    close(n.fd_out);
    n.fd_out = -1;
    int st = 0;
    while (waitpid(n.pid, &st, 0) < 0 && errno == EINTR) {}
    activos[pos] = activos.back();
    activos.pop_back();

    if (WIFEXITED(st) && WEXITSTATUS(st) == 0 && !n.msg.empty()) {
        n.estado = TERMINADA;
        n_ok++;
        printf("[plan] %s terminada\n", n.id.c_str());
        for (int s : n.sucs)
            if (nodos[s].estado == ESPERA && --nodos[s].pendientes == 0) {
                nodos[s].estado = LISTA;
                listos.push_back(s);
            }
    } else {
        n.estado = FALLIDA;
        n_fail++;
        if (WIFSIGNALED(st))
            printf("[plan] %s FALLIDA (senal %d)\n", n.id.c_str(), WTERMSIG(st));
        else
            printf("[plan] %s FALLIDA (codigo %d)\n", n.id.c_str(), WEXITSTATUS(st));
        marcar_rama(i);
    }
}

static void inspeccion_seremi() {
    printf("\n[SEREMI] Llego la autoridad: abortando todas las actividades\n");
    for (int a : activos) kill(nodos[a].pid, SIGTERM);
    for (int a : activos) {
        int st;
        while (waitpid(nodos[a].pid, &st, 0) < 0 && errno == EINTR) {}
        close(nodos[a].fd_out);
        nodos[a].estado = ABORT_SEREMI;
        n_seremi++;
    }
    activos.clear();
    for (auto& n : nodos)
        if (n.estado == ESPERA || n.estado == LISTA) { n.estado = ABORT_SEREMI; n_seremi++; }
    listos.clear();
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 2;
    }
    char* fin = nullptr;
    long K = strtol(argv[2], &fin, 10);
    if (*fin != '\0' || K < 1) error("K debe ser un entero >= 1");

    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY &&
        K > (long)rl.rlim_cur - 32) {
        K = (long)rl.rlim_cur - 32;
        fprintf(stderr, "Aviso: K limitado a %ld por el limite de descriptores\n", K);
        if (K < 1) error("limite de descriptores insuficiente");
    }

    parsear(argv[1]);
    printf("[plan] %zu actividades, K=%ld\n", nodos.size(), K);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigset_t bloq, orig;
    sigemptyset(&bloq);
    sigaddset(&bloq, SIGINT);
    sigprocmask(SIG_BLOCK, &bloq, &orig);

    for (size_t i = 0; i < nodos.size(); i++)
        if (nodos[i].pendientes == 0) { nodos[i].estado = LISTA; listos.push_back((int)i); }

    vector<pollfd> pfds;
    while (!seremi && (!listos.empty() || !activos.empty())) {
        while (!listos.empty() && (long)activos.size() < K) {
            int i = listos.front();
            listos.pop_front();
            lanzar(i);
        }
        if (activos.empty()) continue;

        pfds.assign(activos.size(), pollfd());
        for (size_t j = 0; j < activos.size(); j++) {
            pfds[j].fd = nodos[activos[j]].fd_out;
            pfds[j].events = POLLIN;
        }
        int r = ppoll(pfds.data(), pfds.size(), nullptr, &orig);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("ppoll");
            break;
        }
        for (int j = (int)pfds.size() - 1; j >= 0; j--) {
            if (!(pfds[j].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            Nodo& n = nodos[activos[j]];
            char buf[MAX_MSG];
            ssize_t k = read(n.fd_out, buf, sizeof buf);
            if (k > 0) {
                if (n.msg.size() + (size_t)k <= MAX_MSG) n.msg.append(buf, (size_t)k);
            } else if (k == 0 || (errno != EINTR && errno != EAGAIN)) {
                finalizar(j);
            }
        }
    }

    if (seremi) inspeccion_seremi();

    printf("\n[resumen] ok=%ld fallidas=%ld abortadas_por_rama=%ld abortadas_por_seremi=%ld\n",
           n_ok, n_fail, n_rama, n_seremi);
    if (seremi) return 130;
    return n_fail ? 1 : 0;
}