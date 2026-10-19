#include "cliente.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

#include "monocypher-ed25519.h"

#ifdef __SWITCH__
#include <switch.h>

#include "constants.hpp"
#include "download.hpp"
#include "fs.hpp"
#else
#include <openssl/evp.h>
#include <openssl/sha.h>
#endif

namespace cliente {

    namespace {

        // La publica de OQB. La privada solo la tiene Emilio.
        constexpr const char CLAVE_PUBLICA_HEX[] = "2635d56a138ce071923af20bdf303348f4d51413cae7b3959afaf6a22ec301ed";
        constexpr const char PAGINA[] = "https://ohquebacan.github.io/oqb-clientes/";

        std::vector<uint8_t> sha256(const std::string& texto)
        {
            std::vector<uint8_t> h(32);
#ifdef __SWITCH__
            sha256CalculateHash(h.data(), texto.data(), texto.size());
#else
            SHA256(reinterpret_cast<const unsigned char*>(texto.data()), texto.size(), h.data());
#endif
            return h;
        }

        // AES-128-CTR con el contador entero de 16 bytes, igual que WebCrypto
        // con length 64: los 8 bytes bajos empiezan en cero y un nombre nunca
        // llega a desbordarlos.
        std::vector<uint8_t> aesCtr(const uint8_t key[16], const uint8_t iv[16], const std::vector<uint8_t>& datos)
        {
            std::vector<uint8_t> out(datos.size());
#ifdef __SWITCH__
            Aes128CtrContext ctx;
            aes128CtrContextCreate(&ctx, key, iv);
            aes128CtrCrypt(&ctx, out.data(), datos.data(), datos.size());
#else
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            int n = 0;
            EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key, iv);
            EVP_DecryptUpdate(ctx, out.data(), &n, datos.data(), static_cast<int>(datos.size()));
            EVP_CIPHER_CTX_free(ctx);
#endif
            return out;
        }

        std::string hex(const std::vector<uint8_t>& b, size_t n)
        {
            static const char* d = "0123456789abcdef";
            std::string s;
            for (size_t i = 0; i < n && i < b.size(); ++i) {
                s += d[b[i] >> 4];
                s += d[b[i] & 15];
            }
            return s;
        }

        bool deHex(const std::string& s, std::vector<uint8_t>& out)
        {
            if (s.size() % 2) return false;
            out.clear();
            for (size_t i = 0; i < s.size(); i += 2) {
                unsigned v;
                if (std::sscanf(s.c_str() + i, "%2x", &v) != 1) return false;
                out.push_back(static_cast<uint8_t>(v));
            }
            return true;
        }

