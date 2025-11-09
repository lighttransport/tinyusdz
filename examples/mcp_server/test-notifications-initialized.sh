set -v

hostname=localhost
port_no=8085
#port_no=5173
entrypoint=mcp
#entrypoint=""

# Remove \r\n 
sess_id=`echo $(cat sess_id.txt) | tr -d '\r'`

sess_header="mcp-session-id: ${sess_id}"
#sess_header="mcp-session-id: 4486cfdc-39ce-49c4-bac5-8d3d7fb0689a"
echo $sess_header

curl -v -X POST \
  -H "Connection: Keep-Alive" \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "${sess_header}" \
  -d '{
    "jsonrpc": "2.0",
    "method": "notifications/initialized"
  }' \
  http://${hostname}:${port_no}/${entrypoint}

#curl -X POST \
#  -H "Content-Type: application/json" \
#  -d '{"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"claude-ai","version":"0.1.0"}},"jsonrpc":"2.0","id":0}' \
#  http://localhost:8085/mcp
