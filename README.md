# Windows IOCP Chat Server

게임 서버 기본기 검증용으로 만든 최소 범위의 Windows IOCP 기반 TCP 채팅 서버입니다.

## 개요

이 프로젝트는 아래 범위만 구현합니다.

- C++
- TCP
- 길이 헤더 기반 패킷
- 멀티 클라이언트 접속
- 닉네임 설정
- 채팅방 생성 / 입장 / 퇴장
- 방 단위 브로드캐스트
- 접속 종료 처리
- 서버 로그
- 콘솔 테스트 클라이언트

아래 항목은 의도적으로 제외했습니다.

- DB
- Redis
- GUI 클라이언트
- 파일 전송
- 귓속말 / 친구 / 차단 / 이모지
- 재접속 복구
- TLS / 암호화
- 운영자 페이지

## 핵심 구조

- `Session`
  - 클라이언트 소켓, 비동기 recv/send 상태, 닉네임, 현재 방 정보를 관리합니다.
- `Room`
  - 방 멤버를 보관하고 브로드캐스트용 멤버 스냅샷을 제공합니다.
- `PacketParser`
  - TCP 스트림을 누적해서 길이 헤더 기준으로 완성된 패킷을 분리합니다.
- `Server`
  - IOCP, `AcceptEx`, 세션/방 관리, 패킷 디스패치, 종료 정리를 담당합니다.

## 처리 흐름

```text
recv
-> 버퍼 누적
-> 패킷 완성
-> 디스패치
-> 룸 브로드캐스트
```

## 프로젝트 구조

```text
IocpChatServer.sln
Common/
  Protocol.h
ChatServer/
  Main.cpp
  Server.h / Server.cpp
  Session.h / Session.cpp
  Room.h / Room.cpp
  PacketParser.h / PacketParser.cpp
ChatClient/
  Main.cpp
docs/
  protocol.md
  test-scenario.md
  sample-screenshot.svg
```

## 프로토콜 요약

자세한 내용은 [docs/protocol.md](./docs/protocol.md)에서 확인할 수 있습니다.

- 헤더: `uint16 packetSize + uint16 packetType`
- 문자열 필드: `uint16 length + bytes`
- 최대 패킷 크기: `4096`

## 빌드

1. Visual Studio 2022에서 [IocpChatServer.sln](./IocpChatServer.sln)을 엽니다.
2. `x64 / Debug` 또는 `x64 / Release`를 선택합니다.
3. 솔루션 전체를 빌드합니다.

빌드 결과물은 아래 경로에 생성됩니다.

- `x64/Debug/ChatServer/ChatServer.exe`
- `x64/Debug/ChatClient/ChatClient.exe`

## 실행

서버:

```powershell
.\x64\Debug\ChatServer\ChatServer.exe 7777
```

클라이언트:

```powershell
.\x64\Debug\ChatClient\ChatClient.exe 127.0.0.1 7777
```

클라이언트 명령어:

```text
/nick Alice
/create Lobby
/join Lobby
/leave
/quit
```

명령어가 아니면 일반 채팅 메시지로 전송됩니다.

## 수동 테스트

자세한 절차는 [docs/test-scenario.md](./docs/test-scenario.md)에 정리했습니다.

빠른 확인 순서:

1. 서버를 실행합니다.
2. 클라이언트 A에서 아래를 입력합니다.

```text
/nick Alice
/create Lobby
hello from alice
```

3. 클라이언트 B에서 아래를 입력합니다.

```text
/nick Bob
/join Lobby
hello from bob
```

4. 두 클라이언트가 같은 방 메시지를 함께 받는지 확인합니다.
5. 클라이언트 B에서 `/leave`를 입력합니다.
6. 한 클라이언트를 종료하고 서버 로그에서 접속 종료 및 방 정리를 확인합니다.

## 예시 화면

벡터 예시 파일:

- [docs/sample-screenshot.svg](./docs/sample-screenshot.svg)

## 설계 메모

- TCP는 스트림 기반이므로 패킷 경계를 길이 헤더로 직접 복원해야 합니다.
- 세션 종료 시 현재 방에서 즉시 제거해서 방 멤버 정보가 남지 않도록 처리했습니다.
- 룸 멤버 관리와 실제 브로드캐스트 전송 책임을 나눠 구조를 단순하게 유지했습니다.
