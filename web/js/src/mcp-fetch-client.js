// Minimal MCP (Model Context Protocol) client over plain HTTP fetch.
//
// The TinyUSDZ native MCP server (src/tydra/mcp-server.cc, civetweb) speaks
// JSON-RPC 2.0 over a single POST /mcp endpoint, returns a plain JSON body, and
// carries the session id in an `mcp-session-id` response header on initialize.
// It does NOT implement the streamable-HTTP/SSE transport, so the official
// @modelcontextprotocol/sdk transport does not fit — this tiny client matches
// the server exactly. Browser-friendly (uses fetch); requires the server to
// send CORS headers (it echoes the request Origin).

export class McpFetchClient {
  constructor(url) {
    this.url = url;
    this.sid = null;
    this._id = 0;
  }

  async _rpc(method, params, { notification = false } = {}) {
    const body = { jsonrpc: '2.0', method };
    if (!notification) body.id = ++this._id;
    if (params !== undefined) body.params = params;

    const headers = { 'Content-Type': 'application/json' };
    if (this.sid) headers['mcp-session-id'] = this.sid;

    const resp = await fetch(this.url, {
      method: 'POST',
      headers,
      body: JSON.stringify(body),
    });

    const sid = resp.headers.get('mcp-session-id');
    if (sid) this.sid = sid;

    if (notification) return null;  // server returns 202, no body
    if (!resp.ok) throw new Error(`MCP HTTP ${resp.status} ${resp.statusText}`);
    const j = await resp.json();
    if (j.error) {
      throw new Error(j.error.message || JSON.stringify(j.error));
    }
    return j.result;
  }

  async connect() {
    const info = await this._rpc('initialize', {
      protocolVersion: '2025-03-26',
      clientInfo: { name: 'usddiff-web', version: '0' },
      capabilities: {},
    });
    // Best-effort; some servers require it before tools/call.
    try { await this._rpc('notifications/initialized', undefined, { notification: true }); } catch (_) { /* ignore */ }
    return info;
  }

  async listTools() {
    const r = await this._rpc('tools/list');
    return (r && r.tools) || [];
  }

  // Returns the tool's result object directly (this server does not wrap it in
  // MCP `content` blocks).
  async callTool(name, args) {
    return this._rpc('tools/call', { name, arguments: args || {} });
  }
}

// Base64-encode a Uint8Array without blowing the call stack on large inputs.
export function toBase64(bytes) {
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}
