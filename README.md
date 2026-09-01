# WebCacheX

Course project for **Computer Networks** at Ferdowsi University of Mashhad (FUM). A multi-threaded reverse-proxy cache with two origin web servers and a PyQt dashboard for sending requests and inspecting CSV logs.

## Architecture

```
PyQt Client (GUI)
       |
       v
Cache Server :8080  (reverse proxy + TTL cache)
       |
       +--> Origin A :8081  (web_content_a/)
       |
       +--> Origin B :8082  (web_content_b/)
```

The client talks only to the cache. Routing uses the HTTP `Host` header (`a.local` / `b.local`). Cache keys are `host|path`, TTL is 10 seconds, and each request is logged as `HIT`, `MISS`, `EXPIRE`, or `ERROR`.

## Features

| Area | Details |
|------|---------|
| Protocol | Simplified HTTP/1.1 over TCP (`GET` only) |
| Cache | In-memory map, 10s TTL, host-aware keys |
| Origins | Two static file servers on separate ports |
| Concurrency | Thread-per-connection; mutex-protected cache and logs |
| GUI | Request client + live log viewers for cache and both origins |
| Build | CMake or direct `g++` (Linux / WSL) |

## Quick start

### Prerequisites

- **WSL** or Linux for the C++ servers
- **Python 3** + PyQt5 for the dashboard
- `g++` with C++17 and pthread, or CMake 3.16+

### 1. Build and run Origin A

```bash
cd web_server
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o web_server src/main.cpp
./web_server --port 8081 --root ../web_content_a --log origin_a_log.csv
```

### 2. Run Origin B (second terminal)

```bash
cd web_server
./web_server --port 8082 --root ../web_content_b --log origin_b_log.csv
```

### 3. Run Cache Server (third terminal)

```bash
cd cache_server
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -Iinclude -o cache_server src/main.cpp src/logger.cpp
./cache_server
```

### 4. Run the dashboard

```bash
pip install -r requirements.txt
cd ui_client
python app_dashboard.py
```

### CMake (optional)

```bash
cmake -S . -B build
cmake --build build
# binaries: build/web_server_bin, build/cache_server_bin
```

## Test with netcat

```bash
printf "GET /index.html HTTP/1.1\r\nHost: a.local\r\nConnection: close\r\n\r\n" | nc 127.0.0.1 8080
```

Repeat immediately for a cache `HIT`; wait more than 10 seconds for `EXPIRE` / `MISS`.

## Project layout

```text
webcachex/
├── cache_server/       # Reverse proxy + cache
├── web_server/         # Origin static file server
├── ui_client/          # PyQt dashboard
├── web_content_a/      # Origin A files
├── web_content_b/      # Origin B files
├── documentation/      # Detailed course docs
└── CMakeLists.txt
```

See [`documentation/README.md`](documentation/README.md) for the full design write-up, test scenarios, and protocol contract.

## Author

Arman Bijari

## License

MIT — see [LICENSE](LICENSE).
