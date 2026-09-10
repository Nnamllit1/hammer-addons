#ifndef HAMMER_EDITOR_H
#define HAMMER_EDITOR_H
#include "hammer_extensions.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HA_CAP_EDITOR_EVENTS UINT64_C(32)
#define HA_CAP_LIVE_PANELS UINT64_C(64)
#define HA_EDITOR_VISIBLE 1u
#define HA_EDITOR_ACTIVE 2u
#define HA_EDITOR_WINDOW_MODIFIED 4u
#define HA_EDITOR_HAS_DOCUMENT_PATH 8u
/* Observation of Qt editor metadata, not a scene snapshot or edit-operation revision.
   Identity is (session_id, window_id). sequence increases when metadata changes.
   Paths are exactly what the editor reports through windowFilePath; empty means
   unavailable. WINDOW_MODIFIED is Qt's window indicator, not verified scene dirtiness.
   All strings/state are borrowed only for the callback. Never retain these pointers. */
typedef struct HA_EditorStateV1 {
    uint32_t size, flags;
    uint64_t window_id, sequence;
    const char* session_id;
    const char* title;
    const char* document_path;
} HA_EditorStateV1;
static inline const HA_EditorStateV1* HA_GetEditorState(const HA_InteractionV1* event) {
    if(!event || event->size < offsetof(HA_InteractionV1,editor) + sizeof(event->editor) ||
       !event->editor || event->editor->size < sizeof(HA_EditorStateV1)) return NULL;
    return event->editor;
}
/* Updates only this add-on's HA_LABEL/HA_TEXT_VIEW controls (UTF-8, <=4096 bytes).
   Returns 1 on success, 0 for unsupported hosts, wrong ownership/type or busy state.
   Callbacks may update their own panel; UI changes appear on the next refresh. */
static inline int HA_SetPanelText(const HA_HostV1* host,uint64_t panel,const char* control,const char* value) {
    const HA_ExtensionsV1* ext=HA_GetExtensions(host);
    if(!ext || ext->size < offsetof(HA_ExtensionsV1,set_panel_text) + sizeof(ext->set_panel_text) ||
       !ext->set_panel_text) return 0;
    return ext->set_panel_text(host->context,panel,control,value);
}
#ifdef __cplusplus
}
#endif
#endif
