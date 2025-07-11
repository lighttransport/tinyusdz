curl -X POST \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tusdcat",
      "arguments": {
        "text": "#usda 1.0\n def \"bora\" { }"
      },
    },
    "id": 2
  }' \
  http://localhost:8085/mcp
