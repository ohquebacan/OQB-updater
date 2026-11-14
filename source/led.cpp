#include "led.hpp"

#include <switch.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace led {

namespace {

    constexpr const char* ArchivoApagada = "sdmc:/config/aio-switch-updater/luz_apagada";

    enum class Modo { Nada, Pulso, PulsoBateria, Exito, Error };

    /* Switch Lite: no tiene luz en un mando. Su luz de avisos va directo por un
       pin de la placa (GPIO, como hace lite-led) y solo se prende o se apaga,
       así que los efectos se hacen prendiéndola y apagándola rápido desde un
       hilo (periodo de 2 ms: mil llamadas por segundo en vez de las diez mil de
       lite-led, para no quitarle CPU a la instalación). */
    constexpr u32 LiteCodigoGpio = 0x35000065;
    constexpr u32 LiteModoGpio = 3;
    constexpr u64 LitePeriodoNs = 2'000'000;
    constexpr int LiteMinimo = 8, LiteMaximo = 255;

    Thread g_hilo{};
    bool g_hiloCreado = false;
    std::atomic<bool> g_seguir{false};
    std::atomic<int> g_modoLite{static_cast<int>(Modo::Nada)};
    std::atomic<u64> g_inicioModo{0};
    bool g_pinFijo = false;          // Lite: pin prendido fijo (reinicio), sin hilo
    GpioPadSession g_pinFijoSesion{};
    bool g_reinicioPendiente = false;
    int g_activada = -1;             // -1: sin leer todavía

    // Solo las fallas, para que el archivo no crezca en cada instalación.
    void anotar(const char* paso, Result rc)
    {
        if (R_SUCCEEDED(rc))
            return;
        if (FILE* f = std::fopen("sdmc:/config/aio-switch-updater/led-lite.log", "a")) {
            std::fprintf(f, "%s rc=0x%08X\n", paso, static_cast<unsigned>(rc));
            std::fclose(f);
        }
    }

    bool esLite()
    {
        static int lite = -1;
        if (lite >= 0)
            return lite == 1;
        SetSysProductModel modelo{};
        lite = 0;
        if (R_SUCCEEDED(setsysInitialize())) {
            lite = R_SUCCEEDED(setsysGetProductModel(&modelo)) && modelo == SetSysProductModel_Hoag;
            setsysExit();
        }
        return lite == 1;
    }

    bool bateriaBaja()
    {
        if (R_FAILED(psmInitialize()))
            return false;
        u32 carga = 100;
        PsmChargerType cargador = PsmChargerType_Unconnected;
        const bool baja = R_SUCCEEDED(psmGetBatteryChargePercentage(&carga)) && R_SUCCEEDED(psmGetChargerType(&cargador)) &&
                          carga < 15 && cargador == PsmChargerType_Unconnected;
        psmExit();
        return baja;
    }

    // ---- Mandos (Joy-Con derecho, Pro Controller): el patrón lo corre el mando solo.

    constexpr HidNpadIdType Mandos[] = {
        HidNpadIdType_Handheld, HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
        HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8,
    };

    void aplicar(const HidsysNotificationLedPattern& patron)
    {
        if (R_FAILED(hidsysInitialize()))
            return;
        for (const auto mando : Mandos) {
            HidsysUniquePadId ids[2]{};
            s32 total = 0;
            if (R_FAILED(hidsysGetUniquePadsFromNpad(mando, ids, 2, &total)))
                continue;
            for (s32 i = 0; i < total && i < 2; ++i)
                hidsysSetNotificationLedPattern(&patron, ids[i]);
        }
        hidsysExit();
    }

    HidsysNotificationLedPattern vacio()
    {
        HidsysNotificationLedPattern p;
        std::memset(&p, 0, sizeof(p));
        return p;
    }

    void ciclo(HidsysNotificationLedPattern& p, u8 intensidad, u8 pasos, u8 espera)
    {
        auto& c = p.miniCycles[p.totalMiniCycles++];
        c.ledIntensity = intensidad;
        c.transitionSteps = pasos;
        c.finalStepDuration = espera;
    }

