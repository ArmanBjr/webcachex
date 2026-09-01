// Build:
//   g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o cache_server src/main.cpp
//
// Run:
//   ./cache_server
//
// Demo:
//   printf "GET /index.html HTTP/1.1\r\nHost: a.local\r\nConnection: close\r\n\r\n" | nc 127.0.0.1 8080
//   printf "GET /index.html HTTP/1.1\r\nHost: b.local\r\nConnection: close\r\n\r\n" | nc 127.0.0.1 8080

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "logger.h"
#include <iomanip>
#include <ctime>
#include <memory>

namespace
{

    volatile sig_atomic_t g_stop = 0;

    void handle_sigint(int) { g_stop = 1; }

    void print_errno(const char *what)
    {
        std::cerr << what << " failed: " << std::strerror(errno) << " (errno=" << errno << ")\n";
    }

    // RAII for file descriptors
    struct FdGuard
    {
        int fd;
        explicit FdGuard(int f) : fd(f) {}
        ~FdGuard()
        {
            if (fd >= 0)
                ::close(fd);
        }
        FdGuard(const FdGuard &) = delete;
        FdGuard &operator=(const FdGuard &) = delete;
        FdGuard(FdGuard &&o) noexcept : fd(o.fd) { o.fd = -1; }
        FdGuard &operator=(FdGuard &&o) noexcept
        {
            if (this != &o)
            {
                if (fd >= 0)
                    ::close(fd);
                fd = o.fd;
                o.fd = -1;
            }
            return *this;
        }
    };

    using Clock = std::chrono::steady_clock;

    struct CacheEntry
    {
        std::string full_http_response;
        Clock::time_point expires_at;
    };

    std::unordered_map<std::string, CacheEntry> g_cache;
    std::mutex g_cache_mu;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> g_key_locks;
    std::mutex g_key_locks_mu;

    std::shared_ptr<std::mutex> get_key_mutex(const std::string &key)
    {
        std::lock_guard<std::mutex> lk(g_key_locks_mu);
        auto it = g_key_locks.find(key);
        if (it != g_key_locks.end())
            return it->second;
        auto m = std::make_shared<std::mutex>();
        g_key_locks.emplace(key, m);
        return m;
    }

    constexpr std::chrono::seconds kTTL{10};
    constexpr size_t kMaxOriginResponseBytes = 8 * 1024 * 1024; // 8MB safety cap

    struct RequestLine
    {
        std::string method;
        std::string path;
        std::string version;
    };

    std::optional<RequestLine> parse_request_line(const std::string &header_blob)
    {
        const size_t line_end = header_blob.find("\r\n");
        if (line_end == std::string::npos)
            return std::nullopt;

        std::string line = header_blob.substr(0, line_end);
        std::istringstream iss(line);

        RequestLine rl;
        if (!(iss >> rl.method >> rl.path >> rl.version))
            return std::nullopt;
        return rl;
    }

    std::optional<std::string> read_all_from_origin(int origin_fd)
    {
        std::string out;
        out.reserve(8192);

        char buf[4096];
        while (true)
        {
            ssize_t n = ::recv(origin_fd, buf, sizeof(buf), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                print_errno("recv(origin)");
                return std::nullopt;
            }
            if (n == 0)
                break; // origin closed

            if (out.size() + static_cast<size_t>(n) > kMaxOriginResponseBytes)
            {
                std::cerr << "[CACHE] Origin response too large, refusing to cache\n";
                return std::nullopt;
            }
            out.append(buf, buf + n);
        }
        return out;
    }

