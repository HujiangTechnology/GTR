//
//  gtr_core.c
//  GTR
//
//  Created L1MeN9Yu on 2018/9/1.
//  Copyright © 2018 limengyu.top. All rights reserved.
//

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <zconf.h>
#include <assert.h>
#include <curl/curl.h>
#include "gtr_core.h"
#include "gtr_race.h"
#include "gtr_thread_pool.h"
#include "gtr_atomic.h"
#include "gtr_header.h"
#include "gtr_proxy.h"
#include "gtr_log.h"
#include "gtr_file_utilty.h"
#include "gtr_task_id.h"
#include "gtr_method.h"

/**
 * 请求的线程池
 */
static thread_pool gtr_core_thread_pool;

static const char *global_user_agent;

static const char *temp_directory;

static gtr_core_proxy *global_proxy;

static void gtr_core_config_http_method(CURL *handle, gtr_core_race *request);

static void gtr_core_config_url(CURL *handle, const char *url);

static void gtr_core_config_accept_encoding(CURL *handle);

static void gtr_core_config_keep_alive(CURL *handle);

static void gtr_core_config_headers(CURL *handle, const char *user_agent, struct curl_slist *header);

static void gtr_core_config_verify_peer(CURL *handle, bool on);

static void gtr_core_config_signal(CURL *handle, bool is_on);

static void gtr_core_config_time_out(CURL *handle, unsigned int time_out);

static void gtr_core_config_header_call_back(CURL *handle, gtr_core_race_response_header *response_header);

static void gtr_core_config_write_call_back(CURL *handle, gtr_core_race_response_body *response_body);

static void gtr_core_config_progress(CURL *handle, gtr_core_race *request);

static void gtr_core_config_proxy(CURL *handle);

static void gtr_core_config_speed_limit(CURL *handle, long limit);

static void gtr_core_config_debug(CURL *handle);

//--- Private Utility
static void gtr_core_create_temp_dir(void);

//--- Call Back
static int debug_func(CURL *__unused handle, curl_infotype type, char *data, size_t size, void *__unused user_p) {
    switch (type) {
        case CURLINFO_TEXT:
            gtr_core_log(gtr_log_flag_trace, "Info: %.*s", size, data);
            break;
        case CURLINFO_HEADER_IN:
            gtr_core_log(gtr_log_flag_trace, "Rx header: %.*s", size, data);
            break;
        case CURLINFO_HEADER_OUT:
            gtr_core_log(gtr_log_flag_trace, "Tx header: %.*s", size, data);
            break;
        case CURLINFO_DATA_IN:
            gtr_core_log(gtr_log_flag_trace, "Rx data: %.*s", size, data);
            break;
        case CURLINFO_DATA_OUT:
            gtr_core_log(gtr_log_flag_trace, "Tx data: %.*s", size, data);
            break;
        case CURLINFO_SSL_DATA_IN:
            gtr_core_log(gtr_log_flag_trace, "Rx SSL data: %.*s", size, data);
            break;
        case CURLINFO_SSL_DATA_OUT:
            gtr_core_log(gtr_log_flag_trace, "Tx SSL data: %.*s", size, data);
            break;
        case CURLINFO_END:
            break;
    }

    return 0;
}

/**
 * 读取回调(put|post)
 * @param ptr ptr
 * @param size size
 * @param nmemb nmemb
 * @param userp userp
 * @return size_t
 */
static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userp) {
    gtr_core_race_request_body *request_data = (gtr_core_race_request_body *) userp;
    unsigned long bytes_read = 0;
    if (size * nmemb < 1)
        return (size_t) bytes_read;

    if (size * nmemb > request_data->size_left) {
        //This chunk can handle the whole transaction
        bytes_read = request_data->size_left;
        memcpy(ptr, request_data->data + (request_data->size - request_data->size_left), bytes_read);
        request_data->size_left = 0;
    } else {
        //This chunk can only handle part of the transaction
        bytes_read = (size * nmemb);
        memcpy(ptr, request_data->data + (request_data->size - request_data->size_left), bytes_read);
        request_data->size_left = (request_data->size_left - bytes_read);
    }
    return (size_t) bytes_read;
}

