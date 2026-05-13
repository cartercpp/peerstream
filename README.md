# ringnet

A peer-to-peer terminal chat in C++23. Each peer runs the same binary — it listens for incoming connections *and* dials out to known peers on startup. Incoming bytes land in a per-peer fixed-capacity ring buffer and are painted live to the console with direct cursor positioning. No server, no framing protocol, no dependencies beyond Winsock.

## How it works

Each node does three things in parallel:

- **Accept thread** — `select()` on the listening socket with a 250 ms timeout, then `accept()` and spin up a listener thread per new peer. The timeout is what lets the thread notice `stop_token` requests instead of blocking forever.
- **Per-peer listener threads** — one `jthread` per peer, blocking on `recv()`, pushing each received byte into that peer's ring buffer, then notifying the display.
- **Display thread** — wakes on the condition variable, walks every peer entry, and uses `SetConsoleCursorPosition` to paint the peer's IP and the current ring buffer contents on its own row. No scrolling, no terminal redraws — just absolute positioning.

The main thread reads keystrokes with `_getch()` and fans them out to every connected peer socket. `$` triggers cleanup: stop token requested, sockets closed, threads joined, `WSACleanup()`.

### The ring buffer

`ring_buffer<T, Capacity>` is a fixed-capacity circular buffer with `std::mutex` + `std::condition_variable` baked in. `push` is non-blocking and overwrites the oldest element when full (chat history naturally rolls off). `blocking_pop` / `blocking_front` / `blocking_back` wait on the CV until something is there. The whole thing is templated on capacity so the storage is a `std::array` — no heap allocation per peer.

## Build

Windows only right now — uses Winsock2 and `<conio.h>` directly.

```
cl /std:c++latest /EHsc main.cpp help.cpp
```

Or open the folder in Visual Studio / CLion and build.

## Run

Edit `knownPeerIpAddresses` in `main.cpp` with the LAN IPs of the other nodes, build, and run the same binary on each machine. They'll find each other on port 8080. Type to broadcast, `$` to quit.

## Files

```
main.cpp          // node orchestration: threads, accept loop, display, input
help.cpp          // CreateSocket — getaddrinfo + socket + bind/listen or connect
ring_buffer.hpp   // thread-safe fixed-capacity ring buffer
ip_type.hpp       // IPV4 / IPV6 enum
```

## Known limitations

- Windows-only.
- `knownPeerIpAddresses` is hardcoded. No discovery, no config file.
- The accept thread takes `mtx` to mutate the peer vectors, but the connect-on-startup loop doesn't. Single-threaded at that point, but worth tightening if startup ever races with the accept loop.

## License

MIT.
