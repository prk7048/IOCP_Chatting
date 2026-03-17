# Windows IOCP Chat Server

게임서버 기본기 검증용으로 만든 최소 범위의 Windows IOCP 기반 TCP 채팅서버입니다.

## 구현 범위

- C++
- TCP
- 길이 헤더 기반 패킷
- 멀티 클라이언트 접속
- 닉네임 설정
- 채팅방 생성 / 입장 / 퇴장
- 방 안 브로드캐스트
- 접속 종료 처리
- 서버 로그
- README
- 테스트용 콘솔 클라이언트

다음 항목은 의도적으로 포함하지 않았습니다.

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
  - 클라이언트 소켓, IOCP recv/send, 닉네임, 현재 방 상태를 관리합니다.
- `Room`
  - 같은 방의 세션 목록을 보관하고 브로드캐스트 대상 스냅샷을 제공합니다.
- `PacketParser`
  - TCP 스트림을 누적 버퍼로 받아 길이 헤더 기준으로 완전한 패킷을 분리합니다.
- `Server`
  - IOCP, `AcceptEx`, 세션/방 맵, 패킷 디스패치와 종료 정리를 담당합니다.

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

자세한 표는 [docs/protocol.md](./docs/protocol.md)에 정리되어 있습니다.

- 헤더 구조: `uint16 packetSize + uint16 packetType`
- 문자열 필드: `uint16 length + bytes`
- 처리 흐름: `recv -> 누적 -> 패킷 완성 -> 디스패치 -> 룸 브로드캐스트`

## 빌드

1. Visual Studio 2022에서 [IocpChatServer.sln](./IocpChatServer.sln)을 엽니다.
2. `x64 / Debug` 또는 `x64 / Release`로 빌드합니다.
3. `ChatServer`를 먼저 실행하고, 그 다음 `ChatClient`를 여러 개 실행합니다.

## 실행 예시

서버:

```powershell
ChatServer.exe 7777
```

클라이언트:

```powershell
ChatClient.exe 127.0.0.1 7777
```

클라이언트 명령어:

```text
/nick Alice
/create Lobby
/join Lobby
/leave
/quit
```

명령어가 아니면 일반 채팅으로 전송됩니다.

## 테스트 시나리오

1. 클라이언트 A 접속 후 `/nick Alice`
2. 클라이언트 A가 `/create Lobby`
3. 클라이언트 B 접속 후 `/nick Bob`
4. 클라이언트 B가 `/join Lobby`
5. A, B가 일반 문자열을 입력해 방 브로드캐스트 확인
6. B가 `/leave`
7. A 또는 B를 종료해 접속 종료 로그와 방 정리 확인

상세 절차는 [docs/test-scenario.md](./docs/test-scenario.md)를 참고하면 됩니다.

예시 스크린샷 산출물은 [docs/sample-screenshot.svg](./docs/sample-screenshot.svg)에 포함했습니다.

## 고민했던 점

- TCP는 스트림이므로 패킷 경계가 없어서 `PacketParser`가 누적 버퍼를 기준으로 길이 헤더를 해석합니다.
- 세션 종료 시 `Server::HandleSessionClosed`에서 현재 방을 빠져나오게 하여 룸 멤버십이 남지 않도록 정리합니다.
- 룸 단위 브로드캐스트는 `Room`이 멤버 스냅샷만 제공하고, 실제 패킷 생성과 전송은 `Server`에서 분리해 책임을 나눴습니다.

## 검증 메모

현재 이 작업 환경에는 Visual Studio / MSVC 빌드 도구가 PATH에 잡혀 있지 않아 실제 컴파일은 여기서 수행하지 못했습니다. 대신 바로 열어서 빌드 가능한 솔루션과 수동 검증용 콘솔 클라이언트, 테스트 시나리오, 예시 스크린샷 파일까지 함께 포함했습니다.
