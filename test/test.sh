#!/usr/bin/sh
# usage: test.sh file.json
# pretty-prints file.json, sends it to DeaDBeeF, and pretty-prints the response
echo "Request:"
jq . $1
echo "Response:"
cat $1 | socat - /tmp/ddb_socket | jq .
