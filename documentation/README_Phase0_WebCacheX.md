# WebCacheX — Phase 0: System Contract & Bonus Scenario

This document defines the **system-level contract** for the WebCacheX project, including the **bonus multi-origin caching scenario**.  
It serves as the foundation for all subsequent implementation phases and will be referenced throughout development.

The goal of this phase is **not implementation**, but to precisely define *how components interact*, *what is supported*, and *what guarantees the system provides*.

---

## 1. System Overview

WebCacheX is a **three-layer networked system** consisting of:

- A **Browser Client (GUI)**  
- A **Cache Server (Proxy / Reverse Proxy)**  
- Multiple **Origin Web Servers**

The client **never communicates directly with origin servers**.  
All requests are sent to the Cache Server, which decides whether to serve a cached response or forward the request to the appropriate origin.

This design follows a simplified **reverse proxy caching model**.

---

## 2. Components and Network Topology

| Component        | Address             | Role |
|------------------|---------------------|------|
| Cache Server     | `127.0.0.1:8080`    | Reverse proxy, cache, routing, logging |
| Web Server A     | `127.0.0.1:8081`    | Origin server for `a.local` |
| Web Server B     | `127.0.0.1:8082`    | Origin server for `b.local` |
| GUI Client       | —                   | Sends HTTP requests to Cache Server |

All communication uses **TCP sockets**.

---

## 3. Bonus Feature: Multi-Origin Routing

### 3.1 Routing Rule

The system supports **multiple origin servers** as a bonus feature.

- The client includes a `Host` header in each request.
- The Cache Server uses this header to determine the destination origin.

Routing table:

| Host header | Origin server |
|------------|---------------|
| `a.local`  | `127.0.0.1:8081` |
| `b.local`  | `127.0.0.1:8082` |

If the `Host` header is missing or unknown, the Cache Server responds with:

```
HTTP/1.1 400 Bad Request
```

This strict behavior makes routing explicit and easily verifiable during evaluation.

---

## 4. HTTP Message Contract (Simplified HTTP/1.1)

The system supports a **minimal, well-defined subset of HTTP/1.1**.

### 4.1 Supported Request Format

Only the `GET` method is supported.

```
GET /path HTTP/1.1
Host: a.local
Connection: close

```

Rules:
- Requests must end headers with `\r\n\r\n`
- No request body is supported
- Headers other than `Host` are ignored

---

### 4.2 Supported Response Format

```
HTTP/1.1 200 OK
Content-Type: text/html
Cache-Control: max-age=10
Content-Length: 123
Connection: close

<html>...</html>
```

Or, if the file does not exist:

```
HTTP/1.1 404 Not Found
```

Notes:
- Responses follow the standard structure:  
  **status line → headers → empty line → body**
- The Cache Server stores the **entire raw HTTP response** (headers + body) for simplicity and correctness.

---

## 5. Cache Model and Expiration Policy

### 5.1 Cache Key Definition (Bonus-Aware)

To prevent conflicts between different origins, cache entries are indexed using:

```
cache_key = host + "|" + path
```

Example:
```
a.local|/index.html
b.local|/index.html
```

These are treated as **distinct cache entries**.

---

### 5.2 Cache Lifetime

- Cache TTL is **fixed at 10 seconds**, as required by the project specification.
- TTL semantics follow standard HTTP freshness concepts:
  - **Fresh**: current time < expiration time
  - **Stale**: current time ≥ expiration time

The `Cache-Control: max-age=10` header is included for clarity, but the Cache Server enforces the TTL explicitly.

---

### 5.3 Cache Decision Logic

For each incoming request:

| Condition | Action |
|---------|--------|
| Entry exists and is fresh | **HIT** → return cached response |
| Entry exists but is stale | **EXPIRE** → remove entry, then fetch from origin |
| Entry does not exist | **MISS** → fetch from origin |

Expired entries are removed lazily during access.

---

## 6. Logging Contract (`cache_log.txt`)

The Cache Server logs **one line per client request**.

### 6.1 Log Format

```
timestamp | client_ip:port | host | path | result | origin_ip:port | response_time_ms | bytes
```

### 6.2 Result Field Semantics

- `HIT`     → served from cache
- `MISS`    → fetched from origin
- `EXPIRE`  → stale cache entry replaced
- `ERROR`   → network or parsing error

### 6.3 Timing Definition

`response_time_ms` is measured from:
> the moment the request is fully received from the client  
> until the response is completely sent back.

This provides a consistent performance metric.

---

## 7. Concurrency Model (Design-Level Decision)

- The Cache Server and Web Servers are **multi-threaded**
- Connection handling model:
  - Thread-per-connection (simple and acceptable for educational use)
- Shared resources:
  - Cache map → protected by mutex
  - Log file → protected by mutex

No advanced eviction (e.g., LRU) is required; expiration-based eviction is sufficient.

---

## 8. GUI Client Contract (PyQt)

The Browser Client is implemented using **PyQt** and communicates only with the Cache Server.

The GUI must allow the user to:
- Select the origin host (`a.local` or `b.local`)
- Enter a resource path (e.g. `/index.html`)
- Send the request
- View the full HTTP response

Networking is handled via `QTcpSocket` in asynchronous mode (signal/slot based).

The GUI generates requests exactly following the request format defined in Section 4.

---

## 9. Definition of Done — Phase 0

Phase 0 is considered complete when:

1. All components and ports are clearly defined.
2. Request and response formats are fully specified.
3. Multi-origin routing behavior is unambiguous.
4. Cache key, TTL, and HIT/MISS/EXPIRE semantics are fixed.
5. Logging format and timing rules are defined.
6. GUI responsibilities and request structure are documented.

---

This contract will be referenced throughout implementation and ensures that **correctness, bonus features, and evaluation criteria are all satisfied by design**, not by accident.