    HidsysNotificationLedPattern patronMando(Modo modo)
    {
        auto p = vacio();
        switch (modo) {
            case Modo::Pulso:  // de tenue a pleno y de vuelta (el «fade» de lite-led)
                p.baseMiniCycleDuration = 0x8;
                p.startIntensity = 0x2;
                ciclo(p, 0xF, 0xF, 0x0);
                ciclo(p, 0x2, 0xF, 0x0);
                break;
            case Modo::PulsoBateria:  // el mismo pulso y un doble parpadeo: ojo, batería baja
                p.baseMiniCycleDuration = 0x8;
                p.startIntensity = 0x2;
                ciclo(p, 0xF, 0xF, 0x0);
                ciclo(p, 0x2, 0xF, 0x0);
                ciclo(p, 0xF, 0x1, 0x1);
                ciclo(p, 0x0, 0x1, 0x1);
                ciclo(p, 0xF, 0x1, 0x1);
                ciclo(p, 0x0, 0x1, 0x2);
                break;
            case Modo::Exito:  // dos destellos y se queda apagada (una sola vuelta)
                p.baseMiniCycleDuration = 0x2;
                p.totalFullCycles = 0x1;
                ciclo(p, 0xF, 0x1, 0xA);
                ciclo(p, 0x0, 0x1, 0xA);
                ciclo(p, 0xF, 0x1, 0xA);
                ciclo(p, 0x0, 0x1, 0xF);
                break;
            case Modo::Error:  // tres parpadeos rápidos y una pausa, sin parar
                p.baseMiniCycleDuration = 0x2;
                for (int i = 0; i < 3; ++i) {
                    ciclo(p, 0xF, 0x1, 0x6);
                    ciclo(p, 0x0, 0x1, 0x6);
                }
                ciclo(p, 0x0, 0x1, 0xF);
                ciclo(p, 0x0, 0x1, 0xF);
                break;
            case Modo::Nada:
                break;
        }
        return p;
    }

    // ---- Switch Lite: brillo (0..255) del modo a los `ms` de empezado; -1 = terminó.

    int brilloLite(Modo modo, u64 ms)
    {
        auto triangulo = [](u64 t, u64 periodo) {
            const u64 mitad = periodo / 2;
            const u64 f = t % periodo;
            const u64 sube = f < mitad ? f : periodo - f;
            return LiteMinimo + int((LiteMaximo - LiteMinimo) * sube / mitad);
        };
        switch (modo) {
            case Modo::Pulso:
                return triangulo(ms, 2400);
            case Modo::PulsoBateria: {
                const u64 f = ms % 3400;  // un pulso y después dos parpadeos
                if (f < 2400) return triangulo(f, 2400);
                const u64 g = f - 2400;
                return (g < 150 || (g >= 300 && g < 450)) ? LiteMaximo : 0;
            }
            case Modo::Exito:
                if (ms >= 1000) return -1;
                return (ms < 200 || (ms >= 400 && ms < 600)) ? LiteMaximo : 0;
            case Modo::Error: {
                const u64 f = ms % 1500;  // tres parpadeos de 120 ms y una pausa
                return (f < 720 && (f / 120) % 2 == 0) ? LiteMaximo : 0;
            }
            case Modo::Nada:
                return -1;
        }
        return -1;
    }

    u64 ahoraMs() { return armTicksToNs(armGetSystemTick()) / 1'000'000; }

    void hiloLite(void*)
    {
        const Result ini = gpioInitialize();
        anotar("gpioInitialize", ini);
        if (R_FAILED(ini))
            return;
        GpioPadSession pin{};
        const Result abrir = gpioOpenSession2(&pin, LiteCodigoGpio, LiteModoGpio);
        anotar("gpioOpenSession2", abrir);
        if (R_SUCCEEDED(abrir)) {
            anotar("gpioPadSetDirection", gpioPadSetDirection(&pin, GpioDirection_Output));
            while (g_seguir.load()) {
                const int b = brilloLite(static_cast<Modo>(g_modoLite.load()), ahoraMs() - g_inicioModo.load());
                if (b < 0)
                    break;
                const u64 prendidoNs = (LitePeriodoNs * u64(b)) / LiteMaximo;
                if (prendidoNs) {
                    gpioPadSetValue(&pin, GpioValue_High);
                    svcSleepThread(prendidoNs);
                }
                if (prendidoNs < LitePeriodoNs) {
                    gpioPadSetValue(&pin, GpioValue_Low);
                    svcSleepThread(LitePeriodoNs - prendidoNs);
                }
            }
            gpioPadSetValue(&pin, GpioValue_Low);
            gpioPadClose(&pin);
        }
        gpioExit();
    }

