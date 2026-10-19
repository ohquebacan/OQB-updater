// Pruebas del certificado de cliente contra un vector que genero la pagina
// (oqb-cliente.js con la clave real), asi se comprueba que la app y la pagina
// hablan el mismo formato.
//
//   c++ -std=c++17 -Iinclude -Iinclude/vendor -Ilib/borealis/library/include/borealis/extern/nlohmann \
//       -I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib -lcrypto \
//       -o /tmp/cl tests/cliente_test.cpp source/cliente.cpp source/vendor/monocypher.c source/vendor/monocypher-ed25519.c
//   /tmp/cl vector.json

#include "cliente.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    int fallas = 0;
    const auto check = [&fallas](bool ok, const char* que) {
        std::cout << (ok ? "ok     " : "FALLA  ") << que << "\n";
        if (!ok) ++fallas;
    };

    std::ifstream f(argc > 1 ? argv[1] : "vector.json");
    const auto v = nlohmann::ordered_json::parse(f);
    const std::string id = v["id"];
    const auto cert = v["cert"];

    check(cliente::codigoDe(id) == v["codigo"].get<std::string>(), "el codigo coincide con el de la pagina");
    check(cliente::archivoDe(id) == v["archivo"].get<std::string>(), "el nombre del archivo coincide con el de la pagina");

    std::string nombre;
    check(cliente::verificar(id, cert, nombre) && nombre == "José Pérez",
          "verifica el certificado de la pagina y descifra el nombre con tildes");

    std::string otro;
    check(!cliente::verificar("0123456789abcdee", cert, otro), "copiado a otra consola no vale");

    auto alterado = cert;
    alterado["f"] = "2030-01-01";
    check(!cliente::verificar(id, alterado, otro), "cambiar la fecha rompe la firma");

    alterado = cert;
    alterado["s"] = "AAAA";
    check(!cliente::verificar(id, alterado, otro), "una firma basura no vale");

    const std::string url = cliente::urlSoporte({{"id", id}, {"n", nombre}});
    check(url.rfind("https://ohquebacan.github.io/oqb-clientes/#s=", 0) == 0, "el enlace de soporte apunta a la pagina");

    std::cout << (fallas ? "\nHAY FALLAS\n" : "\nTODO OK (0 fallas)\n");
    return fallas ? 1 : 0;
}
