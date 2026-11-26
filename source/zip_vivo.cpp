#include "zip_vivo.hpp"

#include <switch.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

#include "actividad.hpp"
#include "constants.hpp"
#include "download.hpp"
#include "fs.hpp"

namespace zipvivo {

    namespace {

        std::uint16_t u16(const std::string& s, size_t i)
        {
            std::uint16_t v;
            std::memcpy(&v, s.data() + i, 2);
            return v;
        }
        std::uint32_t u32(const std::string& s, size_t i)
        {
            std::uint32_t v;
            std::memcpy(&v, s.data() + i, 4);
            return v;
        }
        std::uint64_t u64(const std::string& s, size_t i)
        {
            std::uint64_t v;
            std::memcpy(&v, s.data() + i, 8);
            return v;
        }

        std::string minusculas(std::string s)
        {
            for (auto& c : s) c = std::tolower(static_cast<unsigned char>(c));
            return s;
        }

        // Lo mismo que extract::detectWrapperDir, pero con los nombres ya leidos.
        std::string envoltorio(const std::vector<std::string>& nombres)
        {
            static const std::set<std::string> raizSd = {
                "atmosphere", "bootloader", "switch", "config", "Nintendo", "emuMMC",
                "sept", "warmboot_mariko", "payloads", "themes", "games", "ReiNX", "sxos",
            };
            std::string candidato;
            for (const auto& n : nombres) {
                const auto barra = n.find('/');
                if (barra == std::string::npos || barra == 0) return "";
                const std::string arriba = n.substr(0, barra);
                if (candidato.empty()) candidato = arriba;
                else if (candidato != arriba) return "";
            }
            if (candidato.empty() || raizSd.count(candidato)) return "";
            return candidato + "/";
        }

        constexpr size_t BLOQUE = 512 * 1024;

        // El estado de la descarga: en que archivo va y que parte de el.
        class Escritor
        {
        public:
            explicit Escritor(const std::vector<Entrada>& e) : entradas(e), salida(BLOQUE), ioBuf(BLOQUE) {}
            ~Escritor()
            {
                this->cerrar();
            }

            bool recibir(const std::uint8_t* datos, size_t n)
            {
                while (n > 0) {
                    if (this->k >= this->entradas.size()) {
                        // El indice del final: ya no hace falta.
                        this->pos += n;
                        return true;
                    }
                    const Entrada& e = this->entradas[this->k];
                    switch (this->estado) {
                        case Estado::Saltar: {
                            if (this->pos > e.posicion) return this->falla(STATUS_CORRUPT_ARCHIVE);
                            const std::uint64_t falta = e.posicion - this->pos;
                            const size_t t = std::min<std::uint64_t>(falta, n);
                            this->avanzar(datos, n, t);
                            if (this->pos == e.posicion) {
                                this->estado = Estado::Cabecera;
                                this->cabecera.clear();
                            }
                            break;
                        }
                        case Estado::Cabecera: {
                            const size_t t = std::min<size_t>(30 - this->cabecera.size(), n);
                            this->cabecera.append(reinterpret_cast<const char*>(datos), t);
                            this->avanzar(datos, n, t);
                            if (this->cabecera.size() == 30) {
                                if (u32(this->cabecera, 0) != 0x04034b50) return this->falla(STATUS_CORRUPT_ARCHIVE);
                                this->saltarCabecera = u16(this->cabecera, 26) + u16(this->cabecera, 28);
                                this->estado = Estado::Nombre;
                            }
                            break;
                        }
                        case Estado::Nombre: {
                            const size_t t = std::min<std::uint64_t>(this->saltarCabecera, n);
                            this->saltarCabecera -= t;
                            this->avanzar(datos, n, t);
                            if (this->saltarCabecera == 0 && !this->abrir(e)) return false;
                            break;
                        }
                        case Estado::Datos: {
                            const size_t t = std::min<std::uint64_t>(this->restante, n);
                            if (!this->escribir(e, datos, t)) return false;
                            this->restante -= t;
                            this->avanzar(datos, n, t);
                            if (this->restante == 0 && !this->terminar(e)) return false;
                            break;
                        }
                    }
                }
                return true;
            }

            bool completo() const { return this->k >= this->entradas.size(); }
            long error() const { return this->codigo; }

        private:
            enum class Estado { Saltar, Cabecera, Nombre, Datos };
            const std::vector<Entrada>& entradas;
            size_t k = 0;
            std::uint64_t pos = 0, saltarCabecera = 0, restante = 0, escritos = 0;
            Estado estado = Estado::Saltar;
            std::string cabecera;
            FILE* out = nullptr;
            bool inflando = false;
            z_stream z{};
            uLong crc = 0;
            std::vector<std::uint8_t> salida;
            std::vector<char> ioBuf;
            long codigo = 0;

            void avanzar(const std::uint8_t*& datos, size_t& n, size_t t)
            {
                datos += t;
                n -= t;
                this->pos += t;
            }

            bool falla(long c)
            {
                this->codigo = c;
                this->cerrar();
                return false;
            }

