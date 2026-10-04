#pragma once

#include <string>

// El flujo de actualizar la propia app, en un solo sitio.
//
// Lo usan dos pantallas: la entrada del tab Herramientas y el aviso que salta
// al abrir la app. Estaba escrito solo en el tab, con los textos a mano en el
// codigo, y duplicarlo era la forma segura de que un dia los dos caminos
// dejaran de hacer lo mismo.
namespace appUpdate {

    // true si `tag` (el ultimo release publicado) es distinto de la version
    // compilada. Un tag vacio significa que no se pudo consultar: ahi no se
    // avisa de nada, porque no se sabe.
    bool isAvailable(const std::string& tag);

    // Abre el flujo completo: confirmar, descargar, extraer y reiniciar.
    //
    // Con `hasUpdate` en false es el mismo flujo pero re-descargando la version
    // actual, que sirve para recoger un rebuild publicado bajo el mismo tag.
    void pushFlow(const std::string& targetTag, bool hasUpdate);

    // El aviso de que hay version nueva, para mostrar al abrir la app. No hace
    // nada si no hay actualizacion, y solo se muestra una vez por arranque.
    void showNoticeIfAvailable(const std::string& tag);

}  // namespace appUpdate
