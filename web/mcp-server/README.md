# mcp-server

To install dependencies:

```bash
bun install
```

To run:

```bash
bun run index.ts
```

This project was created using `bun init` in bun v1.2.13. [Bun](https://bun.sh) is a fast all-in-one JavaScript runtime.

## Claude for Desktop

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

NOTE: No authorization(API key) required at the moment.
NOTE: It is recommended to terminate Claude for Desktop process from Task Manager(Windows) or Force quit app(macOS), then restart to reflect changes of .json.
