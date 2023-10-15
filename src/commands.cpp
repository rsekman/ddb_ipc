#include "commands.hpp"

#include <deadbeef/deadbeef.h>
#include <deadbeef/artwork.h>
#include <limits.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <stdexcept>

#include "../submodules/cpp-base64/base64.h"
#include "ddb_ipc.hpp"
#include "argument.hpp"
#include "response.hpp"
#include "properties.hpp"

using json = nlohmann::json;
namespace ddb_ipc {

COMMAND(play, Argument)
    ddb_api->sendmessage(DB_EV_PLAY_CURRENT, 0, 0, 0);
    return ok_response(id);
}

COMMAND(pause, Argument)
    ddb_api->sendmessage(DB_EV_PAUSE, 0, 0, 0);
    return ok_response(id);
}

COMMAND(play_pause, Argument)
    ddb_api->sendmessage(DB_EV_TOGGLE_PAUSE, 0, 0, 0);
    return ok_response(id);
}

COMMAND(stop, Argument)
    ddb_api->sendmessage(DB_EV_STOP, 0, 0, 0);
    return ok_response(id);
}

COMMAND(prev, Argument)
    ddb_api->sendmessage(DB_EV_PREV, 0, 0, 0);
    return ok_response(id);
}

COMMAND(next, Argument)
    ddb_api->sendmessage(DB_EV_NEXT, 0, 0, 0);
    return ok_response(id);
}

#if (DDB_API_LEVEL >= 17)

COMMAND(prev_album, Argument)
    ddb_api->sendmessage(DB_EV_PLAY_PREV_ALBUM, 0, 0, 0);
    return ok_response(id);
}

COMMAND(next_album, Argument)
    ddb_api->sendmessage(DB_EV_PLAY_NEXT_ALBUM, 0, 0, 0);
    return ok_response(id);
}

COMMAND(random_album, Argument)
    ddb_api->sendmessage(DB_EV_PLAY_RANDOM_ALBUM, 0, 0, 0);
    return ok_response(id);
}

#else

COMMAND(prev_album, Argument)
    return error_response(id,
        "Command requires API level >= 17, but API level is " +  std::to_string(DDB_API_LEVEL) + "."
    );
}

COMMAND(next_album, Argument)
    return error_response(id,
        "Command requires API level >= 17, but API level is " +  std::to_string(DDB_API_LEVEL) + "."
    );
}

#endif

class SetVolumeArgument : Argument {
    public:
        float volume;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SetVolumeArgument, volume);

COMMAND(set_volume, SetVolumeArgument)
    float vol = args.volume;
    float mindb = ddb_api->volume_get_min_db();
    if(vol > 100 || vol < 0) {
        return bad_request_response(id, std::string("Argument volume must be from [0, 100]"));
    }
    ddb_api->volume_set_db( (1-vol/100) * mindb );
    return ok_response(id);
}

class AdjustVolumeArgument : Argument {
    public:
        float adjustment;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AdjustVolumeArgument, adjustment);

COMMAND(adjust_volume, AdjustVolumeArgument)
    float adj = args.adjustment/100;
    if(adj > 1 || adj < -1) {
        return bad_request_response(id, std::string("Argument adjustment must be from [-1, 1]"));
    }
    float mindb = ddb_api->volume_get_min_db();
    float voldb = ddb_api->volume_get_db();
    ddb_api->volume_set_db(voldb - mindb * adj);
    return ok_response(id);
}

COMMAND(toggle_mute, Argument)
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

COMMAND(get_playpos, Argument)
    DB_playItem_t* cur = ddb_api->streamer_get_playing_track();
    if(!cur) {
        return error_response(id, "Not playing.");
    }
    float dur = ddb_api->pl_get_item_duration(cur);
    float playpos = ddb_api->streamer_get_playpos();
    json resp = ok_response(id,
        {{"data", {
            {"duration", dur},
            {"position", playpos}
        }}}
    );
    ddb_api->pl_item_unref(cur);
    return resp;
}

class SeekArgument : Argument {
    public:
        std::optional<float> percent = {};
        std::optional<float> seconds = {};
};
void from_json(const json& j, SeekArgument& a){
    SeekArgument def;
    a.percent = j.contains("percent") ? j.at("percent") : def.percent;
    a.seconds = j.contains("seconds") ? j.at("seconds") : def.seconds;
    if((bool) a.percent == (bool) a.seconds) {
        throw std::invalid_argument("Exactly one of the percent and seconds arguments must be specified.");
    }
    if (a.percent) {
        float f = a.percent.value();
        if(f < 0 || f > 100) {
            throw std::invalid_argument("Argument percent must be from [0, 100].");
        }
    }
    if (a.seconds && a.seconds.value() < 0) {
        throw std::invalid_argument("Argument percent must be from [0, 100].");
    }
}

