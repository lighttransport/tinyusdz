set -v

hostname=localhost
port_no=8085
entrypoint=mcp

#      "protocolVersion": "2025-03-26",
#      "protocolVersion": "2024-11-05",
#  -H "Host: localhost:8086" \
#  -H "Connection:Keep-Alive" \
#  -H "Sec-Fetch-Mode: cors" \
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -D response_headers.txt \
  -d '{
    "jsonrpc": "2.0",
    "method": "initialize",
    "params": {
     "protocolVersion": "2024-11-05",
      "capabilities": {
      },
      "clientInfo": {
        "name": "curl-client",
        "version": "1.0.0"
      }
    },
    "id": 0
  }' \
  http://${hostname}:${port_no}/${entrypoint}

grep -i "mcp-session-id" response_headers.txt | cut -d' ' -f2 > sess_id.txt

sleep 1

# remove '\r'
sess_id=`cat sess_id.txt | tr -d '\r'`
sess_header="mcp-session-id: ${sess_id}"
#echo $sess_header

curl -v -X POST \
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
