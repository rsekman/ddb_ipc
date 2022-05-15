#include <iostream>
#include <random>
#include <sstream>
#include <fstream>
#include <stdexcept>

#include <limits.h>
#include <nlohmann/json.hpp>

#include "ddb_ipc.hpp"
#include "commands.hpp"
#include "properties.hpp"
#include <deadbeef/deadbeef.h>
#include <deadbeef/plugins/artwork/artwork.h>

using json = nlohmann::json;
namespace ddb_ipc {

json command_play(int id, json args) {
    ddb_api->sendmessage(DB_EV_PLAY_CURRENT, 0, 0, 0);
    return ok_response(id);
}

json command_pause(int id, json args) {
    ddb_api->sendmessage(DB_EV_PAUSE, 0, 0, 0);
    return ok_response(id);
}

json command_play_pause(int id, json args) {
    ddb_api->sendmessage(DB_EV_TOGGLE_PAUSE, 0, 0, 0);
    return ok_response(id);
}

json command_stop(int id, json args) {
    ddb_api->sendmessage(DB_EV_STOP, 0, 0, 0);
    return ok_response(id);
}

json command_prev(int id, json args) {
    ddb_api->sendmessage(DB_EV_PREV, 0, 0, 0);
    return ok_response(id);
}

json command_next(int id, json args) {
    ddb_api->sendmessage(DB_EV_NEXT, 0, 0, 0);
    return ok_response(id);
}

void validate_arguments(arg_schema schema, json args) {
    arg_t arg;
    std::string arg_name;
    std::ostringstream  error;
    bool invalid_type = false;
    std::string type_name;
    for (auto i = schema.begin(); i != schema.end(); i++) {
        arg_name = i->first;
        arg = i->second;
        if ( args.contains(arg_name)) {
            switch ( arg.type ) {
                case ARG_POLYMORPHIC:
                    break;
                case ARG_NUMBER:
                    if (!args[arg_name].is_number() ){
                        invalid_type = true;
                        type_name = "a number";
                    }
                    break;
                case ARG_INT:
                    if (!args[arg_name].is_number_integer() ){
                        invalid_type = true;
                        type_name = "an integer";
                    }
                    break;
                case ARG_STRING:
                    if (!args[arg_name].is_string() ){
                        invalid_type = true;
                        type_name = "a string";
                    }
                    break;
            }
            if (invalid_type) {
                error << "Argument " << arg_name << " must be " << type_name;
                throw std::invalid_argument(error.str());
            }
        } else if(arg.mandatory) {
            error << "Argument " << i->first << " is mandatory";
            throw std::invalid_argument(error.str());
        }
    }
}

json command_set_volume(int id, json args) {
    arg_schema as = {
        {"volume", {true, ARG_NUMBER}}
    };
    try {
        validate_arguments(as, args);
    } catch (std::invalid_argument& e) {
        return bad_request_response(id, e.what());
    }
    float vol = args["volume"];
    float mindb = ddb_api->volume_get_min_db();
    if(vol > 100 || vol < 0) {
        return bad_request_response(id, std::string("Argument volume must be from [0, 100]"));
    }
    ddb_api->volume_set_db( (1-vol/100) * mindb );
    return ok_response(id);
}

json command_adjust_volume(int id, json args) {
    arg_schema as = {
        {"adjustment", {true, ARG_NUMBER}}
    };
    try {
        validate_arguments(as, args);
    } catch (std::invalid_argument& e) {
        return bad_request_response(id, e.what());
    }
    float adj = ((float) args["adjustment"])/100;
    if(adj > 1 || adj < -1) {
        return bad_request_response(id, std::string("Argument adjustment must be from [-1, 1]"));
    }
    float mindb = ddb_api->volume_get_min_db();
    float voldb = ddb_api->volume_get_db();
    ddb_api->volume_set_db(voldb - mindb * adj);
    return ok_response(id);
}

json command_toggle_mute(int id, json args) {
    DDB_IPC_DEBUG << "Toggling mute." << std::endl;
    int m = ddb_api->audio_is_mute();
    if(m) {
        ddb_api->audio_set_mute(0);
    } else {
        ddb_api->audio_set_mute(1);
    }
    ddb_api->sendmessage(DB_EV_CONFIGCHANGED, 0, 0, 0);
    return ok_response(id);
}

json command_get_playpos(int id, json args) {
    DB_playItem_t* cur = ddb_api->streamer_get_playing_track();
    if(!cur) {
        return error_response(id, "Not playing.");
    }
    float dur = ddb_api->pl_get_item_duration(cur);
    float playpos = ddb_api->streamer_get_playpos();
    json resp = ok_response(id);
    resp.merge_patch(
        json {
            {"status", DDB_IPC_RESPONSE_OK},
            {"data", {
                {"duration", dur},
                {"position", playpos}
                }
            }
        }
    );
    ddb_api->pl_item_unref(cur);
    return resp;
}
json command_seek(int id, json args) {
    // TODO implement seeking
    arg_schema as = {
        {"percent", {false, ARG_NUMBER}},
        {"seconds", {false, ARG_NUMBER}}
    };
    try {
        validate_arguments(as, args);
        if(args.contains("percent") == args.contains("seconds")) {
            throw std::invalid_argument("Exactly one of the percent and seconds arguments must be specified.");
        }
    } catch (std::invalid_argument& e) {
        return bad_request_response(id, e.what());
    }
    DB_playItem_t* cur = ddb_api->streamer_get_playing_track();
    json resp;
    if(!cur) {
        return error_response(id, "Not playing.");
    }
    float dur = ddb_api->pl_get_item_duration(cur);
    uint32_t pos;
    if(args.contains("percent")) {
        if(args["percent"] < 0 || args["percent"] > 1){
            resp = bad_request_response(id, "Argument percent must be from [0, 1].");
        } else {
            pos = dur * 1000 * (float) args["percent"]; // milliseconds
            ddb_api->sendmessage(DB_EV_SEEK, 0, pos, 0);
            resp = ok_response(id);
        }
    }
    if(args.contains("seconds")) {
        if(args["seconds"] < 0){
            resp = bad_request_response(id, "Argument seconds must be non-negative.");
        } else if (args["seconds"] > dur) {
            resp = error_response(id, "Attempt to seek beyond end.");
        } else {
            pos = 1000 * (float) args["seconds"]; // milliseconds
            ddb_api->sendmessage(DB_EV_SEEK, 0, pos, 0);
            resp = ok_response(id);
        }
    }
    ddb_api->pl_item_unref(cur);
    return resp;
}

json command_get_currently_playing(int id, json args) {
    // TODO implement
    // if arg format present, return using that format
    // otherwise return using default format
    return error_response(id, std::string("Not implemented"));
}

json command_toggle_stop_after_current_track(int id, json args) {
    int stop = ddb_api->conf_get_int("playlist.stop_after_current", 0);
    DDB_IPC_DEBUG << "Toggling stop after current track from " << stop << std::endl;
    if (stop) {
        ddb_api->conf_set_int("playlist.stop_after_current", 0);
    } else {
        ddb_api->conf_set_int("playlist.stop_after_current", 1);
    }
    ddb_api->sendmessage(DB_EV_CONFIGCHANGED, 0, 0, 0);
    return ok_response(id);
}

json command_toggle_stop_after_current_album(int id, json args) {
    DDB_IPC_DEBUG << "Toggling stop after current album." << std::endl;
    int stop = ddb_api->conf_get_int("playlist.stop_after_album", 0);
    if (stop) {
        ddb_api->conf_set_int("playlist.stop_after_album", 0);
    } else {
        ddb_api->conf_set_int("playlist.stop_after_album", 1);
    }
    ddb_api->sendmessage(DB_EV_CONFIGCHANGED, 0, 0, 0);
    return ok_response(id);
}

std::random_device rd;
std::mt19937 mersenne_twister(rd());
auto dist = std::uniform_int_distribution<long>(LONG_MIN, LONG_MAX);

typedef struct {
    int socket;
    int id;
} response_addr_t;

void callback_cover_art_found (int error, ddb_cover_query_t *query, ddb_cover_info_t *cover) {
    response_addr_t* addr  = (response_addr_t*) (query->user_data);
    json response = json {
        {"request_id", addr->id}
    };
    DDB_IPC_DEBUG << "Entered cover art callback for descriptor " << addr->socket << std::endl;
    if (
        (query->flags & DDB_ARTWORK_FLAG_CANCELLED) ||
        cover == NULL ||
        cover->image_filename == NULL
    ) {
        response["status"] = DDB_IPC_RESPONSE_ERR;
        response["message"] = "No cover art found";
    } else {
        response["cover-art-filename"] = cover->image_filename;

    }
    send_response(response, addr->socket);
    ddb_api->pl_item_unref(query->track);
    free(query->user_data);
    free(query);
}

ddb_cover_query_t* cover_query = NULL;
json command_request_cover_art(int id, json args) {
    DB_playItem_t* cur = ddb_api->streamer_get_playing_track();
    if (!cur) {
        return error_response(id, "Not playing");
    }
    ddb_api->pl_item_ref(cur);
    int64_t sid = dist(mersenne_twister);
    DDB_IPC_DEBUG << "Received cover art request, dispatching with sid=" << sid << std::endl;
    ddb_cover_query_t* cover_query = (ddb_cover_query_t*) calloc(sizeof(ddb_cover_query_t), 1);
    cover_query->flags = 0;
    cover_query->track = (DB_playItem_t*) cur;
    cover_query->source_id = sid;
    cover_query->user_data = malloc(sizeof(response_addr_t));
    cover_query->_size = sizeof(ddb_cover_query_t);
    response_addr_t addr = {
        .socket = args["socket"],
        .id = id,
    };
    *(response_addr_t*) cover_query->user_data = addr;
    ddb_artwork->cover_get(cover_query, callback_cover_art_found);
    DDB_IPC_DEBUG << "Sent cover art query" << std::endl;
    return ok_response(id);
}


std::map<std::string, ipc_command> commands = {
    {"play", command_play},
    {"pause", command_pause},
    {"play-pause", command_play_pause},
    {"stop", command_stop},
    {"set-volume", command_set_volume},
    {"adjust-volume", command_adjust_volume},
    {"toggle-mute", command_toggle_mute},
    {"seek", command_seek},
    {"get-playpos", command_get_playpos},
    {"toggle-stop-after-current-track", command_toggle_stop_after_current_track},
    {"toggle-stop-after-current-album", command_toggle_stop_after_current_album},
    {"request-cover-art", command_request_cover_art},
    {"get-property", command_get_property},
    {"set-property", command_set_property},
    {"observe-property", command_observe_property}
};

}
