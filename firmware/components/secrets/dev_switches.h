// The DEV_* switches of secrets.h, 0 when secrets.h does not define them or does not
// exist: a release build needs no secrets.h.
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef DEV_OPEN_DIAGNOSTICS
#define DEV_OPEN_DIAGNOSTICS 0
#endif
#ifndef DEV_EARLY_CONFIRM
#define DEV_EARLY_CONFIRM 0
#endif
#ifdef DEV_FW_ORIGIN
#define DEV_FW_STAND 1
#else
#define DEV_FW_STAND 0
#endif
#ifdef CGDN1_MIGRATE
#define DEV_MIGRATE 1
#else
#define DEV_MIGRATE 0
#endif
// Any switch is on: GET /status reports "dev":true.
#define DEV_ANY (DEV_OPEN_DIAGNOSTICS || DEV_EARLY_CONFIRM || DEV_FW_STAND || DEV_MIGRATE)
