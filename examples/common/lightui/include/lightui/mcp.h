/*
 * lightui/mcp.h — MCP (Model Context Protocol) server for lightui
 *
 * Exposes the widget tree, surface, and theme to LLM-based tools
 * (e.g. Claude Code) for interactive debugging and inspection.
 *
 * Transports:
 *   - stdio:  JSON-RPC over stdin/stdout (newline-delimited)
 *   - HTTP:   JSON-RPC over HTTP POST (Streamable HTTP transport)
 *
 * Usage:
 *   lui_mcp_t *mcp = lui_mcp_create(&(lui_mcp_config_t){
 *       .transport = LUI_MCP_STDIO
 *   });
 *   lui_mcp_set_context(mcp, &ui_ctx, surface);
 *   // in event loop:
 *   lui_mcp_poll(mcp);
 *   // cleanup:
 *   lui_mcp_destroy(mcp);
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_MCP_H
#define LIGHTUI_MCP_H

#include <lightvg/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — avoid pulling in full headers */
#ifndef LUI_WIDGET_T_DEFINED
#define LUI_WIDGET_T_DEFINED
typedef struct lui_widget  lui_widget_t;
#endif
typedef struct lui_ui_ctx  lui_ui_ctx_s;  /* avoid conflict with typedef in layout.h */
struct lui_surface;
struct lui_theme;

typedef enum {
    LUI_MCP_STDIO,      /* JSON-RPC over stdin/stdout           */
    LUI_MCP_HTTP,        /* JSON-RPC over HTTP POST              */
} lui_mcp_transport_t;

typedef struct {
    lui_mcp_transport_t transport;
    const char         *host;       /* HTTP only (default "127.0.0.1")  */
    int                 port;       /* HTTP only (default 3001)         */
} lui_mcp_config_t;

typedef struct lui_mcp lui_mcp_t;

/**
 * Create an MCP server with the given transport configuration.
 * For HTTP, starts listening immediately.
 * Returns NULL on failure.
 */
lui_mcp_t *lui_mcp_create(const lui_mcp_config_t *config);

/**
 * Set the UI context and surface for inspection tools.
 * These pointers are not owned; they must remain valid while the MCP
 * server is running. Either may be NULL (tools that need them will
 * return errors).
 */
void lui_mcp_set_context(lui_mcp_t *mcp,
                          void *ui_ctx,           /* lui_ui_ctx_t* */
                          struct lui_surface *surface);

/** Set the current theme (for get_theme / set_theme tools). */
void lui_mcp_set_theme(lui_mcp_t *mcp, struct lui_theme *theme);

/**
 * Poll for incoming MCP messages and handle them (non-blocking).
 * Call this once per frame or in your event loop.
 * Returns the number of messages handled.
 */
int lui_mcp_poll(lui_mcp_t *mcp);

/** Destroy the MCP server and free resources. */
void lui_mcp_destroy(lui_mcp_t *mcp);

/**
 * Custom tool handler callback.
 *
 * @param args_json   JSON string of the tool arguments (may be NULL / "{}").
 * @param args_len    Length of args_json (or -1 for NUL-terminated).
 * @param user_data   Opaque pointer passed at registration time.
 * @param out_json    Set *out_json to a malloc'd JSON content array string
 *                    e.g. "[{\"type\":\"text\",\"text\":\"hello\"}]".
 *                    The MCP server will free() it.
 * @param out_json_len Set *out_json_len to its length (or -1 for strlen).
 */
typedef void (*lui_mcp_tool_handler_fn)(
    const char *args_json, int args_len,
    void *user_data,
    char **out_json, int *out_json_len);

/**
 * Register a custom tool on the MCP server.
 *
 * @param mcp          MCP server instance.
 * @param name         Tool name (will be copied).
 * @param description  Human-readable description (will be copied).
 * @param input_schema Raw JSON string for inputSchema (will be copied).
 * @param handler      Callback invoked when the tool is called.
 * @param user_data    Opaque pointer forwarded to handler.
 * @return 0 on success, -1 on failure (name too long, table full, duplicate).
 */
int lui_mcp_register_tool(lui_mcp_t *mcp,
    const char *name, const char *description,
    const char *input_schema,
    lui_mcp_tool_handler_fn handler, void *user_data);

/* ---- Canvas debugging context ------------------------------------------- */

/**
 * Set a canvas for debugging tools (inspect_pixel, inspect_region, etc.).
 * The canvas is not owned by the MCP server; must remain valid.
 */
void lui_mcp_set_canvas(lui_mcp_t *mcp, void *canvas /* lvg_canvas_t* */);

/**
 * Set a font for the measure_text tool.
 * The font is not owned; caller must keep it alive.
 */
void lui_mcp_set_font(lui_mcp_t *mcp, void *font /* lui_font_t* */);

/**
 * Get the active drawing recorder (for application-level dual-write).
 * Returns NULL if recording is not active.
 *
 * Usage in draw code:
 *   lui_recorder_t *rec = lui_mcp_get_recorder(mcp);
 *   if (rec) lui_rec_fill_rect(rec, x, y, w, h, color);
 *   lvg_canvas_fill_rect(&canvas, x, y, w, h, color);
 */
struct lui_recorder;
void *lui_mcp_get_recorder(lui_mcp_t *mcp);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_MCP_H */
