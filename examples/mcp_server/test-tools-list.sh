curl -X POST \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/list",
    "params": {
      },
    },
    "id": 2
  }' \
  http://localhost:8085/mcp
