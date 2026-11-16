#pragma once

#include <string>

/* El idioma del Inicio OQB de hekate (clave oqblang de bootloader/nyx.ini:
   0 español, 1 inglés, 2 portugués) según el idioma de la app, que es el de
   la consola salvo que se haya elegido otro en sus ajustes. Cualquier otro
   idioma va en inglés.

   El pack trae nyx.ini en inglés. La app lo ajusta en dos momentos y nunca
   más, para no pisar a quien lo cambie a mano en hekate:
   - al terminar de instalar un HATS pack;
   - la primera vez que se abre esta versión, solo si nyx.ini sigue con el
     inglés del pack (o sin la clave). */
namespace hekateIdioma {

    int desdeLocale(const std::string& locale);

    // Devuelve nyx.ini con oqblang puesto en valor; respeta CRLF y el resto
    // del archivo. Si no hay sección [config], lo devuelve igual.
    std::string conIdioma(const std::string& ini, int valor);

    // -1 si nyx.ini no tiene la clave.
    int leer(const std::string& ini);

    // El idioma con que arrancó la app (language.json o el de la consola).
    void fijarIdiomaApp(const std::string& idioma);

    void trasInstalarPack();
    void alAbrir();

}  // namespace hekateIdioma