static size_t header_callback(char *contents, size_t size, size_t nmemb, void *user_data) {
    size_t real_size = size * nmemb;
    gtr_core_race_response_header *response_header = (gtr_core_race_response_header *) user_data;

    response_header->data = realloc(response_header->data, response_header->size + real_size + 1);
    if (response_header->data == NULL) {
        /* out of memory! */
        gtr_core_log(gtr_log_flag_error, "not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(response_header->data[response_header->size]), contents, real_size);
    response_header->size += real_size;
    response_header->data[response_header->size] = 0;

    return real_size;
}

/**
 * 写入回调
 * @param contents 内容
 * @param size 内容大小
 * @param nmemb nmemb
 * @param user_data user_data
 * @return size_t
 */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *user_data) {
    size_t real_size = size * nmemb;
    gtr_core_race_response_body *response_body = (gtr_core_race_response_body *) user_data;

    response_body->data = realloc(response_body->data, response_body->size + real_size + 1);
    if (response_body->data == NULL) {
        /* out of memory! */
        gtr_core_log(gtr_log_flag_error, "not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(response_body->data[response_body->size]), contents, real_size);
    response_body->size += real_size;
    response_body->data[response_body->size] = 0;

    return real_size;
}

static int progress_callback(
        void *__unused p,
        curl_off_t download_total,
        curl_off_t download_now,
        curl_off_t upload_total,
        curl_off_t upload_now
) {
    gtr_core_log(
            gtr_log_flag_debug,
            "UP: %" CURL_FORMAT_CURL_OFF_T " of %" CURL_FORMAT_CURL_OFF_T
            "DOWN: %" CURL_FORMAT_CURL_OFF_T " of %" CURL_FORMAT_CURL_OFF_T
            "\r\n",
            upload_now, upload_total, download_now, download_total);
    return 0;
}

//--- Core
static void request(gtr_core_race *core_race) {
    CURL *curl_handle;
    CURLcode res;

    gtr_core_race_response_header response_header;
    {
        response_header.data = malloc(1);
        response_header.size = 0;
    }

    gtr_core_race_response_body response_body;
    {
        response_body.data = malloc(1);
        response_body.size = 0;
    }

    curl_handle = curl_easy_init();

    {
        gtr_core_config_http_method(curl_handle, core_race);
    }

    {
        gtr_core_config_url(curl_handle, core_race->url);
    }

    {
        gtr_core_config_accept_encoding(curl_handle);
    }

    {
        gtr_core_config_keep_alive(curl_handle);
    }

    {
        gtr_core_config_verify_peer(curl_handle, true);
    }

    {
        gtr_core_config_time_out(curl_handle, core_race->time_out);
    }

    {
        //如果不使用threaded resolver或者c-ares,需要no_signal=1,否则在dns解析超时情况下crash
        //详见:https://curl.haxx.se/libcurl/c/CURLOPT_NOSIGNAL.html https://curl.haxx.se/libcurl/c/threadsafe.html
        gtr_core_config_signal(curl_handle, true);
    }

    {
        gtr_core_config_header_call_back(curl_handle, &response_header);
    }

    {
        gtr_core_config_write_call_back(curl_handle, &response_body);
    }

    {
        gtr_core_config_progress(curl_handle, core_race);
    }

    {
        gtr_core_config_proxy(curl_handle);
    }

    {
        gtr_core_config_speed_limit(curl_handle, core_race->speed_limit);
    }

    {
        gtr_core_config_debug(curl_handle);
    }

    struct curl_slist *header = gtr_core_add_custom_headers(core_race->header);
    gtr_core_config_headers(curl_handle, global_user_agent, header);

    {
        res = curl_easy_perform(curl_handle);
    }

    {
        long http_response_code = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_response_code);

        long condition_unmet = 0;
        curl_easy_getinfo(curl_handle, CURLINFO_CONDITION_UNMET, &condition_unmet);

        /* check for errors */
        if (res != CURLE_OK) {
            gtr_core_log(gtr_log_flag_error, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            if (core_race->on_failed) {
                core_race->on_failed(core_race->task_id, http_response_code, res, curl_easy_strerror(res));
            }
        } else {
            gtr_core_log(gtr_log_flag_trace, "%lu bytes retrieved\n", response_body.size);
            if (core_race->on_succeed) {
                core_race->on_succeed(core_race->task_id, http_response_code, response_header.data, response_header.size, response_body.data, response_body.size);
            }
        }
    }

    {
        if (header) {curl_slist_free_all(header);}
    }

    {
        curl_easy_cleanup(curl_handle);
        free(core_race->url);
        free(core_race->header);
        free(core_race->method);
        free(core_race);
        free(response_header.data);
        free(response_body.data);
    }
}

//--- Public
void gtr_core_init(const char *user_agent, void *log_callback, unsigned int cylinder_count) {
    const char *ua = user_agent ?: "top.limengyu.GTR";
    global_user_agent = strdup(ua);
    config_log_callback(log_callback);
    curl_global_init(CURL_GLOBAL_ALL);
    gtr_core_thread_pool = thread_pool_init(cylinder_count);
    thread_pool_wait(gtr_core_thread_pool);
    gtr_core_create_temp_dir();
    gtr_core_log(
            gtr_log_flag_trace,
            "GTR start succeed !!!\n"
            "global_user_agent = %s\n"
            "curl version : %s",
            global_user_agent,
            curl_version()
    );
}

void __unused gtr_core_dispose(void) {
    curl_global_cleanup();
    thread_pool_destroy(gtr_core_thread_pool);
}

void gtr_core_open_proxy(const char *url, unsigned int port) {
    assert(url);
    assert(port > 0);
    global_proxy = (gtr_core_proxy *) calloc(1, sizeof(gtr_core_proxy));
    size_t url_size = strlen(url) + 1;
    global_proxy->url = malloc(url_size);
    memcpy(global_proxy->url, url, url_size);
    global_proxy->port = port;
}

void gtr_core_close_proxy(void) {
    if (global_proxy) {
        if (global_proxy->url) {
            free(global_proxy->url);
        }
        global_proxy = NULL;
    }
}

void gtr_core_race_start(gtr_core_race *race) {
    thread_pool_add_work(gtr_core_thread_pool, (void (*)(void *)) request, race);
}

//---Private Config
static void gtr_core_config_http_method(CURL *handle, gtr_core_race *request) {
    assert(request);
    if (strcmp(request->method, METHOD_GET) == 0) {
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(request->method, METHOD_POST) == 0) {
        if (request->request_data && request->request_data->data) {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request->request_data->data);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, request->request_data->size);
            curl_easy_setopt(handle, CURLOPT_INFILESIZE, request->request_data->size);
        } else {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, 0);
        }
    } else if (strcmp(request->method, METHOD_DOWNLOAD) == 0) {
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(request->method, METHOD_UPLOAD) == 0) {
        curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
    } else {
        if (request->method) {curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, request->method);}
        if (request->request_data && request->request_data->data) {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request->request_data->data);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, request->request_data->size);
            curl_easy_setopt(handle, CURLOPT_INFILESIZE, request->request_data->size);
        } else {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, 0);
        }
    }
}

