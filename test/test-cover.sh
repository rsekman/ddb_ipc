#!/usr/bin/sh
rm test-cover.jpg
jq . test-cover.json
# sleep to let asynchronous request finish
(cat test-cover.json; sleep 1) |
    socat - /tmp/ddb_socket |
    jq -r ".blob?" |
    sed -n -e '2p' |
    # discard the first message, it just tells us that the request was accepted
    base64 -d |
    feh -
