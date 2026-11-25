#pragma once

#include <string>

#include "constants.hpp"

namespace CurrentCfw {

    CFW getCFW();
    std::string getAmsInfo();
    extern CFW running_cfw;
    // Nexus Bacán corriendo ahora: registra el servicio «ams:usb» mientras funciona.
    bool nexusCorriendo();

}  // namespace CurrentCfw