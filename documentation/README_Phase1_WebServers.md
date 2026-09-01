# Phase 1 — Web Servers (Origin A & B)

This phase implements two independent HTTP origin servers in **C++**, designed to be used later behind a cache server.
The goal is to have a clean, correct, and demonstrable baseline before introducing caching and routing logic.

---

## Overview

- **WebServer A**
  - Listens on: `127.0.0.1:8081`
  - Serves files from: `web_content_a/`

- **WebServer B**
  - Listens on: `127.0.0.1:8082`
  - Serves files from: `web_content_b/`

Both servers use the **same binary** and codebase.  
Only the port and content root differ via command-line arguments.

---

## Features Implemented

### Networking
- TCP socket (`AF_INET`, `SOCK_STREAM`)
- `SO_REUSEADDR` enabled
- Explicit bind to `127.0.0.1`
- `listen()` + `accept()` loop

### Concurrency
- **Thread-per-connection** model
- Each client connection handled in its own `std::thread`
- Proper resource cleanup using RAII (`FdGuard`)

### HTTP Handling
- Minimal HTTP/1.1 request parsing
- Reads until `\r\n\r\n` (end of headers)
- Parses request line:
  ```
  GET /path HTTP/1.1
  ```
- Only `GET` is supported
- Invalid methods return `405 Method Not Allowed`

### Path Handling & Security
- `/` is normalized to `/index.html`
- Requests containing `..` are rejected (`400 Bad Request`)
- Prevents directory traversal attacks
- Full file path is resolved as:
  ```
  full_path = root_dir + normalized_path
  ```

### Responses
- **200 OK** for existing files
- **404 Not Found** for missing files
- Proper HTTP response format:
  - Status line
  - Headers
  - Blank line
  - Body

### Headers
- `Content-Type` based on file extension
- `Content-Length` correctly set
- `Cache-Control: max-age=10` (required for cache semantics)
- `Connection: close`

### Supported Content-Types
| Extension | Content-Type |
|---------|--------------|
| `.html` / `.htm` | `text/html` |
| `.txt` | `text/plain` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.png` | `image/png` |
| `.jpg` / `.jpeg` | `image/jpeg` |
| other | `application/octet-stream` |

---

## Build Instructions

From the `web_server/` directory:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread     -o web_server src/main.cpp
```

---

## Run Instructions

### Start WebServer A
```bash
./web_server --port 8081 --root ../web_content_a --log origin_a_log.csv
```


### Start WebServer B
```bash
./web_server --port 8082 --root ../web_content_b --log origin_b_log.csv
```

---

## Testing (using netcat)

### Successful request (200 OK)
```bash
printf "GET /index.html HTTP/1.1\r\nHost: a.local\r\nConnection: close\r\n\r\n" | nc 127.0.0.1 8081
```

### Root path normalization
```bash
printf "GET / HTTP/1.1\r\nHost: a.local\r\n\r\n" | nc 127.0.0.1 8081
```

### Not Found (404)
```bash
printf "GET /nope.html HTTP/1.1\r\nHost: a.local\r\n\r\n" | nc 127.0.0.1 8081
```

### Directory traversal attempt (blocked)
```bash
printf "GET /../../etc/passwd HTTP/1.1\r\nHost: a.local\r\n\r\n" | nc 127.0.0.1 8081
```

### Verify A/B separation
```bash
printf "GET /index.html HTTP/1.1\r\nHost: b.local\r\n\r\n" | nc 127.0.0.1 8082
```

---

## Definition of Done — Phase 1

- WebServer A is reachable on port **8081**
- WebServer B is reachable on port **8082**
- Each server serves content from its own directory
- HTTP `GET` requests are parsed correctly
- `/` maps to `/index.html`
- Directory traversal is blocked
- `200 OK` and `404 Not Found` responses are correct
- `Cache-Control: max-age=10` is present on successful responses
- Multiple clients can connect concurrently
- No socket or thread resource leaks

---

## Next Phase

**Phase 2 — Cache Server**
- CacheServer on port `8080`
- Forward requests to A or B based on `Host` header
- Cache responses using `(host | path)` as cache key
- Implement TTL-based freshness and stale handling
