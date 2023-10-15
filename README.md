# DeaDBeeF IPC
IPC Plugin for the DeaDBeeF Audio Player

Allows other programs to communicate with and control DeaDBeeF by reading and writing JSON messages to a socket.
Inspired by the corresponding functionality in `mpv --input-ipc-server`.

I mainly developed this to enable [`ur-ddb-ipc`](https://github.com/rsekman/ur-ddb-ipc).
Together they will let you monitor and control DeaDBeeF from your smartphone.

## Dependencies

- DeaDBeeF 1.8.8
- nlohmann-json

## Building

```sh
export CPATH=/path/to/deadbeef/
export DDB_IPC_DEBUG_LEVEL=n
make install
```
`ddb_ipc` uses the artwork plugin bundled with DeaDBeeF and needs to be able to find `deadbeef/plugins/artwork/artwork.h`.
Clone https://github.com/DeaDBeeF-Player/deadbeef and add it to your `CPATH`
Set `n = 0, 1, 2, 3` for increasingly verbose console logging on `stderr`;
the default is `3`.

`ddb_ipc` is Linux only with no plans to support other operating systems.

## Usage

Configure a path to the communication socket (default: `/tmp/ddb_socket`).
Read and write JSON to it.

```sh
% tee >(jq .) < cmd | socat - /tmp/ddb_socket | jq .
{
  "command": "get-now-playing",
  "args": {
    "format": "%artist% - '['%album% - #%tracknumber%']' %title%"
  },
  "request_id": 1
}
{
  "now-playing": "Blind Guardian - [Nightfall In Middle-Earth - #04] Nightfall",
  "request_id": 1,
  "status": "OK"
}

```

## Protocol

The protocol is inspired by, but does not follow precisely, that of `mpv`.
All messages are JSON dictionaries terminated by newlines `\n`.

There is no authentication, handshake, or session management. 

### Requests

Each request to `ddb_ipc` shall contain the key `command` (a string), and may optionally contain the keys `request_id` (an integer) and `args` (a dictionary).
Other keys are ignored.
`ddb_ipc` will send a response to each request.

### Responses

Each response from `ddb_ipc` shall contain the key `status` (a string).
The key `status` takes one of three values (`"OK", "ERROR", "BAD REQUEST")` indicating success, an error, or a malformed message (e.g., type errors), respectively.
Responses in the latter two categories may contain the key `message` (a string) describing what went wrong.

If the request being responded to contained the key `request_id`, the response shall contain also contain the key `request_id`, with the same value.
Message ids are optional and are **not** constrained to be sequential or unique, or in any other way; they are simply copied from request to response and their semantics are up to the client.

The response may contain any other keys as appropriate.


### Events

`ddb_ipc` may send messages to clients when certain events occur in the player.
Event messages shall contain the key `event` (a string), and may contain other keys as appropriate.


### Commands

Commands are identified by name and may take any number of typed, possibly optional, keyword arguments in the `args` dictionary.
Unknown keys in `args` are ignored.
Example valid commands are as follows:

```json
{"command":"get-playpos","request_id":1}
{"command":"get-property","args":{"property":"volume"},"request_id":2}
{"command":"get-property","args":{"property":"playlist.stop_after_album"},"request_id":8}
{"command":"seek","args":{"percent":0.49}}
{"command":"seek","args":{"percent":0.49,"foo":"bar"}}
```

Available commands and their call signatures are listed below.
Arguments are specified as `name::type`, followed by a `?` if the argument is optional, and `=default` if there is a default value.
Additional range restrictions may apply.

- `play` start the player
- `pause` pause the player
- `play-pause` toggle play/pause
- `play-num idx::int` play track number `idx` in the current playlist (zero-indexed)
- `prev-track` go the previous track
- `next-track` go the next track
- `stop` stop playing
- `set-volume volume::float` set the volume in percent.
    `volume` must be from `[0, 100]`.
- `adjust-volume adjustment::float` adjust the volume by `adjustment` percent.
    `adjustment` must be from `[-100, 100]`.
    DeaDBeeF clamps the adjusted volume to `[0, 100]`.
- `toggle-mute` toggle mute
- `seek percent::float? seconds::float?` seek to `percent` percent or `seconds` seconds.
    Exactly one of the arguments must be present.
    If present, `percent` must be from `[0, 100]`.
    If present, `seconds` must be non-negative.
    An error is returned if seeking beyond the end of the track.
- `get-playpos` returns `data`, a dictionary with the keys `position` and `duration`, both floats giving the position in the current track and its duration, in seconds.
    Returns an error if not currently playing.
- `get-now-playing format::string?="%artist% - %title%"` formats the currently playing track according to the title format string `format`, returning it with the key `now-playing`
    See upstream [DeaDBeeF](https://github.com/DeaDBeeF-Player/deadbeef/wiki/Title-formatting-2.0) and [foobar2000](https://wiki.hydrogenaud.io/index.php?title=Foobar2000:Title_Formatting_Reference) documentation for format string documentation.
    Returns an error if the format string is invalid.
    Returns an error if not currently playing.
- `get-current-playlist idx::int` gets the title and index of the current playlist
    Returns an error if there is no current playlist.
- `set-current-playlist idx::int` sets the current playlist by index.
    Returns an error if unsuccessful, e.g. because `idx` is out of range.
- `get-playlist-contents idx::int format::string?=%artist% - %title%"` Gets the contents of the playlist with index `idx` formatted according to `format`.
    Returns an error if `idx` is out of range.
    Returns an error if the format string is invalid.
- `request-cover-art accept::[string]?=["filename"]` issues a request for the cover art for the currently playing track.
    Cover art look-up is asynchronous.
    An initial `OK` response to this command therefore only indicates a successful *request*.
    When the request returns, a second message with the same `request_id` will be sent asynchronously, containing the cover art data itself.
    If no cover art was found, the second response will be an error.
    If present, `accept` must contain at least one of `"filename"` and `"blob"`.
    If `accept` contains `"filename"`, the response will contain the key `filename` with an absolute path to the (cached) cover art.
    If `accept` contains `"blob`", the response will the contain the key `blob` with a base64-encoding of the cover art.
- `toggle-stop-after-current-track` toggle the stop after current track flag
- `toggle-stop-after-current-album` toggle the stop after current album flag
- `get_property`, `set_property`, `observe_property` these commands are documented in the next section

### Properties

Clients can read, modify, and monitor the configuration of DeaDBeeF using the `get_property`, `set_property`, and `observe_property` commands.
Property names are those found in DeaDBeeF's `config` file.

The `get_property property::string` command returns the requested property with the key `property`.
It is an int, a float, or a string, as appropriate.

The `set_property property::string value::(int (+) float (+) string)` command sets the specified `property` to the value `value`.
It is the caller's responsibility to ensure that `value` has the correct type, as the type of a property cannot be determined by the DeaDBeeF API.

The `observe_property property::string` command allows clients to subscribe to changes in the configuration.
On any subsequent change in the configuration (`DB_EV_CONFIGCHANGED`), an event message with `event: "property-change"` will be sent containing the keys `property` (a string) and `value` (typed as appropriate).
The DeaDBeeF API does not allow more fine-grained monitoring of configuration changes.
Hence, a `property-change` event is **not** a guarantee that the observed *actually changed*.
For example, a client observing `shuffle` and `repeat` will be told of both values even when only one changes.

For convenience, some properties have wrappers
- The property `shuffle` takes the values `"off", "tracks", "albums", "values"`
- The property `repeat` takes the values `"off", "one", "all"
- The property `mute` is returned as boolean (but is set and observed as an int, as C does not distinguish `bool` from `int`).

## License

GPL 3
