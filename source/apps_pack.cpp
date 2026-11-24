#include "apps_pack.hpp"

#include <minizip/unzip.h>
#include <sys/stat.h>
#include <zlib.h>

#include <borealis.hpp>
#include <cstdio>
#include <fstream>
#include <map>

#include "actividad.hpp"
#include "app_updates.hpp"
#include "constants.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "pack_version.hpp"

namespace appsPack {

    namespace {
        constexpr const char ZIP_TEMP[] = "/config/aio-switch-updater/apps_pack.zip";
        constexpr const char LOG[] = "/config/aio-switch-updater/apps_pack.log";
        constexpr std::uint64_t CRC_HASTA = 4u << 20;  // más grande que esto, basta el tamaño (y la versión)

        std::string texto(const nlohmann::ordered_json& j, const char* k)
        {
            const auto it = j.find(k);
            return it != j.end() && it->is_string() ? it->get<std::string>() : "";
        }

        // Una ruta del manifiesto: relativa, sin «..» ni barras invertidas.
        bool rutaSegura(const std::string& r)
        {
            if (r.empty() || r[0] == '/' || r.find('\\') != std::string::npos || r.find(':') != std::string::npos) return false;
            size_t ini = 0;
            while (ini <= r.size()) {
                const size_t fin = r.find('/', ini);
                const std::string parte = r.substr(ini, fin == std::string::npos ? std::string::npos : fin - ini);
                if (parte.empty() || parte == "." || parte == "..") return false;
                if (fin == std::string::npos) break;
                ini = fin + 1;
            }
            return true;
        }

