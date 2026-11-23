#!/bin/bash
# usage: mcp.sh METHOD PARAMS_JSON
U=http://127.0.0.1:8089/mcp
H=(-H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream')
SID=$(curl -s -m 10 -D - -o /dev/null "${H[@]}" -X POST $U -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"client","version":"1.0"}}}' | grep -i mcp-session-id | awk '{print $2}' | tr -d '\r')
curl -s -m 10 -o /dev/null "${H[@]}" -H "mcp-session-id: $SID" -H 'mcp-protocol-version: 2025-06-18' -X POST $U -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
curl -s -m 300 "${H[@]}" -H "mcp-session-id: $SID" -H 'mcp-protocol-version: 2025-06-18' -X POST $U -d "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"$1\",\"params\":$2}" | sed -n 's/^data: //p'
