// Build:
//   g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o web_server src/main.cpp
//
// Run:
//   ./web_server --port 8081 --root ../web_content_a
//   ./web_server --port 8082 --root ../web_content_b

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sstream>
#include <fstream>
#include <mutex>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <system_error>

#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <algorithm>

namespace
{

    volatile sig_atomic_t g_stop = 0;

    void handle_sigint(int) { g_stop = 1; }

    std::mutex g_log_mu;

    std::string now_iso8601_utc()
    {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        gmtime_r(&t, &tm);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    std::string csv_escape(const std::string &s)
    {
        bool need_quotes = false;
        for (char c : s)
        {
            if (c == ',' || c == '"' || c == '\n' || c == '\r')
            {
                need_quotes = true;
                break;
            }
        }
        if (!need_quotes)
            return s;

        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('"');
        for (char c : s)
        {
            if (c == '"')
                out.push_back('"');
            out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

    void ensure_origin_log_header(const std::string &log_path)
    {
        std::ifstream in(log_path, std::ios::binary);
        bool need_header = true;
        if (in.good())
        {
            in.seekg(0, std::ios::end);
            need_header = (in.tellg() <= 0);
        }
        in.close();

        if (need_header)
        {
            std::ofstream out(log_path, std::ios::app);
            out << "timestamp,server_port,client_ip,client_port,method,path,status_code,bytes\n";
        }
    }

    void origin_log_line(const std::string &log_path,
                         uint16_t server_port,
                         const std::string &client_ip,
                         uint16_t client_port,
                         const std::string &method,
                         const std::string &path,
                         int status_code,
                         size_t bytes)
    {
        std::lock_guard<std::mutex> lk(g_log_mu);
        ensure_origin_log_header(log_path);

        std::ofstream out(log_path, std::ios::app);
        out
            << csv_escape(now_iso8601_utc()) << ","
            << server_port << ","
            << csv_escape(client_ip) << ","
            << client_port << ","
            << csv_escape(method) << ","
            << csv_escape(path) << ","
            << status_code << ","
            << bytes
            << "\n";
    }

    void print_errno(const char *what)
    {
        std::cerr << what << " failed: " << std::strerror(errno)
                  << " (errno=" << errno << ")\n";
    }

    constexpr size_t kMaxHeaderBytes = 8 * 1024; // 8KB

    // Reads from socket until "\r\n\r\n" is found or max header size exceeded.
    // Returns the full header string (may include extra bytes after header end).
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
                // client closed connection
                return std::nullopt;
            }

            data.append(buf, buf + n);

            if (data.size() > kMaxHeaderBytes)
            {
                std::cerr << "[PARSE] Header too large (> " << kMaxHeaderBytes << " bytes)\n";
                return std::nullopt;
            }

            if (data.find("\r\n\r\n") != std::string::npos)
            {
                return data;
            }
        }
    }

    struct RequestLine
    {
        std::string method;
        std::string path;
        std::string version;
    };

    // Extracts the request line (first line) and parses it into 3 tokens.
    std::optional<RequestLine> parse_request_line(const std::string &header_blob)
    {
        const size_t line_end = header_blob.find("\r\n");
        if (line_end == std::string::npos)
        {
            std::cerr << "[PARSE] No CRLF found in request\n";
            return std::nullopt;
        }

        std::string line = header_blob.substr(0, line_end);

        std::istringstream iss(line);
        RequestLine rl;
        if (!(iss >> rl.method >> rl.path >> rl.version))
        {
            std::cerr << "[PARSE] Bad request line: " << line << "\n";
            return std::nullopt;
        }
        return rl;
    }

    std::optional<std::string> normalize_path(const std::string &raw_path)
    {
        // Must start with '/'
        if (raw_path.empty() || raw_path[0] != '/')
        {
            return std::nullopt;
        }

        // Default document
        if (raw_path == "/")
        {
            return std::string("/index.html");
        }

        // Very simple traversal protection
        // Reject anything containing ".."
        if (raw_path.find("..") != std::string::npos)
        {
            return std::nullopt;
        }

        return raw_path;
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

    void send_simple_response(int fd, int status_code, const std::string &status_text,
                              const std::string &body,
                              const std::string &content_type = "text/plain")
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
            << "Content-Type: " << content_type << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;

        std::string resp = oss.str();
        (void)send_all(fd, resp.data(), resp.size());
    }

