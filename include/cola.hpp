#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/* La cola de descargas (5.0.0): los ports y las apps se agregan y se instalan
   uno tras otro en segundo plano, mientras se sigue usando la app.

   Lo que toca el arranque (pack, Atmosphère, firmware, hekate...) sigue en su
   pantalla completa (WorkerPage): mientras corre una de esas, la cola no
   empieza nada nuevo, y una de esas no empieza mientras la cola trabaja (las
   dos usan el mismo ProgressEvent y el mismo archivo temporal). */
namespace cola {

    struct Paso {
        std::string texto;
        std::function<void()> fn;  // corre en el hilo de la cola; avisa por ProgressEvent
    };

    /* Agrega una instalacion al final; empieza sola cuando le toca. Devuelve su id.
       `accesos`: si salio bien, que .nro ofrecer como acceso directo (se pregunta
       al terminar, con siguienteOferta). */
    int agregar(const std::string& nombre, std::vector<Paso> pasos, std::function<std::vector<std::string>()> accesos = {}, long long bytes = 0);

    // El id de lo que se llama asi si ya esta en la cola (instalandose o esperando); 0 si no.
    int idDe(const std::string& nombre);

    // Lo que van a ocupar en la SD lo que se instala y lo que espera (para no pasarse de espacio).
    long long reservado();

    /* La pantalla de descarga de ese trabajo esta a la vista (se llama en cada
       cuadro). Si falla mientras se mira, el error lo muestra ella; si no, se
       avisa con siguienteFallo. */
    void mirando(int id);

    struct Oferta {
        std::string nombre;
        std::vector<std::string> nros;
    };
    // Lo que termino bien y tiene acceso directo para ofrecer (se saca de la lista).
    bool siguienteOferta(Oferta& out);

    // Trabajando o con algo esperando.
    bool ocupada();

    struct Hecho {
        int id = 0;
        std::string nombre;
        std::string error;  // vacio: salio bien
        bool cancelado = false;
    };

    // Lo que fallo sin que nadie mirara su pantalla (se saca de la lista).
    bool siguienteFallo(Hecho& out);

    struct Estado {
        bool trabajando = false;
        int actualId = 0;
        std::string actual;    // lo que se instala ahora
        std::string paso;      // en que va ("Bajando parte 2 de 9")
        std::string detalle;   // el ultimo archivo, si el paso lo dice
        int pasoNum = 0, pasos = 0;
        double ahora = 0, total = 0, velocidad = 0;  // bytes del paso actual
        int step = 0, max = 0;
        std::vector<std::string> espera;  // lo que sigue, en orden
        std::vector<int> esperaIds;
        std::vector<Hecho> hechos;        // lo ultimo terminado, lo mas nuevo primero
    };
    Estado estado();

    // La proxima linea de actividad de lo que se instala (para la cascada).
    bool siguienteLinea(std::string& out);

    // Cancela lo que se esta instalando (lo escrito queda; la parte se marca pendiente).
    void cancelarActual();

    // Quita de la espera (indice en Estado::espera).
    void quitar(std::size_t i);

    // Sube o baja en la espera (indice en Estado::espera; -1 arriba, +1 abajo).
    void mover(std::size_t i, int hacia);

    // Olvida la lista de terminados.
    void limpiarHechos();

    /* Para las WorkerPage (pantalla completa). tomarExclusiva() devuelve false
       mientras la cola trabaja: la pagina espera y lo vuelve a intentar. Con
       una exclusiva tomada, la cola no empieza nada hasta soltarExclusiva(). */
    bool tomarExclusiva();
    void soltarExclusiva();

    // Al cerrar la app: corta lo que este bajando y espera al hilo.
    void detener();

}  // namespace cola
