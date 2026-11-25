#pragma once
// Provision locally with tools/fota/provision.py; never compile a private key.
#if __has_include("FotaTrust.local.h")
#include "FotaTrust.local.h"
#else
#define TUNNEL_FOTA_URL_PREFIX ""
#define TUNNEL_FOTA_CA_PEM ""
#define TUNNEL_FOTA_PUBLIC_KEY ""
#endif
