#pragma once

#include <string>

// ---------------------------------------------------------------------
// Minimal libmicrohttpd-API-compatible shim.
//
// This project originally used libmicrohttpd for REST and libwebsockets
// for WebSocket on two separate ports, because MHD can't hand off a
// connection for a protocol upgrade the way Go's net/http could (Go's
// gorilla/websocket hijacks the same listening socket net/http already
// accepted on). To get genuine single-port REST+WS — matching the
// original Go architecture, and required for the frontend's WebSocket(...)
// call using the page's own origin/port — everything now runs through
// libwebsockets alone. See ws_server.cpp, which handles both HTTP and WS
// callback reasons on one lws_context/one port.
//
// Every handler function in handlers_*.cpp was written against MHD's
// naming (MHD_Connection*, MHD_HTTP_OK, send_json returning MHD_Result)
// specifically so that swapping the transport wouldn't require touching
// ~30 handler functions — only this shim and the server plumbing change.
// ---------------------------------------------------------------------

struct MHD_Connection {
    int response_status = 200;
    std::string response_content_type = "application/json";
    std::string response_body;
    // Extra header name/value beyond Content-Type + CORS — e.g. the PDF
    // report placeholder's Content-Disposition. Only ever need 0 or 1 of
    // these in practice, so a single pair is enough.
    std::string extra_header_name;
    std::string extra_header_value;
};

typedef int MHD_Result;
constexpr int MHD_YES = 1;
constexpr int MHD_NO = 0;

constexpr int MHD_HTTP_OK = 200;
constexpr int MHD_HTTP_BAD_REQUEST = 400;
constexpr int MHD_HTTP_NOT_FOUND = 404;
constexpr int MHD_HTTP_INTERNAL_SERVER_ERROR = 500;

// Sets the JSON response on the connection context. The actual network
// write happens later, when libwebsockets signals the socket is writable
// (see LWS_CALLBACK_HTTP_WRITEABLE in ws_server.cpp).
MHD_Result send_json(struct MHD_Connection* connection, int status_code, const std::string& body);

// Like send_json but with a custom content type and (optionally) one extra
// response header — used by the PDF report placeholder's
// Content-Disposition header.
MHD_Result send_raw(struct MHD_Connection* connection, int status_code,
                     const std::string& content_type, const std::string& body,
                     const std::string& extra_header_name = "",
                     const std::string& extra_header_value = "");

// Dispatches a fully-received request (method + path + body) to the right
// handler. Called from ws_server.cpp once libwebsockets has delivered the
// complete request. This was previously a file-local `static` function in
// http_server.cpp; it's exported now because the server loop that drives
// it lives in a different translation unit (ws_server.cpp).
MHD_Result route(struct MHD_Connection* connection, const std::string& method,
                  const std::string& path, const std::string& body);
