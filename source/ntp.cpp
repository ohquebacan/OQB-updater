#include "ntp.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <switch.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <ctime>
#include <thread>

namespace ntp {

    namespace {

        // Servidores por orden de preferencia. Los dos primeros son anycast, así
        // que responden desde el nodo más cercano; el tercero queda de reserva
        // por si una regla de DNS del pack se lleva por delante alguno.
        constexpr const char* SERVERS[] = {
            "time.cloudflare.com",
            "time.google.com",
            "pool.ntp.org",
        };

        constexpr uint16_t NTP_PORT = 123;

        // NTP cuenta desde 1900 y POSIX desde 1970.
        constexpr uint64_t NTP_TO_POSIX = 2208988800ull;

        // Corto a propósito: esto corre al abrir la app. Si un servidor no
        // contesta en tres segundos, se pasa al siguiente.
        constexpr int RECV_TIMEOUT_S = 3;

        // Por debajo de esto no se toca el reloj. Corregir dos segundos no
        // arregla nada y escribe en el reloj del sistema en cada arranque.
        constexpr long long MIN_DRIFT_S = 5;

        /* Cualquier respuesta que diga que estamos antes de 2024 es basura: el
           reloj de la consola puede ir atrasado, pero un servidor NTP real no
           devuelve una fecha anterior a cuando se escribió esto. Sin este
           limite, un paquete corrupto o un servidor falso podría dejar la
           consola en 1970. */
        constexpr uint64_t SANE_FLOOR = 1704067200ull;  // 2024-01-01

        /* Paquete NTP de RFC 5905. La disposición viene del cliente de
           lettier/ntpclient (BSD 3-Clause), por la vía de NX-ntpc, que es de
           donde salía el intento anterior que había en el repo. */
        struct Packet
        {
            uint8_t li_vn_mode;
            uint8_t stratum;
            uint8_t poll;
            uint8_t precision;
            uint32_t rootDelay;
            uint32_t rootDispersion;
            uint32_t refId;
            uint32_t refTm_s;
            uint32_t refTm_f;
            uint32_t origTm_s;
            uint32_t origTm_f;
            uint32_t rxTm_s;
            uint32_t rxTm_f;
            uint32_t txTm_s;
            uint32_t txTm_f;
        };

        static_assert(sizeof(Packet) == 48, "el paquete NTP son 48 bytes");

        std::thread backgroundThread;

        // Pregunta la hora a un servidor. Devuelve false y deja el motivo en
        // `error` si no se pudo.
        bool query(const char* host, uint64_t& out, std::string& error)
        {
            struct hostent* server = gethostbyname(host);
            if (server == nullptr || server->h_addr_list[0] == nullptr) {
                error = "no se pudo resolver el nombre";
                return false;
            }

            const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock < 0) {
                error = "no se pudo abrir el socket";
                return false;
            }

            /* El timeout es lo que distingue esto del intento que ya había en el
               repo: sin él, un servidor que no contesta deja el hilo esperando
               para siempre. */
            struct timeval tv;
            tv.tv_sec = RECV_TIMEOUT_S;
            tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(NTP_PORT);
            std::memcpy(&addr.sin_addr.s_addr, server->h_addr_list[0], 4);

            Packet packet;
            std::memset(&packet, 0, sizeof(packet));
            // LI = 0 (sin aviso), versión 4, modo 3 (cliente).
            packet.li_vn_mode = (0 << 6) | (4 << 3) | 3;

            bool ok = false;
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                error = "no se pudo conectar";
            }
            else if (send(sock, &packet, sizeof(packet), 0) < 0) {
                error = "no se pudo enviar la consulta";
            }
            else if (recv(sock, &packet, sizeof(packet), 0) < (ssize_t)sizeof(packet)) {
                error = "el servidor no respondió a tiempo";
            }
            else {
                const uint8_t mode = packet.li_vn_mode & 0x7;
                if (mode != 4) {
                    // Modo 4 es la respuesta de un servidor. Otra cosa no lo es.
                    error = "respuesta que no es de un servidor";
                }
                else if (packet.stratum == 0) {
                    // Stratum 0 es un "kiss-o'-death": el servidor nos está
                    // diciendo que no le preguntemos, no dándonos la hora.
                    error = "el servidor rechazó la consulta";
                }
                else {
                    const uint64_t seconds = ntohl(packet.txTm_s);
                    if (seconds <= NTP_TO_POSIX) {
                        error = "la marca de tiempo no es válida";
                    }
                    else {
                        const uint64_t posix = seconds - NTP_TO_POSIX;
                        if (posix < SANE_FLOOR) {
                            error = "la fecha recibida no es creíble";
                        }
                        else {
                            out = posix;
                            ok = true;
                        }
                    }
                }
            }

            close(sock);
            return ok;
        }

    }  // namespace

    SyncResult sync()
    {
        SyncResult result;

        uint64_t networkTime = 0;
        std::string lastError = "no se pudo consultar ningún servidor";
        bool got = false;
        for (const char* host : SERVERS) {
            std::string error;
            if (query(host, networkTime, error)) {
                result.detail = host;
                got = true;
                break;
            }
            lastError = std::string(host) + ": " + error;
        }

        if (!got) {
            result.detail = lastError;
            return result;
        }

        uint64_t consoleTime = 0;
        if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &consoleTime))) {
            result.detail = "no se pudo leer el reloj de la consola";
            return result;
        }

        // Si está apagada, la hora queda guardada pero no se ve, y hay que
        // decirlo en los dos casos: si no, parece que no hizo nada.
        bool autoCorrection = false;
        if (R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&autoCorrection)))
            result.autoCorrectionEnabled = autoCorrection;

        result.drift = (long long)networkTime - (long long)consoleTime;
        if (result.drift > -MIN_DRIFT_S && result.drift < MIN_DRIFT_S) {
            // Ya estaba en hora. No se escribe el reloj por unos segundos.
            result.ok = true;
            result.alreadyInSync = true;
            return result;
        }

        /* El reloj de red es el que de verdad se puede escribir desde aquí, y
           es el que vale: con la opción "Sincronizar reloj por internet" puesta,
           el sistema lo copia al reloj de usuario — el que se ve — sin salir a
           internet.

           El de usuario se intenta igual, por si en algún firmware o servicio
           sí deja, pero que lo rechace no es un fallo de la sincronización: la
           hora quedó guardada. Darlo por fallido, que es lo que hacía antes,
           era decir que no se hizo nada cuando sí se hizo. */
        if (R_FAILED(timeSetCurrentTime(TimeType_NetworkSystemClock, networkTime))) {
            result.detail = "no se pudo escribir el reloj de la consola";
            return result;
        }

        result.userClockWritten = R_SUCCEEDED(timeSetCurrentTime(TimeType_UserSystemClock, networkTime));

        result.ok = true;
        return result;
    }

    void syncInBackground()
    {
        if (backgroundThread.joinable())
            return;

        // Sin tocar la interfaz desde aquí: este hilo solo habla con la red y
        // con el servicio de hora.
        backgroundThread = std::thread([]() { sync(); });
    }

    void waitForBackgroundSync()
    {
        if (backgroundThread.joinable())
            backgroundThread.join();
    }

}  // namespace ntp