COMMAND(seek, SeekArgument)
    DB_playItem_t* cur = ddb_api->streamer_get_playing_track();
    json resp;
    if(!cur) {
        return error_response(id, "Not playing.");
    }
    float dur = ddb_api->pl_get_item_duration(cur);
    uint32_t pos;
    if(args.percent) {
        pos = dur * 1000 *  args.percent.value()/100; // milliseconds
        ddb_api->sendmessage(DB_EV_SEEK, 0, pos, 0);
        resp = ok_response(id);
    }
    if(args.seconds) {
        if (args.seconds > dur) {
            resp = error_response(id, "Attempt to seek beyond end.");
        } else {
            pos = 1000 * args.seconds.value(); // milliseconds
            ddb_api->sendmessage(DB_EV_SEEK, 0, pos, 0);
            resp = ok_response(id);
        }
    }
    ddb_api->pl_item_unref(cur);
    return resp;
}

class GetNowPlayingArgument : Argument {
    public:
        std::string format = DDB_IPC_DEFAULT_FORMAT;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetNowPlayingArgument, format);

COMMAND(get_now_playing, GetNowPlayingArgument)
    DB_playItem_t* cur = ddb_api->streamer_get_playing_track();
    json resp;
    if(!cur) {
        return error_response(id, "Not playing.");
    }
    ddb_tf_context_t ctx = {
        ._size = sizeof(ddb_tf_context_t),
        .flags = 0,
        .it = cur,
        .plt = NULL,
        .idx = 0,
        .id = 0,
        .iter = PL_MAIN,
    };
    char buf[4096];
    memset(buf, '\0', sizeof(buf));
    //std::string fmt_str = args.format;
    const char* fmt = args.format.c_str();
    char* code = ddb_api->tf_compile(fmt);
    if (code == NULL) {
        resp = error_response(id, "Compilation of title format failed.");
    } else {
        ddb_api->tf_eval(&ctx, code, buf, 4096);
        ddb_api->tf_free(code);
        resp = ok_response(id);
        resp["now-playing"] = std::string(buf);
    }
    ddb_api->pl_item_unref(cur);
    return resp;
}

COMMAND(get_current_playlist, Argument)
    ddb_playlist_t* plt = ddb_api->plt_get_curr();
    if (!plt) {
        return error_response(id, "No playlist set.");
    }
    char buf[4096];
    ddb_api->plt_get_title(plt, buf, sizeof(buf));
    std::string title(buf);
    int idx = ddb_api->plt_get_curr_idx();

    json resp = ok_response(id);
    resp["title"] = title;
    resp["idx"] = idx;
    ddb_api->plt_unref(plt);
    return resp;
}

class SetCurrPlaylistArgument : Argument {
    public:
      int idx;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SetCurrPlaylistArgument, idx);
COMMAND(set_current_playlist, SetCurrPlaylistArgument)
    ddb_api->plt_set_curr_idx(args.idx);
    int curr_idx = ddb_api->plt_get_curr_idx();
    if (curr_idx == args.idx) {
        return ok_response(id);
    } else {
        return error_response(id, "Failed to set playlist");
    }
}

class GetPlaylistContentsArgument : Argument {
    public:
        int idx;
        std::string format = DDB_IPC_DEFAULT_FORMAT;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetPlaylistContentsArgument, idx, format);
COMMAND(get_playlist_contents, GetPlaylistContentsArgument)
    int iter = PL_MAIN;
    ddb_api->pl_lock();
    ddb_playlist_t* plt = ddb_api->plt_get_for_idx(args.idx);
    if (!plt) {
        ddb_api->pl_unlock();
        return error_response(id, "No playlist with given idx.");
    }

    const char* fmt = args.format.c_str();
    char* code = ddb_api->tf_compile(fmt);
    if (code == NULL) {
        return error_response(id, "Compilation of title format failed.");
    }

    json resp = ok_response(id);
    int count = ddb_api->plt_get_item_count(plt, iter);
    std::vector<std::string> items {} ;
    items.reserve(count);
    char buf[4096];
    memset(buf, '\0', sizeof(buf));

    ddb_tf_context_t ctx;
    ddb_playItem_t* prev;
    ddb_playItem_t* cur  = ddb_api->plt_get_head_item (plt, iter);
    while (cur != NULL) {
        ctx = {
            ._size = sizeof(ddb_tf_context_t),
            .flags = 0,
            .it = cur,
            .plt = NULL,
            .idx = 0,
            .id = 0,
            .iter = iter,
        };
        ddb_api->tf_eval(&ctx, code, buf, sizeof(buf));
        items.push_back(buf);
        prev = cur;
        cur = ddb_api->pl_get_next(prev, iter);
        ddb_api->pl_item_unref(prev);
    }
    ddb_api->tf_free(code);
    ddb_api->plt_unref(plt);
    ddb_api->pl_unlock();
    resp["items"] = items;
    return resp;
}

COMMAND(toggle_stop_after_current_track, Argument)
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

