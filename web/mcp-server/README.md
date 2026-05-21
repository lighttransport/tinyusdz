# mcp-server

To install dependencies:

```bash
$ bun install
# or node install

```

## Run server

To run:

```bash
$ bun server-http.js
# or node server-http.js
```

In default settings, this will start MCP server at http://localhost:8085/mcp

### Setup asset data

```bash
$ bun setup-asset.js
# or node setup-asset.js
```

## Connect from Claude for Desktop

Curently we only support connecting MCP through developer config(Stdio transport)

Install nodejs(20.x or later).
Install `mcp-remote` package. https://www.npmjs.com/package/mcp-remote
(`npx mcp-remote`) 

Then, edit `claude_desktop_config.json` (Through `Settings` -> `Developer` -> `Edit Config`) to route 

```
{
  "mcpServers": {
    "tinyusdz": {
      "command": "npx",
      "args": [
        "mcp-remote",
        "http://localhost:8085/mcp"
      ]
    }
  }
}
```

It will be required to add `"-y"` to args if you didn't install `mcp-remote` yet.

NOTE: It is recommended to terminate Claude for Desktop process from Task Manager(Windows) or Force quit app(macOS), then restart to reflect changes of `claude_desktop_config.json`

## Security

The HTTP MCP server requires a bearer token and binds to loopback by default:

* **Bind address** — defaults to `127.0.0.1` (not reachable from the LAN). Set
  `MCP_HOST=0.0.0.0` only if you intentionally want to expose it.
* **Authentication** — every `/mcp` request must send
  `Authorization: Bearer <token>`. Set the token with `MCP_AUTH_TOKEN`. If unset,
  an ephemeral token is generated and printed to the console on startup.
* **CORS** — cross-origin browser access is denied by default. Opt specific
  origins in with `MCP_ALLOWED_ORIGINS` (comma-separated).
* **DNS-rebinding protection** — only requests whose `Host` header is in
  `MCP_ALLOWED_HOSTS` are accepted (defaults to the loopback host/port).

```bash
MCP_AUTH_TOKEN=$(openssl rand -hex 32) bun server-http.js
# client must then send:  Authorization: Bearer $MCP_AUTH_TOKEN
```

NOTE: tools operate on a process-global context shared across sessions; run one
trusted client per server process until per-session isolation lands.

## TODO

* Run MCP server in a browser(service worker or 
