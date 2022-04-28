#include <iostream>

#include <nlohmann/json.hpp>
#include "ddb_ipc.hpp"
#include "commands.hpp"
#include "properties.hpp"
#include <deadbeef/deadbeef.h>

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

json command_set_volume(int id, json args) {
    if( !args.contains("volume") ) {
        return bad_request_response(id, std::string("Argument volume is mandatory"));
    }
    if(!args["volume"].is_number()) {
        return bad_request_response(id, std::string("Argument volume must be a float"));
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
    if( !args.contains("adjustment") ) {
        return bad_request_response(id, std::string("Argument adjustment is mandatory"));
    }
    if( !args["adjustment"].is_number() ) {
        return bad_request_response(id, std::string("Argument adjustment must be a float"));
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
    return resp;
}
json command_seek(int id, json args){
    // TODO implement seeking
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
  {"get-property", command_get_property},
  {"set-property", command_set_property},
  {"observe-property", command_observe_property}
};

}
