#ifndef PROTOCOL_H
#define PROTOCOL_H

// 공통 상수
#define MAX_BUF     1024
#define MAX_NAME    20
#define MAX_CLIENTS 10
#define SERVER_PORT 9000

// 메시지 타입 정의
#define MSG_LOGIN           1
#define MSG_LOGIN_OK        2
#define MSG_LOGIN_FAIL      3
#define MSG_CHAT            4 //기본 일반 채팅

// 파일 요청
#define MSG_FILE_UPLOAD     5
#define MSG_FILE_DOWNLOAD   6

// 파일 전송
#define MSG_FILE_READY      7      // 서버: 업로드 준비 완료
#define MSG_FILE_DATA       8      // 파일 데이터 청크
#define MSG_FILE_END        9      // 파일 전송 종료

// 종료 및 기타
#define MSG_EXIT            10
#define MSG_ERROR           11      // 파일 없음/오류 제어용


//귓속말 전송
#define MSG_DM 12
#define MSG_DM_FAIL         13 //귓속말 대상 없음 에러
#define MSG_LIST_REQEUST 20
#define MSG_LIST_RESPONSE 21

//사용자 강퇴 후 전송 메시지
#define MSG_KICK_NOTICE 99

// 공통 메시지 구조체
typedef struct {
    int type;                      // 메시지 타입
    char sender[MAX_NAME];         // 보내는 사람 (ID)
    char target[MAX_NAME];
    char data[MAX_BUF];            // 문자열, 파일 청크 등
    int data_len;                  // 파일 전송 시 유효 바이트 수
} Message;

#endif
