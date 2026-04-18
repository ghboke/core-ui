/*
 * plugin_api.h — Plugin interface for Core UI host applications
 *
 * Each plugin is a DLL that exports plugin_init / plugin_shutdown.
 * The host loads plugins and provides a UiHostAPI for them to use.
 */
#ifndef PLUGIN_API_H
#define PLUGIN_API_H

#include "ui_core.h"

#define PLUGIN_API_VERSION 1

/* ------------------------------------------------------------------ */
/* Host-provided API (passed to plugin_init)                          */
/* ------------------------------------------------------------------ */
typedef struct UiHostAPI {
    int version;
    UiWindow (*create_window)(const UiWindowConfig* config);
    void     (*notify_host)(const char* event_name, const char* json_data);
    void*    host_userdata;
} UiHostAPI;

/* ------------------------------------------------------------------ */
/* Plugin export macros                                               */
/* ------------------------------------------------------------------ */
#ifdef __cplusplus
  #define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
  #define PLUGIN_EXPORT __declspec(dllexport)
#endif

/*
 * Every plugin DLL must export these functions:
 *
 *   PLUGIN_EXPORT int         plugin_init(const UiHostAPI* host);
 *   PLUGIN_EXPORT void        plugin_shutdown(void);
 *   PLUGIN_EXPORT const char* plugin_name(void);
 *   PLUGIN_EXPORT int         plugin_version(void);
 */
typedef int         (*PluginInitFunc)(const UiHostAPI* host);
typedef void        (*PluginShutdownFunc)(void);
typedef const char* (*PluginNameFunc)(void);
typedef int         (*PluginVersionFunc)(void);

#endif /* PLUGIN_API_H */