    void pararLite()
    {
        if (g_hiloCreado) {
            g_seguir = false;
            threadWaitForExit(&g_hilo);
            threadClose(&g_hilo);
            g_hiloCreado = false;
        }
        if (g_pinFijo) {
            gpioPadSetValue(&g_pinFijoSesion, GpioValue_Low);
            gpioPadClose(&g_pinFijoSesion);
            gpioExit();
            g_pinFijo = false;
        }
    }

    void modoLite(Modo modo)
    {
        if (!esLite())
            return;
        g_inicioModo = ahoraMs();
        g_modoLite = static_cast<int>(modo);
        g_seguir = true;
        // 0x3B es la prioridad más baja que el sistema le permite a una app
        // (0x3F da 0xE001, prioridad inválida): si la CPU está ocupada, espera la luz.
        const Result crear = threadCreate(&g_hilo, hiloLite, nullptr, nullptr, 0x4000, 0x3B, -2);
        anotar("threadCreate", crear);
        if (R_SUCCEEDED(crear)) {
            const Result arrancar = threadStart(&g_hilo);
            anotar("threadStart", arrancar);
            if (R_SUCCEEDED(arrancar))
                g_hiloCreado = true;
            else
                threadClose(&g_hilo);
        }
    }

    void poner(Modo modo)
    {
        if (!activada())
            return;
        g_reinicioPendiente = false;
        aplicar(patronMando(modo));
        if (esLite()) {
            // Cada modo arranca su propio hilo: más simple que reusar uno que
            // pudo haber terminado (el éxito se apaga solo).
            pararLite();
            modoLite(modo);
        }
    }

}  // namespace

void pulso() { poner(bateriaBaja() ? Modo::PulsoBateria : Modo::Pulso); }
void exito() { poner(Modo::Exito); }
void error() { poner(Modo::Error); }

void reinicio()
{
    if (!activada())
        return;
    // Fija y suave en los mandos; el patrón queda en el mando aunque se cierre la app.
    auto p = vacio();
    p.baseMiniCycleDuration = 0xF;
    p.startIntensity = 0x5;
    ciclo(p, 0x5, 0xF, 0xF);
    aplicar(p);
    g_reinicioPendiente = true;
    if (esLite()) {
        // La Lite no puede quedar a media luz sin el hilo: prendida fija.
        pararLite();
        if (R_SUCCEEDED(gpioInitialize())) {
            if (R_SUCCEEDED(gpioOpenSession2(&g_pinFijoSesion, LiteCodigoGpio, LiteModoGpio))) {
                gpioPadSetDirection(&g_pinFijoSesion, GpioDirection_Output);
                gpioPadSetValue(&g_pinFijoSesion, GpioValue_High);
                g_pinFijo = true;
            } else {
                gpioExit();
            }
        }
    }
}

void apagar()
{
    g_reinicioPendiente = false;
    pararLite();
    aplicar(vacio());
}

void alSalir()
{
    // Pidiendo reiniciar: la luz se queda (el mando la sigue corriendo y el pin
    // de la Lite queda como está al cerrar la app).
    if (g_reinicioPendiente)
        return;
    apagar();
}

bool activada()
{
    if (g_activada < 0) {
        FILE* f = std::fopen(ArchivoApagada, "rb");
        g_activada = f ? 0 : 1;
        if (f)
            std::fclose(f);
    }
    return g_activada == 1;
}

void setActivada(bool si)
{
    if (si) {
        std::remove(ArchivoApagada);
    } else {
        if (FILE* f = std::fopen(ArchivoApagada, "wb"))
            std::fclose(f);
        apagar();
    }
    g_activada = si ? 1 : 0;
}

}  // namespace led
