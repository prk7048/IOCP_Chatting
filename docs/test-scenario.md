# Manual Test Scenario

## Setup

1. Build the solution in `x64 / Debug`.
2. Run `ChatServer`.
3. Run two instances of `ChatClient`.

## Scenario

### Client A

```text
/nick Alice
/create Lobby
hello from alice
```

### Client B

```text
/nick Bob
/join Lobby
hello from bob
/leave
```

## Expected Result

- the server console prints connect, nickname, room, chat, leave, and disconnect logs
- both clients receive room chat while they are in `Lobby`
- after `/leave`, client B no longer receives room messages
- when the last user leaves, the room is removed automatically

## Disconnect Test

1. Enter `/quit` on one client.
2. Confirm the server prints a disconnect log.
3. If that client was inside a room, confirm room cleanup is also applied.
