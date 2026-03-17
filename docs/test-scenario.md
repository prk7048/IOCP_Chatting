# Manual Test Scenario

## 준비

1. `ChatServer` 프로젝트를 실행합니다.
2. `ChatClient`를 2개 이상 실행합니다.

## 시나리오

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

## 기대 결과

- 서버 콘솔에 접속 / 닉네임 변경 / 방 생성 / 입장 / 퇴장 / 종료 로그가 남습니다.
- Client A, B 모두 `Lobby` 안에서의 채팅이 함께 보입니다.
- Client B가 `/leave` 하면 더 이상 `Lobby` 메시지를 받지 않습니다.
- 마지막 사용자가 나가면 방이 자동 제거됩니다.

## 종료 테스트

1. Client A 또는 B에서 `/quit`
2. 서버 로그에서 세션 종료 메시지 확인
3. 사용자가 방 안에 있었다면 방 정리와 퇴장 브로드캐스트 확인
