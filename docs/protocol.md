# Protocol

## 헤더 구조

모든 패킷은 아래 4바이트 헤더로 시작합니다.

| 필드 | 타입 | 설명 |
| --- | --- | --- |
| `packetSize` | `uint16` | 헤더 포함 전체 패킷 길이 |
| `packetType` | `uint16` | 패킷 종류 |

문자열은 `uint16 length + raw bytes` 형식으로 직렬화합니다.

## 패킷 타입

### Client -> Server

| 이름 | 값 | Payload |
| --- | --- | --- |
| `C2S_SetNickname` | `1` | `string nickname` |
| `C2S_CreateRoom` | `2` | `string roomName` |
| `C2S_JoinRoom` | `3` | `string roomName` |
| `C2S_LeaveRoom` | `4` | 없음 |
| `C2S_Chat` | `5` | `string message` |

### Server -> Client

| 이름 | 값 | Payload |
| --- | --- | --- |
| `S2C_Welcome` | `100` | `string message` |
| `S2C_LoginAck` | `101` | `string nickname` |
| `S2C_RoomJoined` | `102` | `string roomName` |
| `S2C_RoomLeft` | `103` | `string roomName` |
| `S2C_RoomMessage` | `104` | `string sender`, `string message` |
| `S2C_SystemMessage` | `105` | `string message` |
| `S2C_Error` | `106` | `string reason` |

## 예시 패킷

닉네임 설정 `Alice`

```text
[size=13][type=1][len=5]["Alice"]
```

채팅 메시지 `hello`

```text
[size=13][type=5][len=5]["hello"]
```

## 처리 흐름

```text
recv
 -> Session::HandleRecv
 -> PacketParser 누적 버퍼 append
 -> 완성된 패킷 추출
 -> Server::DispatchPacket
 -> Room 브로드캐스트 또는 세션 응답
```

## 서버 측 검증 규칙

- 닉네임 / 방 이름: 1~20자, 공백 불가
- 채팅 메시지: 빈 문자열 금지
- 패킷 길이: 최대 4096바이트
