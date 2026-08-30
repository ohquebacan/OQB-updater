// Copia literal de la lógica de matching de fs::removeDirContentsExcept
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>

static std::string normalize(const std::string& p) {
    std::string out; out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(c);
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

static bool isPathPrefix(const std::string& prefix, const std::string& p) {
    if (prefix.empty() || prefix.size() > p.size()) return false;
    if (!std::equal(prefix.begin(), prefix.end(), p.begin(), [](char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); })) return false;
    return prefix.size() == p.size() || p[prefix.size()] == '/';
}

static bool preserved(const std::set<std::string>& keep, const std::string& entry) {
    std::set<std::string> kn;
    for (const auto& k : keep) if (!k.empty()) kn.insert(normalize(k));
    const std::string e = normalize(entry);
    for (const auto& k : kn) if (isPathPrefix(k, e) || isPathPrefix(e, k)) return true;
    return false;
}

static int fails = 0;
static void check(const char* what, bool got, bool want) {
    printf("%-6s %s\n", got == want ? "ok" : "FALLA", what);
    if (got != want) fails++;
}

int main() {
    const std::set<std::string> keep{"/atmosphere/contents/0100B00B51230000"};

    check("preserva la carpeta exacta",
          preserved(keep, "/atmosphere/contents/0100B00B51230000"), true);
    check("preserva con minusculas en disco",
          preserved(keep, "/atmosphere/contents/0100b00b51230000"), true);
    check("preserva con barra final",
          preserved(keep, "/atmosphere/contents/0100B00B51230000/"), true);
    check("preserva con barra doble",
          preserved(keep, "/atmosphere/contents//0100B00B51230000"), true);
    check("preserva contenido interno",
          preserved(keep, "/atmosphere/contents/0100B00B51230000/romfs"), true);
    check("BORRA otra carpeta cualquiera",
          preserved(keep, "/atmosphere/contents/0100000000001000"), false);
    check("BORRA un tid que comparte prefijo largo",
          preserved(keep, "/atmosphere/contents/0100B00B5123000F"), false);
    check("BORRA un tid mas largo que empieza igual",
          preserved(keep, "/atmosphere/contents/0100B00B51230000FF"), false);

    // preserve.txt escrito con minusculas, como lo paso el usuario
    const std::set<std::string> lower{"/atmosphere/contents/0100b00b51230000"};
    check("preserve.txt en minusculas vs disco en mayusculas",
          preserved(lower, "/atmosphere/contents/0100B00B51230000"), true);

    // un archivo profundo listado mantiene los directorios intermedios
    const std::set<std::string> deep{"/atmosphere/contents/ABC/flags/boot2.flag"};
    check("mantiene el directorio intermedio",
          preserved(deep, "/atmosphere/contents/ABC"), true);

    // listar un prefijo corto no debe salvar todo
    const std::set<std::string> risky{"/atmosphere/contents/0100"};
    check("un prefijo corto NO salva todo",
          preserved(risky, "/atmosphere/contents/0100B00B51230000"), false);

    printf("\n%s (%d fallas)\n", fails ? "HAY FALLAS" : "TODO OK", fails);
    return fails != 0;
}
