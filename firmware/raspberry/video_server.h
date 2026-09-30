#ifndef VIDEO_SERVER_H
#define VIDEO_SERVER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// A small Linux HTTP server. Camera capture stays in the main thread.
class VideoServer {
public:
    ~VideoServer();
    bool start(unsigned short port);
    void publish(const std::vector<unsigned char>& jpeg);
    void stop();

private:
    void serve();
    void handleClient(int client);
    bool sendAll(int client, const void* data, std::size_t size);
    bool sendText(int client, const std::string& text);

    int listener = -1;
    std::atomic<bool> running{false};
    std::vector<std::thread> workers;
    std::mutex frameMutex;
    std::condition_variable frameReady;
    std::vector<unsigned char> latestJpeg;
    std::uint64_t frameNumber = 0;
};

#endif
