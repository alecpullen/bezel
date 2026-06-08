#pragma once
// wayland-scanner emits C headers. The wlr-layer-shell header declares a
// parameter literally named `namespace`, which is a C++ keyword. Include
// wayland-client.h first (so its guard is set), then macro-rename `namespace`
// only across the generated header.
#include <wayland-client.h>
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
