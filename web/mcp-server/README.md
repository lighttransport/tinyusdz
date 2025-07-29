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

NOTE: No authorization(API key) supported at the moment.
NOTE: It is recommended to terminate Claude for Desktop process from Task Manager(Windows) or Force quit app(macOS), then restart to reflect changes of `claude_desktop_config.json`

## TODO

* Run MCP server in a browser(service worker or 