    // RAII guard to ensure fd is always closed.
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
        FdGuard(FdGuard &&other) noexcept : fd(other.fd) { other.fd = -1; }
        FdGuard &operator=(FdGuard &&other) noexcept
        {
            if (this != &other)
            {
                if (fd >= 0)
                    ::close(fd);
                fd = other.fd;
                other.fd = -1;
            }
            return *this;
        }
    };

    bool set_reuse_options(int fd)
    {
        int yes = 1;

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        {
            print_errno("setsockopt(SO_REUSEADDR)");
            return false;
        }

#ifdef SO_REUSEPORT
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) < 0)
        {
            // Not fatal; warn only.
            print_errno("setsockopt(SO_REUSEPORT)");
        }
#endif

        return true;
    }

    std::optional<int> create_listen_socket(uint16_t port)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            print_errno("socket");
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

    std::string guess_content_type(const std::string &path)
    {
        auto dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return "application/octet-stream";

        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });

        if (ext == "html" || ext == "htm")
            return "text/html";
        if (ext == "txt")
            return "text/plain";
        if (ext == "css")
            return "text/css";
        if (ext == "js")
            return "application/javascript";
        if (ext == "png")
            return "image/png";
        if (ext == "jpg" || ext == "jpeg")
            return "image/jpeg";

        return "application/octet-stream";
    }

    std::optional<std::string> read_file_bytes(const std::string &full_path)
    {
        std::ifstream in(full_path, std::ios::binary);
        if (!in)
            return std::nullopt;

        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    void send_200_file(int fd, const std::string &content_type, const std::string &body)
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: " << content_type << "\r\n"
            << "Cache-Control: max-age=10\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n";

        std::string hdr = oss.str();
        send_all(fd, hdr.data(), hdr.size());
        send_all(fd, body.data(), body.size());
    }

    void send_404(int fd)
    {
        const std::string body = "Not Found\n";
        send_simple_response(fd, 404, "Not Found", body, "text/plain");
    }

    static std::string list_files_recursive_lines(const std::string &root_dir)
    {
        namespace fs = std::filesystem;

        fs::path root = fs::path(root_dir);
        std::error_code ec;

        // canonical may fail if path doesn't exist; use weakly_canonical
        fs::path canon_root = fs::weakly_canonical(root, ec);
        if (ec)
        {
            // fallback: use as-is
            canon_root = root;
            ec.clear();
        }

        std::string out;

        // Skip permission issues instead of throwing
        fs::directory_options opts = fs::directory_options::skip_permission_denied;

        for (fs::recursive_directory_iterator it(root, opts, ec), end; it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            const fs::directory_entry &de = *it;

            // Skip symlinks to avoid escaping root via links
            std::error_code ec2;
            if (de.is_symlink(ec2))
            {
                ec2.clear();
                continue;
            }

            if (!de.is_regular_file(ec2))
            {
                ec2.clear();
                continue;
            }

            fs::path p = de.path();

            // Make relative to root
            fs::path rel = fs::relative(p, root, ec2);
            if (ec2)
            {
                ec2.clear();
                continue;
            }

            // Normalize to forward slashes and prefix '/'
            std::string rels = rel.generic_string();
            if (rels.empty())
                continue;
            if (rels[0] != '/')
                rels = "/" + rels;

            out += rels;
            out += "\n";
        }

        // Optional: sort output (nice for UI) — simple approach: split/sort/join
        // For simplicity keep as-is. If you want sorted, tell me.

        return out;
    }

    void handle_client(int client_fd,
                       sockaddr_in client_addr,
                       std::string root_dir,
                       uint16_t server_port,
                       std::string log_path)
    {
        FdGuard guard(client_fd);

        char ipbuf[INET_ADDRSTRLEN]{};
        const char *ip_c = ::inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
        std::string client_ip = (ip_c ? ip_c : "unknown");
        uint16_t client_port = ntohs(client_addr.sin_port);

        // Log fields
        std::string method = "UNKNOWN";
        std::string path = "";
        int status_code = 0;
        size_t bytes = 0;

        // Ensure exactly one log line per connection
        struct LogOnExit
        {
            std::string log_path;
            uint16_t server_port;
            std::string client_ip;
            uint16_t client_port;
            std::string &method;
            std::string &path;
            int &status_code;
            size_t &bytes;
            ~LogOnExit()
            {
                origin_log_line(log_path, server_port, client_ip, client_port,
                                method, path, status_code, bytes);
            }
        } log_guard{log_path, server_port, client_ip, client_port, method, path, status_code, bytes};

        std::cerr << "[WORKER " << std::this_thread::get_id() << "] Connected "
                  << client_ip << ":" << client_port << "\n";

        auto headers_opt = read_until_header_end(client_fd);
        if (!headers_opt)
        {
            // client closed early -> status_code stays 0
            return;
        }

        auto req_opt = parse_request_line(*headers_opt);
        if (!req_opt)
        {
            method = "BAD";
            path = "";
            status_code = 400;
            send_simple_response(client_fd, 400, "Bad Request", "Bad Request\n");
            return;
        }

        const auto &req = *req_opt;
        method = req.method;
        path = req.path;

        std::cerr << "[PARSE] method=" << req.method
                  << " path=" << req.path
                  << " version=" << req.version << "\n";

        if (req.method != "GET")
        {
            status_code = 405;
            send_simple_response(client_fd, 405, "Method Not Allowed", "Only GET is supported\n");
            return;
        }

        // ---- Special endpoint: recursive listing ----
        if (req.path == "/__list")
        {
            try
            {
                std::string body = list_files_recursive_lines(root_dir);
                status_code = 200;
                bytes = body.size();
                send_200_file(client_fd, "text/plain", body);
                return;
            }
            catch (...)
            {
                status_code = 500;
                bytes = 0;
                send_simple_response(client_fd, 500, "Internal Server Error", "Failed to list files\n");
                return;
            }
        }

        // ---- Normal file serving ----
        auto safe_path_opt = normalize_path(req.path);
        if (!safe_path_opt)
        {
            status_code = 400;
            send_simple_response(client_fd, 400, "Bad Request", "Invalid path\n");
            return;
        }
        const std::string &safe_path = *safe_path_opt;

        std::string full_path = root_dir + safe_path;
        std::cerr << "[PATH] full=" << full_path << "\n";

        auto body_opt = read_file_bytes(full_path);
        if (!body_opt)
        {
            status_code = 404;
            bytes = 0;
            send_404(client_fd);
            return;
        }

        const std::string &body = *body_opt;
        std::string ctype = guess_content_type(safe_path);

        status_code = 200;
        bytes = body.size();
        send_200_file(client_fd, ctype, body);

        std::cerr << "[WORKER " << std::this_thread::get_id()
                  << "] 200 OK (" << body.size() << " bytes)\n";
    }

    void accept_loop(int listen_fd,
                     const std::string &root_dir,
                     uint16_t server_port,
                     const std::string &log_path)
    {
        while (!g_stop)
        {
            sockaddr_in client{};
            socklen_t client_len = sizeof(client);

            int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr *>(&client), &client_len);
            if (client_fd < 0)
            {
                if (errno == EINTR)
                    continue;
                print_errno("accept");
                continue;
            }

            std::cerr << "[LISTENER] Accepted fd=" << client_fd
                      << " -> dispatching to worker thread\n";

            // Thread-per-connection
            std::thread(handle_client, client_fd, client, root_dir, server_port, log_path).detach();
        }
    }

    struct Args
    {
        uint16_t port = 0;
        std::string root;
        std::string log = "origin_log.csv";
    };

    std::optional<Args> parse_args(int argc, char **argv)
    {
        Args a;

        for (int i = 1; i < argc; ++i)
        {
            std::string key = argv[i];
            if (key == "--port" && i + 1 < argc)
            {
                int p = std::stoi(argv[++i]);
                if (p <= 0 || p > 65535)
                {
                    std::cerr << "Invalid --port: " << p << "\n";
                    return std::nullopt;
                }
                a.port = static_cast<uint16_t>(p);
            }
            else if (key == "--root" && i + 1 < argc)
            {
                a.root = argv[++i];
            }
            else if (key == "--help")
            {
                std::cout << "Usage: " << argv[0]
                          << " --port <8081|8082> --root <content_dir> [--log <log.csv>]\n";
                return std::nullopt;
            }
            else if (key == "--log" && i + 1 < argc)
            {
                a.log = argv[++i];
            }
            else
            {
                std::cerr << "Unknown/invalid arg: " << key << "\n";
                return std::nullopt;
            }
        }

        if (a.port == 0)
        {
            std::cerr << "Missing required --port\n";
            return std::nullopt;
        }
        if (a.root.empty())
        {
            std::cerr << "Missing required --root\n";
            return std::nullopt;
        }

        return a;
    }

} // namespace

int main(int argc, char **argv)
{
    struct sigaction sa{};
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    auto args = parse_args(argc, argv);
    if (!args)
        return 2;

    auto listen_fd_opt = create_listen_socket(args->port);
    if (!listen_fd_opt)
        return 1;

    int listen_fd = *listen_fd_opt;

    std::cerr << "[LISTENER] Listening on 127.0.0.1:" << args->port
              << " (root=" << args->root << ")\n";
    std::cerr << "[LISTENER] Logging to " << args->log << "\n";
    std::cerr << "[LISTENER] Ctrl+C to stop.\n";

    accept_loop(listen_fd, args->root, args->port, args->log);

    std::cerr << "[LISTENER] Shutting down...\n";
    ::close(listen_fd);
    return 0;
}