            void cerrar()
            {
                if (this->inflando) inflateEnd(&this->z);
                this->inflando = false;
                if (this->out) fclose(this->out);
                this->out = nullptr;
            }

            bool abrir(const Entrada& e)
            {
                this->crc = crc32(0L, Z_NULL, 0);
                this->escritos = 0;
                this->restante = e.comprimido;
                const std::string ruta = std::string(ROOT_PATH) + e.nombre;
                if (!e.nombre.empty() && e.nombre.back() == '/') {
                    fs::createTree(ruta);
                }
                else if (!e.nombre.empty()) {
                    fs::createTree(ruta);
                    actividad::detalle(actividad::texto("copiando", ruta));
                    this->out = std::fopen(ruta.c_str(), "wb");
                    if (!this->out) return this->falla(STATUS_EXTRACT_FAILED);
                    setvbuf(this->out, this->ioBuf.data(), _IOFBF, this->ioBuf.size());
                }
                if (e.metodo == 8) {
                    this->z = {};
                    if (inflateInit2(&this->z, -MAX_WBITS) != Z_OK) return this->falla(STATUS_EXTRACT_FAILED);
                    this->inflando = true;
                }
                this->estado = Estado::Datos;
                return this->restante > 0 || this->terminar(e);
            }

            bool volcar(const std::uint8_t* d, size_t t)
            {
                this->crc = crc32(this->crc, d, t);
                this->escritos += t;
                if (this->out && std::fwrite(d, 1, t, this->out) != t) return this->falla(STATUS_EXTRACT_FAILED);
                return true;
            }

            bool escribir(const Entrada& e, const std::uint8_t* d, size_t t)
            {
                if (e.metodo == 0) return this->volcar(d, t);
                this->z.next_in = const_cast<Bytef*>(d);
                this->z.avail_in = t;
                while (this->z.avail_in > 0) {
                    this->z.next_out = this->salida.data();
                    this->z.avail_out = this->salida.size();
                    const int r = inflate(&this->z, Z_NO_FLUSH);
                    if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) return this->falla(STATUS_CORRUPT_ARCHIVE);
                    const size_t hechos = this->salida.size() - this->z.avail_out;
                    if (hechos && !this->volcar(this->salida.data(), hechos)) return false;
                    if (r == Z_STREAM_END || (r == Z_BUF_ERROR && hechos == 0)) break;
                }
                return true;
            }

