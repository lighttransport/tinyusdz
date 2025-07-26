
## Requirements

* nodejs(npx) : v20.x or later

## Filesystem MCP server

To expose local files to MCP clients and Claude for Desktop,
Please use filesystem MCP server.

### claude_desktop_config.json


```json
{
 "mcpServers": {
    ...,
    "file-system": {
      "command": "npx",
      "args": [
        "-y",
        "@modelcontextprotocol/server-filesystem",
        "/Users/yourname/USDAssets",
        "/Users/yourname/AnotherUSDAssets",
      ]
    }
  }
}
```	

You can omit `-y` arg if you already installed `@modelcontextprotocol/server-filesystem`
