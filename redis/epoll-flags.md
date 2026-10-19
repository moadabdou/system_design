# Poll and epoll Flags in the TCP Server

This prototype uses Linux `epoll`, so its flag names start with `EPOLL`.
`poll()` uses related names such as `POLLIN` and `POLLOUT`. Events describe
socket readiness; they do not describe complete commands or replies.

## Flag Meanings

| epoll flag | poll equivalent | Meaning |
| --- | --- | --- |
| `EPOLLIN` | `POLLIN` | Input is readable. On the listening socket, accept pending connections; on a client socket, try `recv()`. Reading can also reveal EOF. |
| `EPOLLOUT` | `POLLOUT` | Writing is possible. This does not guarantee space for the entire reply or mean the application has replies queued. |
| `EPOLLRDHUP` | `POLLRDHUP` | The peer closed its connection or shut down its sending side. Previously sent input may still need reading. |
| `EPOLLHUP` | `POLLHUP` | Hangup. Unread data may remain; this flag alone does not mean the receive buffer is empty. |
| `EPOLLERR` | `POLLERR` | An error occurred on the descriptor. This prototype closes the connection immediately. |
| `EPOLLPRI` | `POLLPRI` | Exceptional input, such as TCP out-of-band data. Not used by this protocol. |
| — | `POLLNVAL` | `poll()` reports an invalid descriptor. There is no equivalent epoll event flag. |

`EPOLLERR` and `EPOLLHUP` can be reported without being requested. Other flags
are selected through the interest mask. See the Linux manuals for
[epoll flags](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html) and
[poll flags](https://man7.org/linux/man-pages/man2/poll.2.html).

## One Event Can Contain Several Flags

The returned `events` value is a bitmask. A socket can be readable and writable
in the same notification:

```cpp
if (events & EPOLLIN) {
    // Try reading.
}
if (events & EPOLLOUT) {
    // Try writing queued replies.
}
```

In [handle_socket_event()](prototype/src/server_network.cpp), the actual guards
also check the I/O helper's return value:

```cpp
if ((events & EPOLLIN) && !read_available(connection)) {
    close_connection(fd);
    return;
}
```

Short-circuit evaluation means `read_available()` runs only for `EPOLLIN`.
Its boolean means whether I/O can continue on the connection: `false` indicates
a fatal read error, not simply that no bytes were available. The write guard
works the same way.

## The System Call Determines What Happened

| Result | Read handling in this server | Write handling in this server |
| --- | --- | --- |
| Positive byte count | Append exactly those bytes to `input_buffer`. | Advance `output_offset` by exactly that count. |
| `-1`, `EAGAIN` or `EWOULDBLOCK` | Stop for now; keep the connection. | Keep unsent bytes and wait for another writable event. |
| `-1`, `EINTR` | Retry within the read-attempt budget. | Retry. |
| Other error | Return `false`; handler closes the connection. | Return `false`; handler closes the connection. |
| `0` | EOF: set `peer_closed_write`. | With bytes pending, this implementation treats no progress as failure. |

Readiness is a notification to attempt I/O, not a reservation of bytes or space.
The nonblocking system calls remain authoritative. See
[recv()](https://man7.org/linux/man-pages/man2/recv.2.html) and
[send()](https://man7.org/linux/man-pages/man2/send.2.html).

## Why update_interest() Changes the Flags

The current implementation builds its subscription from connection state:

```cpp
event.events = connection.peer_closed_write ? 0U : (EPOLLRDHUP | EPOLLIN);
if (connection.has_pending_output()) {
    event.events |= EPOLLOUT;
}
```

| Connection state | Requested events |
| --- | --- |
| Still accepting input, no queued output | `EPOLLIN | EPOLLRDHUP` |
| Still accepting input, output pending | `EPOLLIN | EPOLLRDHUP | EPOLLOUT` |
| Input EOF confirmed, output pending | `EPOLLOUT` |

`has_pending_output()` checks `output_offset < output_buffer.size()`. It means
there are unsent reply bytes, including a possible partial reply.

Leaving `EPOLLOUT` enabled with no output could repeatedly wake the loop because
the socket is usually writable. After writing, `update_interest()` removes it
when the output is empty. After input EOF, it removes read interests.
`queue_reply()` also calls it to enable writing when a reply is queued.
Calling it with unchanged flags is redundant but keeps the implementation simple.

## Level-Triggered Notifications and the Two Input Buffers

This server does not request `EPOLLET`, so it uses level-triggered notifications.
If the read budget expires while the kernel still has unread socket data,
epoll keeps reporting readability. The client need not send anything new.

Bytes already copied into the application's `input_buffer` are outside epoll's
view. `drain_buffered_commands()` is the sole command-processing point and
processes at most `COMMANDS_PER_PASS` (32) commands per connection per loop
iteration. Buffered commands therefore continue without another socket event.
Replies it queues are sent during a later writable event.

`EPOLLET` selects edge-triggered behavior. An implementation using it generally
drains I/O until `EAGAIN` before waiting again, or explicitly schedules unfinished
work. `EPOLLONESHOT` disables notifications after delivery until rearmed with
`EPOLL_CTL_MOD`. Neither modifier is used here. See
[epoll notification modes](https://man7.org/linux/man-pages/man7/epoll.7.html).

## A Client Finishes Sending but Still Wants Replies

1. The client sends a pipeline and calls `shutdown(SHUT_WR)`.
2. The server reads the queued bytes. Once `recv()` returns `0`, it permanently
   sets `peer_closed_write = true` for this connection. `EPOLLRDHUP` reports the
   peer's shutdown; the flag itself is not what sets this application field.
3. The drain pass executes complete buffered commands over as many iterations
   as needed. An unfinished trailing frame cannot receive more bytes and is
   discarded when reached.
4. The server sends pending replies, then closes the connection.

`EAGAIN` does not set `peer_closed_write`: the client can still send more later.
After EOF, the client must establish a new connection to send new commands.

## Clearing Output: Two Different Cases

After a successful drain of outgoing bytes, `clear_output()` empties the string
and resets the offset for reuse. There are no unsent bytes to discard.

When a buffer exceeds the 8 MiB limit, this prototype instead discards pending
output and closes promptly. It avoids waiting indefinitely for a client that
does not read. Some earlier commands may already have executed, so a missing
reply does not prove that retrying a command is safe.

See [the implementation report](implementation-report.md) for the complete
request flow and [the source](prototype/src/server_network.cpp) for the exact
connection cleanup conditions.