    bool send_all(int fd, const char *data, size_t len)
    {
        size_t sent = 0;
        while (sent < len)
        {
            ssize_t n = ::send(fd, data + sent, len - sent, 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                print_errno("send");
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    void send_simple_response(int fd, int code, const std::string &text, const std::string &body)
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << code << " " << text << "\r\n"
            << "Content-Type: text/plain\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;

        std::string resp = oss.str();
        (void)send_all(fd, resp.data(), resp.size());
    }

    constexpr size_t kMaxHeaderBytes = 8 * 1024;

    // Read client request until end of headers (\r\n\r\n)
    std::optional<std::string> read_until_header_end(int fd)
    {
        std::string data;
        data.reserve(1024);

        char buf[2048];
        while (true)
        {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                print_errno("recv");
                return std::nullopt;
            }
            if (n == 0)
            {
                return std::nullopt; // client closed
            }

            data.append(buf, buf + n);

            if (data.size() > kMaxHeaderBytes)
            {
                std::cerr << "[CACHE] Header too large\n";
                return std::nullopt;
            }

            if (data.find("\r\n\r\n") != std::string::npos)
            {
                return data;
            }
        }
    }

    // Trim helpers
    static inline void ltrim(std::string &s)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c)
                                        { return !std::isspace(c); }));
    }
    static inline void rtrim(std::string &s)
    {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c)
                             { return !std::isspace(c); })
                    .base(),
                s.end());
    }
    static inline void trim(std::string &s)
    {
        ltrim(s);
        rtrim(s);
    }

    // Parse Host header; default to a.local if missing
    std::string parse_host_or_default(const std::string &header_blob)
    {
        std::istringstream iss(header_blob);
        std::string line;

        // First line is request line; skip it
        std::getline(iss, line);

        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break; // end of headers

            // case-insensitive starts-with "Host:"
            if (line.size() >= 5)
            {
                std::string prefix = line.substr(0, 5);
                std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                               [](unsigned char c)
                               { return static_cast<char>(std::tolower(c)); });
                if (prefix == "host:")
                {
                    std::string value = line.substr(5);
                    trim(value);

                    // value may include ":port"
                    auto colon = value.find(':');
                    if (colon != std::string::npos)
                        value = value.substr(0, colon);
                    trim(value);

                    if (!value.empty())
                        return value;
                }
            }
        }

        return "a.local"; // default
    }

    struct Origin
    {
        std::string ip;
        uint16_t port;
    };

    // Create outbound connection to origin
    std::optional<int> connect_to_origin(const Origin &o)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            print_errno("socket(origin)");
            return std::nullopt;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(o.port);
        if (::inet_pton(AF_INET, o.ip.c_str(), &addr.sin_addr) != 1)
        {
            std::cerr << "[CACHE] inet_pton failed for origin ip\n";
            ::close(fd);
            return std::nullopt;
        }

        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            print_errno("connect(origin)");
            ::close(fd);
            return std::nullopt;
        }

        return fd;
    }

    // Proxy response: read all from origin and stream to client
    bool relay_origin_response(int origin_fd, int client_fd)
    {
        char buf[4096];
        while (true)
        {
            ssize_t n = ::recv(origin_fd, buf, sizeof(buf), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                print_errno("recv(origin)");
                return false;
            }
            if (n == 0)
                break; // origin closed => done

            if (!send_all(client_fd, buf, static_cast<size_t>(n)))
            {
                return false;
            }
        }
        return true;
    }
    std::string now_iso8601_utc();

    // Main handler per client connection
    void handle_client(int client_fd,
                       sockaddr_in client_addr,
                       const std::unordered_map<std::string, Origin> &routes,
                       CsvLogger &logger)
    {
        FdGuard client_guard(client_fd);

        auto t0 = Clock::now();
        std::string ts = now_iso8601_utc();

        // Fields for logging (we will fill these as we go)
        std::string host = "a.local";
        std::string path = "";
        std::string result = "ERROR";
        std::string origin_selected = "";

        // Small scope guard: ensures exactly one log line per request
        struct LogOnExit
        {
            CsvLogger &logger;
            Clock::time_point t0;
            std::string ts, host, path, result, origin_selected;
            ~LogOnExit()
            {
                auto t1 = Clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                logger.log(ts, host, path, result, origin_selected, ms);
            }
        } log_guard{logger, t0, ts, host, path, result, origin_selected};

        // --- Read request headers ---
        auto req_opt = read_until_header_end(client_fd);
        if (!req_opt)
        {
            // client closed or read error -> stays ERROR
            return;
        }
        const std::string &req = *req_opt;

        // --- Parse host/path ---
        host = parse_host_or_default(req);
        log_guard.host = host;

        auto rl_opt = parse_request_line(req);
        if (!rl_opt)
        {
            send_simple_response(client_fd, 400, "Bad Request", "Bad request line\n");
            log_guard.result = "ERROR";
            return;
        }

        const auto &rl = *rl_opt;
        path = rl.path;
        log_guard.path = path;

        if (rl.method != "GET")
        {
            send_simple_response(client_fd, 405, "Method Not Allowed", "Only GET is supported\n");
            log_guard.result = "ERROR";
            return;
        }

        // --- Route lookup ---
        auto it = routes.find(host);
        if (it == routes.end())
        {
            // Default to a.local if unknown
            auto it2 = routes.find("a.local");
            if (it2 == routes.end())
            {
                send_simple_response(client_fd, 500, "Internal Server Error", "Routing table misconfigured\n");
                log_guard.result = "ERROR";
                return;
            }
            it = it2;
        }
        const Origin &origin = it->second;

        origin_selected = origin.ip + ":" + std::to_string(origin.port);
        log_guard.origin_selected = origin_selected;

        std::string key = host + "|" + path;
        auto now = Clock::now();

        // ---- Fast path: cache lookup without per-key lock ----
        {
            std::lock_guard<std::mutex> lk(g_cache_mu);
            auto ci = g_cache.find(key);
            if (ci != g_cache.end() && ci->second.expires_at > now)
            {
                // HIT
                log_guard.result = "HIT";
                send_all(client_fd,
                         ci->second.full_http_response.data(),
                         ci->second.full_http_response.size());
                return;
            }
            else
            {
                log_guard.result = "MISS";
            }
        }

        // ---- Slow path: per-key lock to prevent thundering herd ----
        auto key_mu = get_key_mutex(key);
        std::unique_lock<std::mutex> key_lock(*key_mu);

        // Double-check after acquiring key lock (another thread might have filled cache)
        now = Clock::now();
        {
            std::lock_guard<std::mutex> lk(g_cache_mu);
            auto ci = g_cache.find(key);
            if (ci != g_cache.end() && ci->second.expires_at > now)
            {
                // Now it's HIT (filled by someone else)
                log_guard.result = "HIT";
                send_all(client_fd,
                         ci->second.full_http_response.data(),
                         ci->second.full_http_response.size());
                return;
            }
            else
            {
                log_guard.result = "MISS";
            }
        }

        // Only ONE thread per key reaches here: it will fetch from origin.

        // --- Connect to origin ---
        auto origin_fd_opt = connect_to_origin(origin);
        if (!origin_fd_opt)
        {
            send_simple_response(client_fd, 502, "Bad Gateway", "Failed to connect to origin\n");
            log_guard.result = "ERROR";
            return;
        }
        FdGuard origin_guard(*origin_fd_opt);

        // --- Forward request (as-is) ---
        if (!send_all(*origin_fd_opt, req.data(), req.size()))
        {
            send_simple_response(client_fd, 502, "Bad Gateway", "Failed to forward request to origin\n");
            log_guard.result = "ERROR";
            return;
        }

        // --- Read full origin response (so we can cache) ---
        auto resp_opt = read_all_from_origin(*origin_fd_opt);
        if (!resp_opt)
        {
            send_simple_response(client_fd, 502, "Bad Gateway", "Failed to read origin response\n");
            log_guard.result = "ERROR";
            return;
        }

        std::string full_resp = std::move(*resp_opt);

        // --- Store in cache with TTL=10s ---
        now = Clock::now();
        {
            std::lock_guard<std::mutex> lk(g_cache_mu);
            g_cache[key] = CacheEntry{full_resp, now + kTTL};
        }

        // --- Send to client ---
        send_all(client_fd, full_resp.data(), full_resp.size());

        // key_lock unlocks automatically here
        return;
    }

    // Listener helpers
    bool set_reuse_options(int fd)
    {
        int yes = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        {
            print_errno("setsockopt(SO_REUSEADDR)");
            return false;
        }
        return true;
    }

    std::optional<int> create_listen_socket(uint16_t port)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            print_errno("socket(listen)");
            return std::nullopt;
        }

        if (!set_reuse_options(fd))
        {
            ::close(fd);
            return std::nullopt;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1)
        {
            std::cerr << "inet_pton failed for 127.0.0.1\n";
            ::close(fd);
            return std::nullopt;
        }

        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            print_errno("bind");
            ::close(fd);
            return std::nullopt;
        }

        constexpr int kBacklog = 128;
        if (::listen(fd, kBacklog) < 0)
        {
            print_errno("listen");
            ::close(fd);
            return std::nullopt;
        }

        return fd;
    }

    void accept_loop(int listen_fd,
                     const std::unordered_map<std::string, Origin> &routes,
                     CsvLogger &logger)
    {
        while (!g_stop)
        {
            sockaddr_in client{};
            socklen_t clen = sizeof(client);

            int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr *>(&client), &clen);
            if (client_fd < 0)
            {
                if (errno == EINTR)
                    continue;
                print_errno("accept");
                continue;
            }

            // thread-per-connection
            std::thread(handle_client, client_fd, client, std::cref(routes), std::ref(logger)).detach();
        }
    }

    std::string now_iso8601_utc()
    {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        gmtime_r(&t, &tm); // Linux/WSL

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    static inline void split_key(const std::string &key, std::string &host, std::string &path)
    {
        auto pos = key.find('|');
        if (pos == std::string::npos)
        {
            host = "";
            path = key;
            return;
        }
        host = key.substr(0, pos);
        path = key.substr(pos + 1);
    }

    std::thread g_cleaner_thread;

    void cache_cleaner_loop(CsvLogger &logger)
    {
        using namespace std::chrono_literals;

        while (!g_stop)
        {
            std::this_thread::sleep_for(1s);

            std::vector<std::string> expired_keys;
            auto now = Clock::now();

            {
                std::lock_guard<std::mutex> lk(g_cache_mu);
                for (auto it = g_cache.begin(); it != g_cache.end();)
                {
                    if (it->second.expires_at <= now)
                    {
                        expired_keys.push_back(it->first);
                        it = g_cache.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            for (const auto &key : expired_keys)
            {
                std::string host, path;
                split_key(key, host, path);

                logger.log(
                    now_iso8601_utc(),
                    host,
                    path,
                    "EXPIRE",
                    "",
                    0);
            }
        }
    }
} // namespace

int main()
{
    // SIGINT handler
    struct sigaction sa{};
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    // Routing table
    std::unordered_map<std::string, Origin> routes{
        {"a.local", {"127.0.0.1", 8081}},
        {"b.local", {"127.0.0.1", 8082}},
    };

    // Logger (CSV)
    CsvLogger logger("cache_log.csv");

    g_cleaner_thread = std::thread(cache_cleaner_loop, std::ref(logger));
    auto listen_fd_opt = create_listen_socket(8080);
    if (!listen_fd_opt)
        return 1;

    FdGuard listen_guard(*listen_fd_opt);

    std::cerr << "[CACHE] Listening on 127.0.0.1:8080\n";
    std::cerr << "[CACHE] Routes: a.local->8081, b.local->8082\n";
    std::cerr << "[CACHE] Logging to: cache_log.csv\n";
    std::cerr << "[CACHE] Ctrl+C to stop.\n";

    accept_loop(*listen_fd_opt, routes, logger);

    g_stop = 1; // just in case
    if (g_cleaner_thread.joinable())
        g_cleaner_thread.join();

    std::cerr << "[CACHE] Shutting down...\n";
    return 0;
}
