curl -X POST \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "ping",
    "id": 123
  }' \
  http://localhost:8085/mcp
