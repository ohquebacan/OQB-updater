#pragma once

#include <string>

// Lo que está haciendo la app ahora, en frases legibles, para las tres líneas
// en cascada de la ventana de trabajo. En memoria: escribir en la SD por cada
// archivo extraído frenaría la instalación.
namespace actividad {
    // Un paso que se ve siempre ("Descargando el pack", "1234 archivos
    // copiados"). Si llegan varios juntos esperan su turno.
    void paso(const std::string& texto);
    // El detalle del momento ("Copiando switch/DBI/DBI.nro"): solo cuenta el
    // último; los que pasan entre dos cuadros no se muestran.
    void detalle(const std::string& texto);
    // Vacía lo pendiente. Al empezar cada tarea.
    void limpiar();
    // La siguiente línea a mostrar, si hay. Para la interfaz.
    bool siguiente(std::string& out);
    // "menus/registro/<clave>" traducido, con su {} rellenado.
    std::string texto(const char* clave, const std::string& valor = "");
    std::string texto(const char* clave, const std::string& valor, const std::string& otro);
    // El nombre del archivo de una ruta o URL (sin la consulta "?...").
    std::string nombre(const std::string& ruta);
}  // namespace actividad
