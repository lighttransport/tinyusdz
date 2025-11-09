sess_id=`cat sess_id.txt | tr -d '\r'`
sess_header="mcp-session-id: ${sess_id}"

curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "${sess_header}" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get_version",
      "arguments": {
      }
    },
    "id": 2
  }' \
  http://localhost:8085/mcp