        std::uint32_t crcDe(const std::string& path)
        {
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) return 0;
            std::vector<unsigned char> buf(0x10000);
            uLong crc = crc32(0L, Z_NULL, 0);
            size_t n;
            while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) crc = crc32(crc, buf.data(), n);
            std::fclose(f);
            return static_cast<std::uint32_t>(crc);
        }

        bool existe(const std::string& path, std::uint64_t* tam = nullptr)
        {
            struct stat st {};
            if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
            if (tam) *tam = static_cast<std::uint64_t>(st.st_size);
            return true;
        }

        App leerApp(const std::string& id, const nlohmann::ordered_json& j)
        {
            App a;
            a.id = id;
            a.nombre = texto(j, "nombre");
            a.tipo = texto(j, "tipo");
            a.version = texto(j, "version");
            a.principal = texto(j, "principal");
            a.zip = texto(j, "zip");
            a.catalogo = texto(j, "catalogo");
            if (j.contains("bytes") && j["bytes"].is_number_unsigned()) a.bytes = j["bytes"].get<std::uint64_t>();
            if (j.contains("reinicio") && j["reinicio"].is_boolean()) a.reinicio = j["reinicio"].get<bool>();
            const auto archivos = j.find("archivos");
            if (archivos != j.end() && archivos->is_array()) {
                for (const auto& f : *archivos) {
                    if (!f.is_array() || f.size() < 4 || !f[0].is_string() || !f[1].is_string() || !f[2].is_number_unsigned() || !f[3].is_number_integer()) continue;
                    Archivo x;
                    x.ruta = f[0].get<std::string>();
                    if (!rutaSegura(x.ruta)) continue;
                    x.crc = static_cast<std::uint32_t>(std::strtoul(f[1].get<std::string>().c_str(), nullptr, 16));
                    x.bytes = f[2].get<std::uint64_t>();
                    x.modo = f[3].get<int>();
                    if (f.size() > 4 && f[4].is_string()) x.version = f[4].get<std::string>();
                    a.archivos.push_back(x);
                }
            }
            return a;
        }

        // Si de esta app no hay ningún archivo de los que cuentan.
        bool sinNada(const App& a)
        {
            for (const auto& f : a.archivos)
                if (f.modo == 0 && existe("/" + f.ruta)) return false;
            return true;
        }

        /* Escribe la entrada actual del zip en `destino`: primero a un archivo
           aparte, comprobado contra el CRC del manifiesto, y recién entonces
           reemplaza al de la SD. Un corte a la mitad deja el viejo. */
        bool escribir(unzFile z, const std::string& destino, std::uint32_t crcEsperado, std::uint64_t tamEsperado)
        {
            fs::createTree(destino.substr(0, destino.rfind('/') + 1));
            const std::string temp = destino + ".oqbnuevo";
            if (unzOpenCurrentFile(z) != UNZ_OK) return false;
            FILE* out = std::fopen(temp.c_str(), "wb");
            if (!out) {
                unzCloseCurrentFile(z);
                return false;
            }
            std::vector<unsigned char> buf(0x10000);
            uLong crc = crc32(0L, Z_NULL, 0);
            std::uint64_t total = 0;
            bool ok = true;
            int n;
            while ((n = unzReadCurrentFile(z, buf.data(), buf.size())) > 0) {
                if (std::fwrite(buf.data(), 1, n, out) != static_cast<size_t>(n)) ok = false;
                crc = crc32(crc, buf.data(), n);
                total += n;
            }
            if (n < 0) ok = false;
            if (std::fclose(out) != 0) ok = false;
            if (unzCloseCurrentFile(z) != UNZ_OK) ok = false;
            if (!ok || total != tamEsperado || static_cast<std::uint32_t>(crc) != crcEsperado) {
                std::remove(temp.c_str());
                return false;
            }
            std::remove(destino.c_str());
            if (std::rename(temp.c_str(), destino.c_str()) != 0) {
                std::remove(temp.c_str());
                return false;
            }
            return true;
        }
    }  // namespace

    nlohmann::ordered_json bajarManifiesto()
    {
        nlohmann::ordered_json m;
        if (existe(APPS_PACK_LOCAL))
            m = fs::parseJsonFile(APPS_PACK_LOCAL);
        else
            download::getRequest(APPS_PACK_URL, m);
        return m.is_object() && m.contains("packs") && m.contains("apps") ? m : nlohmann::ordered_json::object();
    }

    void releerEstado(App& a)
    {
        bool alguno = false, distinto = false;
        int faltan = 0;
        a.tengo.clear();
        for (const auto& f : a.archivos) {
            if (f.modo != 0) continue;
            const std::string path = "/" + f.ruta;
            std::uint64_t tam = 0;
            if (!existe(path, &tam)) {
                ++faltan;
                continue;
            }
            alguno = true;
            if (!f.version.empty()) {
                const std::string v = appUpdates::versionDeNro(path);
                if (f.ruta == a.principal) a.tengo = v;
                /* Con versión, manda la versión: una más nueva instalada aparte
                   (desde Apps) no se ofrece «actualizar» a la del pack. */
                if (!v.empty()) {
                    if (appUpdates::esMasNueva(f.version, v)) distinto = true;
                    continue;
                }
            }
            if (tam != f.bytes)
                distinto = true;
            else if (f.bytes <= CRC_HASTA && crcDe(path) != f.crc)
                distinto = true;
        }
        a.estado = !alguno ? SinInstalar : faltan ? Incompleta : distinto ? Nueva : AlDia;
    }

    std::vector<App> delPackActual(const nlohmann::ordered_json& m)
    {
        std::vector<App> out;
        if (!m.is_object()) return out;
        const std::string pack = packVersion::current();
        const auto packs = m.find("packs");
        const auto apps = m.find("apps");
        if (pack.empty() || packs == m.end() || apps == m.end() || !packs->is_object() || !apps->is_object()) return out;
        const auto lista = packs->find(pack);
        if (lista == packs->end() || !lista->is_array()) return out;
        for (const auto& id : *lista) {
            if (!id.is_string()) continue;
            const auto j = apps->find(id.get<std::string>());
            if (j == apps->end() || !j->is_object()) continue;
            App a = leerApp(id.get<std::string>(), *j);
            if (a.nombre.empty() || a.zip.empty() || a.archivos.empty()) continue;
            releerEstado(a);
            out.push_back(std::move(a));
        }
        return out;
    }

    Resultado instalar(const nlohmann::ordered_json& m, const std::vector<App>& apps)
    {
        Resultado r;
        const std::string base = m.is_object() ? texto(m, "base") : "";
        std::ofstream log(LOG, std::ios::app);
        for (const App& a : apps) {
            actividad::paso(a.nombre);
            const bool nueva = sinNada(a);
            bool ok = !base.empty();
            int escritos = 0;
            if (ok) ok = download::downloadFile(base + a.zip, ZIP_TEMP, OFF) == 200;
            unzFile z = ok ? unzOpen(ZIP_TEMP) : nullptr;
            if (ok && !z) ok = false;
            if (z) {
                std::map<std::string, const Archivo*> porRuta;
                for (const auto& f : a.archivos) porRuta[f.ruta] = &f;
                for (int rc = unzGoToFirstFile(z); ok && rc == UNZ_OK; rc = unzGoToNextFile(z)) {
                    char nombre[0x301] = {};
                    unz_file_info info {};
                    if (unzGetCurrentFileInfo(z, &info, nombre, sizeof(nombre) - 1, nullptr, 0, nullptr, 0) != UNZ_OK) {
                        ok = false;
                        break;
                    }
                    const auto it = porRuta.find(nombre);
                    if (it == porRuta.end()) continue;  // lo que el manifiesto no nombra, no se escribe
                    const Archivo& f = *it->second;
                    const std::string destino = "/" + f.ruta;
                    if (f.modo == 1 && existe(destino)) continue;
                    if (f.modo == 2 && !nueva) continue;
                    actividad::detalle(f.ruta);
                    if (!escribir(z, destino, f.crc, f.bytes)) {
                        ok = false;
                        if (log) log << a.id << ": no quedó bien " << f.ruta << "\n";
                        break;
                    }
                    ++escritos;
                }
                unzClose(z);
            }
            std::remove(ZIP_TEMP);
            if (ok) {
                ++r.bien;
                if (a.reinicio && escritos) r.reiniciar = true;
            }
            else {
                ++r.mal;
                r.fallaron.push_back(a.nombre);
            }
            if (log) log << a.id << (ok ? " ok " : " MAL ") << escritos << " archivos\n";
        }
        appUpdates::avisarInstalacion();
        return r;
    }

}  // namespace appsPack