static void gtr_core_config_url(CURL *handle, const char *url) {
    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
}

static void gtr_core_config_accept_encoding(CURL *handle) {
    //设置CURLOPT_ACCEPT_ENCODING
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
}

static void gtr_core_config_keep_alive(CURL *handle) {
    /* enable TCP keep-alive for this transfer */
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
    /* keep-alive idle time to 120 seconds */
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPIDLE, 120L);
    /* interval time between keep-alive probes: 60 seconds */
    curl_easy_setopt(handle, CURLOPT_TCP_KEEPINTVL, 60L);
}

static void gtr_core_config_headers(CURL *handle, const char *user_agent, struct curl_slist *header) {
    curl_easy_setopt(handle, CURLOPT_USERAGENT, user_agent);
    if (header) {
        CURLcode res = curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
    }
}

static void gtr_core_config_verify_peer(CURL *handle, bool on) {
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, on ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, on ? 1L : 0L);
}

//TODO ETag/If_Modified_since
static void gtr_core_config_time_condition(CURL *handle, unsigned long time) {
    /* January 1, 2020 is 1577833200 */
    curl_easy_setopt(handle, CURLOPT_TIMEVALUE, time);

    /* If-Modified-Since the above time stamp */
    curl_easy_setopt(handle, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
}

static void gtr_core_config_progress(CURL *handle, gtr_core_race *request) {
    assert(request);
    //上传和下载才需要进度回掉方法
    if (!request->method) {return;}
    if (strcmp(request->method, METHOD_DOWNLOAD) == 0) {
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &request->download_data);
    } else if (strcmp(request->method, METHOD_UPLOAD) == 0) {
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, NULL);
    }
}

static void gtr_core_config_time_out(CURL *handle, unsigned int time_out) {
    long t_o = time_out > 0 ? time_out : 10L;
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, t_o);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, t_o);
}

static void gtr_core_config_signal(CURL *handle, bool is_on) {
    long value = is_on ? 1L : 0L;
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, value);
}

static void gtr_core_config_proxy(CURL *handle) {
    if (global_proxy) {
        if (strlen(global_proxy->url) > 0 && global_proxy->port > 0) {
            curl_easy_setopt(handle, CURLOPT_PROXY, global_proxy->url);
            curl_easy_setopt(handle, CURLOPT_PROXYPORT, global_proxy->port);
        }
    }
}

static void gtr_core_config_speed_limit(CURL *handle, long limit) {
    curl_off_t _limit = limit >= 0 ?: 0;
    curl_easy_setopt(handle, CURLOPT_MAX_RECV_SPEED_LARGE, _limit);
}

static void gtr_core_config_header_call_back(CURL *handle, gtr_core_race_response_header *response_header) {
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, response_header);
}

static void gtr_core_config_write_call_back(CURL *handle, gtr_core_race_response_body *response_body) {
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, response_body);
}

static void gtr_core_config_debug(CURL *handle) {
    curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, &debug_func);
    curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
}

//---Private Utility
static void gtr_core_create_temp_dir(void) {
    temp_directory = get_app_temp_directory();
    gtr_create_directory_if_not_exist(temp_directory);
};