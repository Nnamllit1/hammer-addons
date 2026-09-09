#ifndef HAMMER_EXTENSIONS_H
#define HAMMER_EXTENSIONS_H
#include "hammer_addons.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define HA_EXTENSIONS_VERSION 1u
#define HA_CAP_UI UINT64_C(4)
#define HA_CAP_SETTINGS UINT64_C(8)
#define HA_CAP_MENU_HOOKS UINT64_C(16)
#define HA_CAP_IMPORTERS UINT64_C(32)
#define HA_COMMAND 1u
#define HA_PANEL 2u
#define HA_MENU_HOOK 3u
#define HA_IMPORTER 4u
#define HA_IMPORT_ROUTE 5u /* extend an existing action with built-in/add-on importer choice */
#define HA_LABEL 1u
#define HA_BUTTON 2u
#define HA_TEXT 3u
#define HA_CHECKBOX 4u
#define HA_CHOICE 5u
#define HA_CONTINUE 0
#define HA_HANDLED 1
#define HA_ERROR (-1)
#define HA_BUSY (-2)

/* All callbacks run on the editor UI thread when invoked by the UI adapter.
   phase: command, panel.change, panel.click, hook.before, hook.after, import.
   Hook before: CONTINUE invokes the original; HANDLED suppresses it.
   Import: HANDLED means the handler completed its work, ERROR means failure.
   response is optional feedback; write a NUL-terminated UTF-8 string that fits.
   Never retain event strings. Keep callbacks short; no exceptions across ABI. */
typedef struct HA_InteractionV1 {
    uint32_t size;
    const char* tool;
    const char* phase;
    const char* control_id;
    const char* value;
} HA_InteractionV1;
typedef int (HA_CALL *HA_InteractionFn)(void* user, const HA_InteractionV1* event,
                                      char* response, size_t capacity);
typedef struct HA_ControlV1 {
    uint32_t size, kind;
    const char* id;
    const char* label;
    const char* initial_value; /* checkbox: 0/1, choice: zero-based index */
    const char* options;       /* choice: newline-separated labels */
} HA_ControlV1;
typedef struct HA_ContributionV1 {
    uint32_t size, kind;
    const char* id;        /* unique within this add-on */
    const char* tool;      /* all or one supported tool ID */
    const char* label;
    /* command/panel/importer: existing menu path, e.g. File or File/Import;
       empty uses the framework menu. hook/import route: existing action path, e.g. File/Open.
       Prefix a segment with @ to match objectName instead of visible text.
       Unavailable or ambiguous targets never fall back to a guessed location. */
    const char* target;
    const char* options;   /* command: shortcut; importer: comma-separated extensions without dots */
    const HA_ControlV1* controls;
    uint32_t control_count;
    HA_InteractionFn callback;
    void* user;
} HA_ContributionV1;
typedef struct HA_ExtensionsV1 {
    uint32_t size, version;
    /* Register during on_load only. Definitions/strings are copied.
       Returns a process-lifetime handle, or 0 for an invalid registration. */
    uint64_t (HA_CALL *register_contribution)(void* context, const HA_ContributionV1* definition);
    /* Per-user, per-add-on settings. get returns required bytes incl NUL, 0 on error.
       Missing keys return an empty string. No buffer is written if too small. */
    size_t (HA_CALL *get_setting)(void* context, const char* key, char* output, size_t capacity);
    int (HA_CALL *set_setting)(void* context, const char* key, const char* value);
} HA_ExtensionsV1;
/* Safe against an older host with only the original ABI-1 prefix. */
static inline const HA_ExtensionsV1* HA_GetExtensions(const HA_HostV1* host) {
    if (!host || host->size < sizeof(HA_HostV1) || host->abi_version != HA_ABI_VERSION || !host->extensions ||
        host->extensions->size < sizeof(HA_ExtensionsV1) ||
        host->extensions->version != HA_EXTENSIONS_VERSION) return NULL;
    return host->extensions;
}
#ifdef __cplusplus
}
#endif
#endif
