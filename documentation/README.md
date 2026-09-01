# WebCacheX Project

## Table of Contents

- [1. Overview](#1-overview)
- [2. System Architecture](#2-system-architecture)
- [3. Technologies and Tools](#3-technologies-and-tools)
- [4. Communication Contract (Simplified HTTP Protocol)](#4-communication-contract-simplified-http-protocol)
  - [4.1 Request Format](#41-request-format)
  - [4.2 Response Format](#42-response-format)
- [5. Web Servers (Origin A and Origin B)](#5-web-servers-origin-a-and-origin-b)
- [6. Cache Server (Reverse Proxy + Cache)](#6-cache-server-reverse-proxy--cache)
  - [6.1 Routing (Multi-Origin Bonus)](#61-routing-multi-origin-bonus)
  - [6.2 Cache Key](#62-cache-key)
  - [6.3 Cache States](#63-cache-states)
  - [6.4 Time-To-Live (TTL)](#64-time-to-live-ttl)
- [7. Multithreading and Thread Safety](#7-multithreading-and-thread-safety)
- [8. Logging](#8-logging)
  - [8.1 Log Columns](#81-log-columns)
  - [8.2 Screenshots (placeholders)](#82-screenshots-placeholders)
- [9. Client GUI](#9-client-gui)
- [10. How to Run](#10-how-to-run)
  - [10.1 Run WebServer A (Origin A)](#101-run-webserver-a-origin-a)
  - [10.2 Run WebServer B (Origin B)](#102-run-webserver-b-origin-b)
  - [10.3 Run CacheServer (Reverse Proxy + Cache)](#103-run-cacheserver-reverse-proxy--cache)
  - [10.4 Run the UI Client (PyQt)](#104-run-the-ui-client-pyqt)
- [11. Test Scenarios](#11-test-scenarios)
  - [11.1 MISS → HIT (same host, same path)](#111-miss--hit-same-host-same-path)
  - [11.2 EXPIRE after 10 seconds](#112-expire-after-10-seconds)
  - [11.3 Same path on A vs B → separate cache entries](#113-same-path-on-a-vs-b--separate-cache-entries)
  - [11.4 404 Not Found](#114-404-not-found)
- [12. Short Summary](#12-short-summary)
- [Minimal Project Structure (for reference)](#minimal-project-structure-for-reference)
- [Screenshots (placeholders)](#screenshots-placeholders)


---

## 1. Overview

In this project, a **three-layer system** consisting of a **Web Server**, **Cache Server**, and **Client** has been implemented.  
The main goal of the project is to design a **Cache Server acting as a Reverse Proxy**, which receives HTTP requests, forwards them to the appropriate Web Server when needed, and caches responses for a fixed period of time.

The system uses a **simplified HTTP protocol over TCP** and supports **multithreading** to handle multiple concurrent requests.  
As a bonus feature, **Multi-Origin support** is implemented, meaning the Cache Server can route requests to different Web Servers based on the `Host` header.

---

## 2. System Architecture

The overall architecture of the system is designed in three layers:

- **Client (GUI)**  
  A graphical user interface that sends HTTP requests and displays responses.

- **Cache Server (Reverse Proxy + Cache)**  
  The core component of the system. It receives requests from the Client, manages the cache, and forwards requests to the appropriate Web Server when necessary.

- **Web Server A / Web Server B (Origin Servers)**  
  Origin servers that serve actual files from different directories. Each server represents a separate origin.

High-level communication diagram:

```
Client (GUI)
     |
     v
Cache Server (8080)
     |
     +--> Web Server A (8081)
     |
     +--> Web Server B (8082)
```

Key points:
- The Client communicates **only** with the Cache Server.
- The Cache Server decides which origin server to contact based on the `Host` header in the HTTP request.

---

## 3. Technologies and Tools

The following technologies and tools were used in this project:

- **C++17** (Web Server and Cache Server implementation)
- **TCP Socket Programming**
- **Simplified HTTP protocol**
- **Multithreading**
- **PyQt** (Graphical Client)
- **WSL (Windows Subsystem for Linux)** as the execution environment

---
## 4. Communication Contract (Simplified HTTP Protocol)

This project uses a **simplified version of HTTP over TCP**.  
Only the minimum required parts of the protocol are implemented to keep the system clear and robust.

### 4.1 Request Format

Example request sent by the client to the Cache Server:

```
GET /index.html HTTP/1.1
Host: a.local
Connection: close
```

**Rules and assumptions:**
- Only the `GET` method is supported.
- Request bodies are **not** supported.
- The `Host` header is mandatory and is used for **Multi-Origin routing**.
- Requests are terminated by an empty line (`\r\n\r\n`).
- The client always connects to the **Cache Server**, never directly to the Web Servers.

---

### 4.2 Response Format

Example response returned by a Web Server or the Cache Server:

```
HTTP/1.1 200 OK
Content-Type: text/html
Cache-Control: max-age=10
Connection: close

<html>...</html>
```

**Response behavior:**
- Supported status codes:
  - `200 OK` – file found and returned
  - `404 Not Found` – requested file does not exist
- `Cache-Control: max-age=10` defines the cache TTL (10 seconds).
- After headers, an empty line is sent, followed by the response body.
- The full HTTP response (headers + body) is cached as a raw string.

---

## 5. Web Servers (Origin A and Origin B)

The project includes **two independent Web Servers** acting as origin servers.

**General behavior:**
- Each Web Server listens on a **different port**.
- Each server serves **static files** from its own root directory.
- The servers are **multi-threaded** (thread-per-connection).
- If a requested file does not exist, a `404 Not Found` response is returned.

**Server roles:**
- **Web Server A**
  - Port: `8081`
  - Root directory: `web_content_a/`
- **Web Server B**
  - Port: `8082`
  - Root directory: `web_content_b/`

These servers do not implement caching and are unaware of each other.

---

## 6. Cache Server (Reverse Proxy + Cache)

The Cache Server is the **core component** of the project ⭐  
It acts as a **Reverse Proxy** and implements caching with Multi-Origin support.

### 6.1 Routing (Multi-Origin Bonus)

Routing is based on the `Host` header of the HTTP request.

| Host value | Routed to Origin |
|-----------|------------------|
| `a.local` | `127.0.0.1:8081` |
| `b.local` | `127.0.0.1:8082` |

If the `Host` header is missing or invalid, the request is rejected.

---

### 6.2 Cache Key

To avoid mixing content from different origins, the cache key is defined as:

```
cache_key = host + "|" + path
```

This guarantees that identical paths from different origins are cached separately.

---

### 6.3 Cache States

Each request handled by the Cache Server results in one of the following states:

- **MISS** – entry not found in cache
- **HIT** – valid (fresh) cached entry is used
- **EXPIRE** – cached entry exists but is stale and removed
- **ERROR** – network or internal failure

These states are also written to the cache log.

---

### 6.4 Time-To-Live (TTL)

- The cache TTL is fixed to **10 seconds**.
- A cached entry is:
  - **Fresh** if `current_time < expires_at`
  - **Stale** if `current_time ≥ expires_at`
- Stale entries are removed before fetching a fresh response from the origin.

# 7. Multithreading and Thread Safety

Both the **Web Server** and **Cache Server** are designed to handle multiple clients concurrently.

- **Concurrency model:** thread-per-connection  
  Each incoming TCP connection is handled in its own thread.

- **Thread safety (Cache Server):**
  - A `mutex` protects shared access to the **cache map** (to avoid race conditions).
  - A `mutex` protects the **log file** writer (so log lines do not interleave / corrupt).

This prevents common issues such as:
- inconsistent cache state under concurrent requests
- corrupted / interleaved log output


# 8. Logging

Logging is implemented according to the project requirements.  
Each request produces a single log entry in CSV format (and/or a readable equivalent).

Example (human-readable):

```
2025-01-12T14:22:10Z | a.local | /index.html | HIT | 127.0.0.1:8081 | 4ms
```

## 8.1 Log Columns

- **timestamp**: UTC time in ISO-8601 format
- **host**: requested host (e.g., `a.local`, `b.local`)
- **path**: requested path (e.g., `/index.html`)
- **result**: one of `HIT`, `MISS`, `EXPIRE`, `ERROR`
- **origin**: the selected origin server (e.g., `127.0.0.1:8081`)
- **response_time_ms**: end-to-end time for serving the request (in milliseconds)

## 8.2 Screenshots (placeholders)

Screenshots of logs and the GUI are stored here:

- `documentation/image/`

Placeholders (to be filled later):

- `documentation/image/cache_logs.png`
- `documentation/image/gui_dashboard.png`


# 9. Client GUI

The project includes a simple GUI client (PyQt) to send requests to the **Cache Server** and display results.

Main features:

- Select **Host** (`a.local` / `b.local`) for Multi-Origin routing
- Enter the request **path** (e.g., `/index.html`)
- Send the request to **CacheServer (127.0.0.1:8080)**
- Display the received **HTTP response** (headers + body)

> Note: The GUI is mainly for demonstration and testing; core grading focuses on the servers, caching, and logging.


## 10. How to Run

> This project is designed to be run in **WSL** for the C++ servers, and from **Windows** (or WSL) for the Python GUI client.

### 10.1 Run WebServer A (Origin A)

Open a WSL terminal:

```bash
cd web_server

# build
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o web_server src/main.cpp

# run (Origin A)
./web_server --port 8081 --root ../web_content_a --log origin_a_log.csv
```

### 10.2 Run WebServer B (Origin B)

Open a **second** WSL terminal:

```bash
cd web_server

# run (Origin B)
./web_server --port 8082 --root ../web_content_b --log origin_b_log.csv
```

### 10.3 Run CacheServer (Reverse Proxy + Cache)

Open a **third** WSL terminal:

```bash
cd cache_server

# build (main + logger)
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -Iinclude -o cache_server src/main.cpp src/logger.cpp

# run
./cache_server
```

CacheServer listens on **127.0.0.1:8080** and routes requests based on the `Host:` header:
- `a.local` → `127.0.0.1:8081`
- `b.local` → `127.0.0.1:8082`

### 10.4 Run the UI Client (PyQt)

On Windows (PowerShell / CMD), run:

```powershell
cd ui_client
python app_dashboard.py
```

The GUI sends HTTP requests to **CacheServer (127.0.0.1:8080)**, and shows the response and logs.

---

## 11. Test Scenarios

These are simple scenarios that clearly demonstrate correctness (and the bonus multi-origin behavior).

### 11.1 MISS → HIT (same host, same path)

1) First request:
- Host: `a.local`
- Path: `/index.html`
- Expected: `MISS` (fetched from origin and cached)

2) Repeat the same request immediately:
- Expected: `HIT` (served from cache)

You can test via CLI too (optional):

```bash
printf "GET /index.html HTTP/1.1
Host: a.local
Connection: close

" | nc 127.0.0.1 8080
```

### 11.2 EXPIRE after 10 seconds

1) Request any resource (e.g., `a.local` + `/index.html`) so it gets cached  
2) Wait **> 10 seconds** (TTL is 10s)  
3) Request it again  
- Expected: cache entry was expired (removed), so the request becomes `MISS` again

### 11.3 Same path on A vs B → separate cache entries

Request `/index.html` twice with different hosts:

- `Host: a.local` + `/index.html` → caches under key `a.local|/index.html`
- `Host: b.local` + `/index.html` → caches under key `b.local|/index.html`

So even if the path is the same, caching is separated by host.

### 11.4 404 Not Found

Request a file that does not exist, for example:

- Host: `a.local`
- Path: `/does-not-exist.txt`
- Expected: origin returns `404 Not Found`, and CacheServer forwards that response to the client.

---

## 12. Short Summary

This project implements a **three-layer** system (Origin WebServers, a Reverse-Proxy CacheServer, and a GUI client) using a simplified HTTP-over-TCP protocol.  
It supports **caching with TTL=10s** and the **Multi-Origin bonus** via `Host`-based routing.  
The system is stable, easy to demo, and structured in a way that can be extended further.

---

## Minimal Project Structure (for reference)

```text
project/
├── cache_server/          # CacheServer (reverse proxy + cache) + cache_log.csv
├── web_server/            # WebServer binary + origin_a_log.csv / origin_b_log.csv
├── ui_client/             # PyQt GUI client (app_dashboard.py)
├── web_content_a/         # static files for Origin A
├── web_content_b/         # static files for Origin B
└── documentation/
    └── image/             # screenshots (UI + logs)
```

---

## Screenshots (placeholders)

> Images are located in: `documentation/image/`

- UI screenshot (placeholder):
  - `documentation/image/<ui-dashboard.png>`
  - Example markdown:
    - `![UI Dashboard](documentation/image/<ui-dashboard.png>)`

- Logs screenshot (placeholder):
  - `documentation/image/<cache-log.png>`
  - Example markdown:
    - `![Cache Log](documentation/image/<cache-log.png>)`
