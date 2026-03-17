# Protocol

## Header

Every packet starts with a 4-byte header:

| Field | Type | Description |
| --- | --- | --- |
| `packetSize` | `uint16` | total packet size including header |
| `packetType` | `uint16` | packet type |

String fields are serialized as:

```text
uint16 length + raw bytes
```

## Packet Types

### Client to Server

| Name | Value | Payload |
| --- | --- | --- |
| `C2S_SetNickname` | `1` | `string nickname` |
| `C2S_CreateRoom` | `2` | `string roomName` |
| `C2S_JoinRoom` | `3` | `string roomName` |
| `C2S_LeaveRoom` | `4` | none |
| `C2S_Chat` | `5` | `string message` |

### Server to Client

| Name | Value | Payload |
| --- | --- | --- |
| `S2C_Welcome` | `100` | `string message` |
| `S2C_LoginAck` | `101` | `string nickname` |
| `S2C_RoomJoined` | `102` | `string roomName` |
| `S2C_RoomLeft` | `103` | `string roomName` |
| `S2C_RoomMessage` | `104` | `string sender`, `string message` |
| `S2C_SystemMessage` | `105` | `string message` |
| `S2C_Error` | `106` | `string reason` |

## Example Packets

Set nickname to `Alice`:

```text
[size=13][type=1][len=5]["Alice"]
```

Chat message `hello`:

```text
[size=13][type=5][len=5]["hello"]
```

## Server-Side Validation

- nickname and room name: `1..20` chars, no spaces
- chat message: must not be empty
- max packet size: `4096` bytes

## Processing Flow

```text
recv
-> Session::HandleRecv
-> PacketParser append
-> complete packet extraction
-> Server::DispatchPacket
-> room broadcast or direct response
```
