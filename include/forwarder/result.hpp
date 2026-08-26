// Códigos de error propios del módulo de forwarders.
#pragma once

#include <switch.h>

namespace fwd {

// Módulo fuera del rango que usa Nintendo, para no chocar con los del sistema.
inline constexpr u32 MODULE_FWD = 424;

inline constexpr Result Result_BadArgs = MAKERESULT(MODULE_FWD, 1);
inline constexpr Result Result_HblMissing = MAKERESULT(MODULE_FWD, 2);
inline constexpr Result Result_NpdmPatchFailed = MAKERESULT(MODULE_FWD, 3);
inline constexpr Result Result_NroInvalid = MAKERESULT(MODULE_FWD, 4);
inline constexpr Result Result_NroNoAssets = MAKERESULT(MODULE_FWD, 5);

}  // namespace fwd
