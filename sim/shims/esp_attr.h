#pragma once
// Host shim. The device gets the real esp_attr.h from esp_common; the sim only needs the one
// attribute main.c uses, and on a host build it must expand to nothing.
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif
