#pragma once

#include <libwebsockets.h>
#include <string>

// ---------------------------------------------------------------------------
// Compatibility shims — same values as libmicrohttpd so the handler .cpp
// files that use these types and constants compile unchanged after we drop
// the library.
// ---------------------------------------------------------------------------
typedef int MHD_Result;
#define MHD_YES 1
#define MHD_NO  0

// Standard HTTP status codes (subset used by the handler files)
#define MHD_HTTP_OK                    200
#define MHD_HTTP_BAD_REQUEST           400
#define MHD_HTTP_NOT_FOUND             404
#define MHD_HTTP_INTERNAL_SERVER_ERROR 500

// Per-request context passed through the routing and handler layer.  The lws
// http_callback in ws_server.cpp creates one per request, calls route(), and
// then reads back the stored response to write via lws.  Using a struct with
// the legacy name lets all handler .cpp files compile without any changes.
struct MHD_Connection {
    struct lws* wsi          = nullptr;
    int         status       = 200;
    std::string content_type;   // set by send_json / serve_file
    std::string body;           // response body
};

// Store a JSON response into conn; http_callback() will send it via lws.
// Signature identical to the old libmicrohttpd version so handler .cpp files
// require zero changes.
MHD_Result send_json(struct MHD_Connection* conn, int status_code,
                     const std::string& body);

// Dispatch a request: match method+path, invoke the handler, fill
// conn->status / conn->content_type / conn->body.  Called from http_callback
// in ws_server.cpp after the full request body has been accumulated.
MHD_Result route(struct MHD_Connection* conn, const std::string& method,
                 const std::string& path, const std::string& request_body);
