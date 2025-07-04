#include "properties.hpp"

#include <deadbeef/deadbeef.h>

#include <nlohmann/json.hpp>
#include <set>

#include "commands.hpp"
#include "ddb_ipc.hpp"
#include "response.hpp"

using json = nlohmann::json;

using json = nlohmann::json;

template <>
struct fmt::formatter<ddb_shuffle_t> {
    // Parses format specifiers; we can ignore them in this simple case.
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    // Format the enum value.
    template <typename FormatContext>
    auto format(ddb_shuffle_t c, FormatContext& ctx) const {
        std::string_view name = "Unknown";
        switch (c) {
            case DDB_SHUFFLE_OFF:
                name = "off";
                break;
            case DDB_SHUFFLE_ALBUMS:
                name = "albums";
                break;
            case DDB_SHUFFLE_RANDOM:
                name = "random";
                break;
            case DDB_SHUFFLE_TRACKS:
                name = "tracks";
                break;
        }
        return fmt::format_to(ctx.out(), "{}", name);
    }
};

template <>
struct fmt::formatter<ddb_repeat_t> {
    // Parses format specifiers; we can ignore them in this simple case.
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    // Format the enum value.
    template <typename FormatContext>
    auto format(ddb_repeat_t c, FormatContext& ctx) const {
        std::string_view name = "Unknown";
        switch (c) {
            case DDB_REPEAT_OFF:
                name = "off";
                break;
            case DDB_REPEAT_ALL:
                name = "all";
                break;
            case DDB_REPEAT_SINGLE:
                name = "single";
                break;
        }
        return fmt::format_to(ctx.out(), "{}", name);
    }
};

namespace ddb_ipc {

std::map<int, std::set<std::string>> observers = {};

json get_property_volume() {
    float mindb = ddb_api->volume_get_min_db();
    float voldb = ddb_api->volume_get_db();
    return 100 * (mindb - voldb) / mindb;
}

json get_property_mute() { return (json::boolean_t)ddb_api->audio_is_mute(); }

json get_property_shuffle() {
    auto logger = get_logger();
    auto shuffles = std::map<ddb_shuffle_t, std::string>{
        {DDB_SHUFFLE_OFF, "off"},
        {DDB_SHUFFLE_TRACKS, "tracks"},
        {DDB_SHUFFLE_ALBUMS, "albums"},
        {DDB_SHUFFLE_RANDOM, "random"}
    };
    auto shuff = (ddb_shuffle_t)ddb_api->conf_get_int("playback.order", 0);
    logger->debug("shuffle: {}", shuff);
    if (shuffles.count(shuff)) {
        return shuffles[shuff];
    } else {
        return json{"n/a"};
    }
}

json get_property_repeat() {
    auto logger = get_logger();
    auto repeats = std::map<ddb_repeat_t, std::string>{
        {DDB_REPEAT_ALL, "all"},
        {DDB_REPEAT_OFF, "off"},
        {DDB_REPEAT_SINGLE, "one"},
    };
    auto rep = (ddb_repeat_t)ddb_api->conf_get_int("playback.loop", 0);
    logger->debug("repeat: {}", rep);
    if (repeats.count(rep)) {
        return repeats[rep];
    } else {
        return json{"n/a"};
    }
}

void set_property_shuffle(json arg) {
    auto logger = get_logger();
    auto shuffles = std::map<std::string, ddb_shuffle_t>{
        {"off", DDB_SHUFFLE_OFF},
        {"tracks", DDB_SHUFFLE_TRACKS},
        {"albums", DDB_SHUFFLE_ALBUMS},
        {"random", DDB_SHUFFLE_RANDOM}
    };
    if (!arg.is_string()) {
        throw std::invalid_argument(
            "Invalid argument: shuffle must be a string."
        );
    }
    if (shuffles.count(arg)) {
        logger->debug("setting shuffle: {}", shuffles.at(arg));
        ddb_api->conf_set_int("playback.order", (int)shuffles.at(arg));
        ddb_api->sendmessage(DB_EV_CONFIGCHANGED, 0, 0, 0);
    } else {
        throw std::invalid_argument(
            "Invalid argument:shuffle must be one of: off, tracks, albums, "
            "random."
        );
    }
}

void set_property_repeat(json arg) {
    auto repeats = std::map<std::string, ddb_repeat_t>{
        {"all", DDB_REPEAT_ALL},
        {"off", DDB_REPEAT_OFF},
        {"one", DDB_REPEAT_SINGLE},
    };
    if (!arg.is_string()) {
        throw std::invalid_argument(
            "Invalid argument: repeat must be a string."
        );
    }
    try {
        ddb_api->conf_set_int("playback.loop", (int)repeats.at(arg));
        ddb_api->sendmessage(DB_EV_CONFIGCHANGED, 0, 0, 0);
        // ddb_api->streamer_set_repeat(values.at(value));
    } catch (std::invalid_argument& e) {
        throw std::invalid_argument(
            "Invalid argument:repeat must be one of: off, one, all."
        );
    }
}

json property_as_json(std::string prop) {
    char buf[1024];
    char def = '\0';
    ddb_api->conf_get_str(prop.c_str(), &def, buf, sizeof(buf));
    std::string value(buf);
    try {
        return stoll(value);
    } catch (std::invalid_argument& e) {
    }
    try {
        return stod(value);
    } catch (std::invalid_argument& e) {
    }
    return value;
}

// properties that should be processed before returning
std::map<std::string, ipc_property_getter> getters = {
    {"volume", get_property_volume},
    {"shuffle", get_property_shuffle},
    {"repeat", get_property_repeat},
    {"mute", get_property_mute},
};

class GetPropertyArgument : Argument {
  public:
    std::string property;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GetPropertyArgument, property);
COMMAND(get_property, GetPropertyArgument) {
    std::string prop = args.property;
    json resp = ok_response(id);
    resp["property"] = args.property;
    if (getters.count(prop)) {
        resp["value"] = getters[prop]();
    } else {
        resp["value"] = property_as_json(prop);
    }
    return (resp);
}

std::map<std::string, ipc_property_setter> setters = {
    {"shuffle", set_property_shuffle},
    {"repeat", set_property_repeat},
};

class SetPropertyArgument : public GetPropertyArgument {
  public:
    json value;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SetPropertyArgument, property, value)
COMMAND(set_property, SetPropertyArgument) {
    std::string prop = args.property;
    if (setters.count(prop)) {
        try {
            setters[prop](args.value);
        } catch (std::exception& e) {
            return error_response(id, e.what());
        }
        return ok_response(id);
    }
    const char* propc = prop.c_str();
    if (args.value.is_number_integer()) {
        ddb_api->conf_set_int(propc, args.value);
    } else if (args.value.is_number_float()) {
        ddb_api->conf_set_float(propc, args.value);
    } else if (args.value.is_string()) {
        std::string val = args.value;
        ddb_api->conf_set_str(propc, val.c_str());
    } else {
        return bad_request_response(
            id, std::string("Argument property must be a string or number")
        );
    }
    return ok_response(id);
}

class ObservePropertyArgument : public GetPropertyArgument {
  public:
    int socket;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ObservePropertyArgument, property, socket)
COMMAND(observe_property, ObservePropertyArgument) {
    int s = (int)args.socket;
    if (!observers.count(s)) {
        observers[s] = std::set<std::string>({});
    }
    observers[s].insert((std::string)args.property);
    return ok_response(id);
}
}  // namespace ddb_ipc
