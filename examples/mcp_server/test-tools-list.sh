hostname=localhost
port_no=8085
entrypoint=mcp

sess_id=`cat sess_id.txt | tr -d '\r'`
sess_header="mcp-session-id: ${sess_id}"

curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "${sess_header}" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/list",
    "params": {},
    "id": 2
  }' \
  http://${hostname}:${port_no}/${entrypoint}