            bool terminar(const Entrada& e)
            {
                if (this->inflando) {
                    // Lo que quede adentro de zlib.
                    for (;;) {
                        this->z.next_out = this->salida.data();
                        this->z.avail_out = this->salida.size();
                        const int r = inflate(&this->z, Z_FINISH);
                        const size_t hechos = this->salida.size() - this->z.avail_out;
                        if (hechos && !this->volcar(this->salida.data(), hechos)) return false;
                        if (r == Z_STREAM_END || hechos == 0) break;
                    }
                    inflateEnd(&this->z);
                    this->inflando = false;
                }
                bool ok = this->escritos == e.descomprimido && this->crc == e.crc;
                if (this->out && fclose(this->out) != 0) ok = false;
                this->out = nullptr;
                if (!ok) return this->falla(STATUS_CORRUPT_ARCHIVE);
                ++this->k;
                this->estado = Estado::Saltar;
                return true;
            }
        };

    }  // namespace

    Indice leerIndice(const std::string& url)
    {
        Indice idx;
        std::string s;
        std::uint64_t total = 0;
        if (download::rango(url, 0, 0, s, &total) != 206 || total < 22) {
            idx.motivo = "el servidor no da trozos";
            return idx;
        }
        idx.total = total;
        const std::uint64_t desdeCola = total > 65536 ? total - 65536 : 0;
        std::string cola;
        if (download::rango(url, desdeCola, total - 1, cola) != 206) {
            idx.motivo = "no se pudo leer el final";
            return idx;
        }
        const size_t fin = cola.rfind(std::string("PK\x05\x06", 4));
        if (fin == std::string::npos || fin + 22 > cola.size()) {
            idx.motivo = "no es un zip";
            return idx;
        }
        std::uint64_t tamIndice = u32(cola, fin + 12), desde = u32(cola, fin + 16);
        if (tamIndice == 0xFFFFFFFF || desde == 0xFFFFFFFF || u16(cola, fin + 10) == 0xFFFF) {
            const size_t z64 = cola.rfind(std::string("PK\x06\x06", 4));
            if (z64 == std::string::npos || z64 + 56 > cola.size()) {
                idx.motivo = "zip64 sin su registro";
                return idx;
            }
            tamIndice = u64(cola, z64 + 40);
            desde = u64(cola, z64 + 48);
        }
        if (tamIndice == 0 || desde + tamIndice > total || tamIndice > (64u << 20)) {
            idx.motivo = "indice raro";
            return idx;
        }
        std::string ind;
        if (download::rango(url, desde, desde + tamIndice - 1, ind) != 206 || ind.size() != tamIndice) {
            idx.motivo = "no se pudo leer el indice";
            return idx;
        }

        std::vector<Entrada> crudas;
        std::vector<std::string> nombres;
        size_t p = 0;
        while (p + 46 <= ind.size() && u32(ind, p) == 0x02014b50) {
            Entrada e;
            const std::uint16_t flags = u16(ind, p + 8);
            e.metodo = u16(ind, p + 10);
            e.crc = u32(ind, p + 16);
            e.comprimido = u32(ind, p + 20);
            e.descomprimido = u32(ind, p + 24);
            const std::uint16_t largo = u16(ind, p + 28), extra = u16(ind, p + 30), comentario = u16(ind, p + 32);
            e.posicion = u32(ind, p + 42);
            if (p + 46 + largo + extra > ind.size()) break;
            e.nombre = ind.substr(p + 46, largo);
            // zip64: los que no entran en 32 bits van en el extra 0x0001, en este orden.
            size_t q = p + 46 + largo;
            const size_t finExtra = q + extra;
            while (q + 4 <= finExtra) {
                const std::uint16_t id = u16(ind, q), tam = u16(ind, q + 2);
                if (id == 1) {
                    size_t r = q + 4;
                    if (e.descomprimido == 0xFFFFFFFF && r + 8 <= q + 4 + tam) { e.descomprimido = u64(ind, r); r += 8; }
                    if (e.comprimido == 0xFFFFFFFF && r + 8 <= q + 4 + tam) { e.comprimido = u64(ind, r); r += 8; }
                    if (e.posicion == 0xFFFFFFFF && r + 8 <= q + 4 + tam) { e.posicion = u64(ind, r); r += 8; }
                }
                q += 4 + tam;
            }
            p += 46 + largo + extra + comentario;
            if (flags & 1) {
                idx.motivo = "zip cifrado";
                return idx;
            }
            if (e.metodo != 0 && e.metodo != 8) {
                idx.motivo = "compresion " + std::to_string(e.metodo);
                return idx;
            }
            // Nada fuera de la SD ni basura de macOS.
            if (e.nombre.rfind("__MACOSX", 0) == 0) continue;
            if (e.nombre.empty() || e.nombre[0] == '/' || e.nombre.find("..") != std::string::npos || e.nombre.find('\\') != std::string::npos) {
                idx.motivo = "ruta rara: " + e.nombre;
                return idx;
            }
            nombres.push_back(e.nombre);
            crudas.push_back(e);
        }
        if (crudas.empty()) {
            idx.motivo = "zip vacio";
            return idx;
        }
        const std::string quitar = envoltorio(nombres);
        bool nroPrincipal = false;
        for (auto& e : crudas) {
            if (!quitar.empty()) e.nombre = e.nombre.substr(quitar.size());
            idx.descomprimido += e.descomprimido;
            const std::string bajo = minusculas(e.nombre);
            if (bajo.rfind("switch/", 0) == 0 && bajo.size() > 4 && bajo.compare(bajo.size() - 4, 4, ".nro") == 0) {
                /* Con varias apps (Black Ops II: t6game, t6mp y t6zm), el acceso
                   directo va a la que se llama como su carpeta; si ninguna, la primera. */
                const size_t barra = bajo.rfind('/');
                const size_t antes = bajo.rfind('/', barra - 1);
                const bool principal = antes != std::string::npos && bajo.substr(antes + 1, barra - antes - 1) == bajo.substr(barra + 1, bajo.size() - barra - 5);
                if (idx.nro.empty() || (principal && !nroPrincipal)) {
                    idx.nro = std::string(ROOT_PATH) + e.nombre;
                    nroPrincipal = principal;
                }
            }
        }
        std::sort(crudas.begin(), crudas.end(), [](const Entrada& a, const Entrada& b) { return a.posicion < b.posicion; });
        for (size_t i = 1; i < crudas.size(); ++i)
            if (crudas[i].posicion == crudas[i - 1].posicion) {
                idx.motivo = "dos archivos en la misma posicion";
                return idx;
            }
        idx.entradas = std::move(crudas);
        idx.apto = true;
        return idx;
    }

    long instalar(const std::string& url, const Indice& indice)
    {
        s64 libre = 0;
        // Un margen de 32 MB: la SD no se llena justo hasta el borde.
        if (R_SUCCEEDED(fs::getFreeStorageSD(libre)) && (std::uint64_t)libre < indice.descomprimido + (32ull << 20))
            return STATUS_NO_SPACE;
        Escritor escritor(indice.entradas);
        const long codigo = download::enVivo(url, [&escritor](const std::uint8_t* d, std::size_t n) { return escritor.recibir(d, n); });
        if (escritor.error()) return escritor.error();
        if (codigo == 0 || codigo >= 400) return codigo == 0 ? STATUS_DOWNLOAD_FAILED : codigo;
        if (!escritor.completo()) return STATUS_INCOMPLETE_DOWNLOAD;
        return 200;
    }

}  // namespace zipvivo
