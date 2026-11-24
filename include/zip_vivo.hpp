#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Instalar un zip mientras baja, sin guardarlo en la SD (los ports, 4.6.0).

   Primero se lee el indice del zip con tres pedidos parciales (el final del
   archivo y el indice): de ahi salen el nombre, el tamano, la compresion, el
   CRC y la posicion de cada archivo. Despues se baja el zip de corrido y cada
   archivo se escribe en su lugar a medida que llega (guardado tal cual o
   comprimido con deflate). Asi no hace falta espacio para el zip, la SD
   escribe la mitad y no hay una fase aparte de descomprimir.

   Si el servidor no da trozos o el zip trae algo que esto no sabe leer
   (cifrado, otra compresion), `apto` queda en false y se usa el camino de
   siempre (bajar el zip y descomprimirlo). */
namespace zipvivo {

    struct Entrada {
        std::string nombre;    // ya sin la carpeta envoltorio
        std::uint16_t metodo = 0;  // 0 guardado, 8 deflate
        std::uint64_t comprimido = 0, descomprimido = 0, posicion = 0;
        std::uint32_t crc = 0;
    };

    struct Indice {
        std::uint64_t total = 0;          // lo que pesa el zip
        std::uint64_t descomprimido = 0;  // lo que ocupa en la SD
        std::vector<Entrada> entradas;    // por posicion en el zip
        std::string nro;                  // el primer .nro de /switch, si trae
        bool apto = false;
        std::string motivo;               // por que no, para el registro
    };

    Indice leerIndice(const std::string& url);

    /* Baja e instala. Devuelve un codigo como downloadFile: 200 si quedo todo
       bien; 0 o >399 si no (STATUS_NO_SPACE, STATUS_CORRUPT_ARCHIVE,
       STATUS_EXTRACT_FAILED...). Si se corta a la mitad, lo escrito queda: la
       parte se marca pendiente y se vuelve a instalar entera. */
    long instalar(const std::string& url, const Indice& indice);

}  // namespace zipvivo