COMMAND(toggle_stop_after_current_album, Argument)
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

typedef std::set<std::string> accept_t;
accept_t cover_art_formats = {
    "filename",
    "blob",
};

typedef struct {
    int socket;
    request_id id;
    accept_t *accept;
} response_addr_t;

void callback_cover_art_found (int error, ddb_cover_query_t *query, ddb_cover_info_t *cover) {
    response_addr_t* addr  = (response_addr_t*) (query->user_data);
    json resp;
    DDB_IPC_DEBUG << "Entered cover art callback for descriptor " << addr->socket << std::endl;
    if (
        (query->flags & DDB_ARTWORK_FLAG_CANCELLED) ||
        cover == NULL ||
        cover->image_filename == NULL
    ) {
        resp = error_response(addr->id, "No cover art found");
    } else {
        resp = ok_response(addr->id);
        if (addr->accept->count("filename") > 0) {
            DDB_IPC_DEBUG << "Responding with filename." << std::endl;
            resp["filename"] = cover->image_filename;
        }
        if (addr->accept->count("blob") > 0) {
            DDB_IPC_DEBUG << "Responding with blob." << std::endl;
            std::ifstream cover_file(
                    cover->image_filename,
                    std::ios::binary | std::ios::ate
                    );
            std::streamsize cover_size = cover_file.tellg();
            cover_file.seekg(0, std::ios::beg);
            char* buffer = (char*) malloc(cover_size * sizeof(char));
            cover_file.read(buffer, cover_size);
            std::string cover_base64 = base64_encode(
                    (unsigned char*) buffer,
                    cover_size,
                    false
                    );
            resp["blob"] = cover_base64;
            free(buffer);
        }
    }
    send_response(resp, addr->socket);
    ddb_api->pl_item_unref(query->track);
    free(addr->accept);
    free(query->user_data);
    free(query);
}

ddb_cover_query_t* cover_query = NULL;
class RequestCoverArtArgument : Argument {
    public:
        accept_t accept = {"filename"};
        int socket;
};
void from_json(const json &j, RequestCoverArtArgument &a) {
    a.socket = j.at("socket");
    if (!j.contains("accept")) {
        return;
    }
    a.accept.clear();
    std::vector<std::string> accept_arg = j.at("accept");
    for (auto c : accept_arg ) {
        if (cover_art_formats.find(c) != cover_art_formats.end()) {
            a.accept.insert(c);
        }
    }
    if (a.accept.empty()) {
        std::string err_msg("Argument accept must include at least one of the values:");
        for(auto a = cover_art_formats.begin(); a != cover_art_formats.end(); a++){
            err_msg.append(" ");
            err_msg.append(*a);
        }
        throw std::invalid_argument(err_msg);
    }
}
COMMAND(request_cover_art, RequestCoverArtArgument)
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
        .socket = args.socket,
        .id = id,
        .accept = new accept_t(args.accept),
    };
    *(response_addr_t*) cover_query->user_data = addr;
    ddb_artwork->cover_get(cover_query, callback_cover_art_found);
    DDB_IPC_DEBUG << "Sent cover art query" << std::endl;
    return ok_response(id);
}


std::map<std::string, ipc_command> commands = {
    // playback
    {"play", command_play},
    {"pause", command_pause},
    {"play-pause", command_play_pause},
    {"prev-track", command_prev},
    {"next-track", command_next},
    {"prev-album", command_prev_album},
    {"next-album", command_next_album},
    {"stop", command_stop},
    {"set-volume", command_set_volume},
    {"adjust-volume", command_adjust_volume},
    {"toggle-mute", command_toggle_mute},
    {"seek", command_seek},
    // info
    {"get-playpos", command_get_playpos},
    {"get-now-playing", command_get_now_playing},
    {"request-cover-art", command_request_cover_art},
    {"get-current-playlist", command_get_current_playlist},
    {"set-current-playlist", command_set_current_playlist},
    {"get-playlist-contents", command_get_playlist_contents},
    // playback control
    {"toggle-stop-after-current-track", command_toggle_stop_after_current_track},
    {"toggle-stop-after-current-album", command_toggle_stop_after_current_album},
    // properties
    {"get-property", command_get_property},
    {"set-property", command_set_property},
    {"observe-property", command_observe_property}
};

json call_command(std::string command, request_id id, json args) {
    json response;
    try {
        response = commands.at(command)(id, args);
    } catch (std::out_of_range& e) {
        DDB_IPC_DEBUG << "Unknown command: " << e.what() << std::endl;
        response = error_response(id, std::string("Unknown command ") + command);
    } catch (json::out_of_range &e) {
        response = bad_request_response(id, e.what());
    } catch (json::type_error &e) {
        response = bad_request_response(id, e.what());
    } catch (json::exception &e) {
        response = bad_request_response(id, e.what());
    } catch (std::invalid_argument &e) {
        response = bad_request_response(id, e.what());
    }
    return response;
}

}
