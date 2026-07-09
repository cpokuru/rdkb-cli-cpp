#pragma once

#include <microhttpd.h>
#include <string>

// Thin wrapper around libmicrohttpd. Routes are dispatched by (method, path)
// match in http_server.cpp — mirrors gorilla/mux's api.HandleFunc(...) calls
// in the original main.go, just without a routing dependency.
class HttpServer {
public:
    explicit HttpServer(uint16_t port);
    ~HttpServer();

    bool start();
    void stop();

private:
    uint16_t port_;
    struct MHD_Daemon* daemon_ = nullptr;

    static MHD_Result dispatch(void* cls, struct MHD_Connection* connection,
                                const char* url, const char* method,
                                const char* version, const char* upload_data,
                                size_t* upload_data_size, void** con_cls);

    static void request_completed(void* cls, struct MHD_Connection* connection,
                                   void** con_cls, enum MHD_RequestTerminationCode toe);
};

// Helper to send a JSON response with the given HTTP status code.
MHD_Result send_json(struct MHD_Connection* connection, int status_code, const std::string& body);