        // base64 normal y base64url, con o sin relleno.
        bool deBase64(const std::string& s, std::vector<uint8_t>& out)
        {
            out.clear();
            uint32_t acc = 0;
            int bits = 0;
            for (char c : s) {
                int v;
                if (c >= 'A' && c <= 'Z') v = c - 'A';
                else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
                else if (c >= '0' && c <= '9') v = c - '0' + 52;
                else if (c == '+' || c == '-') v = 62;
                else if (c == '/' || c == '_') v = 63;
                else if (c == '=' || c == '\n' || c == '\r') continue;
                else return false;
                acc = (acc << 6) | static_cast<uint32_t>(v);
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
                }
            }
            return true;
        }

        std::string base64url(const std::string& datos)
        {
            static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            std::string out;
            uint32_t acc = 0;
            int bits = 0;
            for (unsigned char c : datos) {
                acc = (acc << 8) | c;
                bits += 8;
                while (bits >= 6) {
                    bits -= 6;
                    out += t[(acc >> bits) & 63];
                }
            }
            if (bits > 0) out += t[(acc << (6 - bits)) & 63];
            return out;
        }

        std::string texto(const nlohmann::ordered_json& j, const char* clave)
        {
            const auto it = j.find(clave);
            return (it != j.end() && it->is_string()) ? it->get<std::string>() : "";
        }

    }  // namespace

    std::string codigoDe(const std::string& idhex)
    {
        std::string g = idhex + hex(sha256("oqb-codigo:" + idhex), 1);
        for (auto& c : g) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (g.size() != 18) return g;
        return g.substr(0, 4) + "-" + g.substr(4, 4) + "-" + g.substr(8, 4) + "-" + g.substr(12, 4) + "-" + g.substr(16);
    }

    std::string archivoDe(const std::string& idhex)
    {
        return hex(sha256("oqb-cliente-archivo:" + idhex), 16);
    }

    bool verificar(const std::string& idhex, const nlohmann::ordered_json& cert, std::string& nombre)
    {
        if (!cert.is_object()) return false;
        std::vector<uint8_t> iv, cifrado, firma, publica;
        if (!deBase64(texto(cert, "iv"), iv) || iv.size() != 16) return false;
        if (!deBase64(texto(cert, "n"), cifrado) || cifrado.empty()) return false;
        if (!deBase64(texto(cert, "s"), firma) || firma.size() != 64) return false;
        if (!deHex(CLAVE_PUBLICA_HEX, publica) || publica.size() != 32) return false;
        const std::string fecha = texto(cert, "f");
        if (fecha.empty()) return false;

        const auto clave = sha256("oqb-cliente-nombre:" + idhex);
        const auto plano = aesCtr(clave.data(), iv.data(), cifrado);
        const std::string n(plano.begin(), plano.end());

        const std::string mensaje = "OQB-CLIENTE-1\n" + idhex + "\n" + n + "\n" + fecha;
        if (crypto_ed25519_check(firma.data(), publica.data(), reinterpret_cast<const uint8_t*>(mensaje.data()), mensaje.size()) != 0)
            return false;

        nombre = n;
        return true;
    }

    std::string urlSoporte(const nlohmann::ordered_json& datos)
    {
        return std::string(PAGINA) + "#s=" + base64url(datos.dump());
    }

#ifdef __SWITCH__
    std::string idConsola()
    {
        u64 id = 0;
        if (R_FAILED(splGetConfig(SplConfigItem_DeviceId, &id)) || id == 0) return "";
        char buf[17];
        std::snprintf(buf, sizeof(buf), "%016lx", id);
        return buf;
    }

    Estado cargar()
    {
        Estado e;
        e.idhex = idConsola();
        if (e.idhex.empty() || !std::filesystem::exists(CLIENTE_PATH)) return e;

        const nlohmann::ordered_json cert = fs::parseJsonFile(CLIENTE_PATH);
        std::string nombre;
        if (verificar(e.idhex, cert, nombre)) {
            e.valido = true;
            e.nombre = nombre;
            e.fecha = texto(cert, "f");
            e.firma = texto(cert, "s");
        }
        return e;
    }

    Activacion activar(Estado& out)
    {
        out = Estado{};
        out.idhex = idConsola();
        if (out.idhex.empty()) return Activacion::Invalida;

        /* raw.githubusercontent guarda en cache unos minutos, tambien los 404:
           sin el parametro, una consola que probo antes de que la registraran
           seguiria viendo "no registrada" un rato. */
        const std::string url = "https://raw.githubusercontent.com/ohquebacan/oqb-clientes/main/l/" +
                                archivoDe(out.idhex) + ".json?t=" + std::to_string(std::time(nullptr));
        nlohmann::ordered_json cert;
        const long status = download::getRequest(url, cert);
        if (status == 404) return Activacion::NoRegistrada;
        if (status != 200) return Activacion::SinRed;

        std::string nombre;
        if (!verificar(out.idhex, cert, nombre)) return Activacion::Invalida;

        fs::createTree(std::filesystem::path(CLIENTE_PATH).parent_path().string() + "/");
        fs::writeJsonToFile(cert, CLIENTE_PATH);

        out.valido = true;
        out.nombre = nombre;
        out.fecha = texto(cert, "f");
        out.firma = texto(cert, "s");
        return Activacion::Ok;
    }
#endif

}  // namespace cliente
