#pragma once

// OQB: la luz del botón HOME de los mandos (Joy-Con derecho, Pro Controller)
// y, en la Switch Lite, su luz de avisos de la placa (GPIO) dicen qué pasa sin
// mirar la pantalla. No necesita ningún sysmodule. Si algo falla (sin mandos,
// sin permiso), simplemente no se ilumina.
namespace led {
    void pulso();      // trabajando: sube y baja sin parar (con doble parpadeo si la batería está baja)
    void exito();      // terminó bien: dos destellos y se apaga
    void error();      // falló: tres parpadeos rápidos que se repiten hasta apagar()
    void reinicio();   // hay que reiniciar: fija y suave, se queda aunque se cierre la app
    void apagar();
    void alSalir();    // al cerrar la app: apaga, salvo que esté pidiendo reiniciar

    // Ajustes › Consola: «Luz de los mandos». Se guarda en la SD.
    bool activada();
    void setActivada(bool si);
}
