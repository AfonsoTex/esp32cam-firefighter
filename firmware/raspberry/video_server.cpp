#include "video_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace {
const std::string PAGE = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>FireNet — Camera</title>
  <style>
    body { margin: 0; background: #13191f; color: #e9eef3;
           font: 17px system-ui, sans-serif; }
    main { max-width: 760px; margin: 40px auto; padding: 0 20px; }
    h1 { margin-bottom: 8px; }
    p { color: #b7c5d2; line-height: 1.5; }
    img { display: block; width: 100%; max-width: 640px; height: auto;
          background: #080c10; border-radius: 12px; }
    a { color: #86d6ff; }
  </style>
</head>
<body>
  <main>
    <h1>FireNet</h1>
    <p>Camera video with model detections.</p>
    <img src="/stream.mjpg" width="640" height="480"
         alt="Camera video. If it does not appear, check that FireNet is running.">
    <p>Green boxes show detections; the number indicates model confidence.</p>
    <p>If the video stops, check the program on the Raspberry Pi and <a href="/">reconnect</a>.</p>
  </main>
</body>
</html>)HTML";
}

VideoServer::~VideoServer()
{
    stop();
}

bool VideoServer::start(unsigned short port)
{
    if (running) return false;

    listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener < 0) {
        std::perror("HTTP socket");
        return false;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(listener, 8) < 0) {
        std::perror("HTTP bind/listen");
        close(listener);
        listener = -1;
        return false;
    }

    running = true;
    try {
        // Bound the number of simultaneous connections and background threads.
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back(&VideoServer::serve, this);
        }
    } catch (const std::exception& error) {
        std::cerr << "Could not start the HTTP server: " << error.what() << '\n';
        stop();
        return false;
    }
    return true;
}

void VideoServer::publish(const std::vector<unsigned char>& jpeg)
{
    if (jpeg.empty()) return;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        latestJpeg = jpeg;
        ++frameNumber;
    }
    frameReady.notify_all();
}

void VideoServer::stop()
{
    running = false;
    frameReady.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }
    workers.clear();
    if (listener >= 0) {
        close(listener);
        listener = -1;
    }
}

bool VideoServer::sendAll(int client, const void* data, std::size_t size)
{
    const char* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;
    // A slow or disconnected browser must not hold a worker indefinitely.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (running && sent < size && std::chrono::steady_clock::now() < deadline) {
        ssize_t count = send(client, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return sent == size;
}

bool VideoServer::sendText(int client, const std::string& text)
{
    return sendAll(client, text.data(), text.size());
}

void VideoServer::serve()
{
    while (running) {
        pollfd event{listener, POLLIN, 0};
        if (poll(&event, 1, 200) <= 0) continue;
        int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) continue;

        timeval timeout{1, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        try {
            handleClient(client);
        } catch (const std::exception& error) {
            std::cerr << "HTTP connection error: " << error.what() << '\n';
        }
        close(client);
    }
}

void VideoServer::handleClient(int client)
{
    std::string request;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (running && request.size() < 4096 &&
           std::chrono::steady_clock::now() < deadline &&
           request.find("\r\n\r\n") == std::string::npos) {
        char buffer[512];
        ssize_t count = recv(client, buffer, sizeof(buffer), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return;
        request.append(buffer, static_cast<std::size_t>(count));
    }
    if (request.find("\r\n\r\n") == std::string::npos) {
        sendText(client, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return;
    }

    std::istringstream firstLine(request.substr(0, request.find("\r\n")));
    std::string method, path, version;
    firstLine >> method >> path >> version;
    if (method != "GET") {
        sendText(client, "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return;
    }
    if (path == "/") {
        sendText(client, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                         "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
                         std::to_string(PAGE.size()) + "\r\n\r\n" + PAGE);
        return;
    }
    if (path != "/stream.mjpg") {
        sendText(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return;
    }

    if (!sendText(client, "HTTP/1.1 200 OK\r\n"
                          "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                          "Cache-Control: no-store\r\nConnection: close\r\n\r\n")) return;

    std::uint64_t lastFrame = 0;
    while (running) {
        std::vector<unsigned char> jpeg;
        {
            std::unique_lock<std::mutex> lock(frameMutex);
            frameReady.wait_for(lock, std::chrono::seconds(1), [&] {
                return !running || frameNumber != lastFrame;
            });
            if (!running) break;
            if (frameNumber == lastFrame) continue;
            // Keep only the newest image; slow clients cannot delay capture.
            jpeg = latestJpeg;
            lastFrame = frameNumber;
        }
        std::string header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                             std::to_string(jpeg.size()) + "\r\n\r\n";
        if (!sendText(client, header) ||
            !sendAll(client, jpeg.data(), jpeg.size()) ||
            !sendText(client, "\r\n")) break;
    }
}
