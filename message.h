#ifndef MESSAGE_H
#define MESSAGE_H

#define MAX_MSG_LEN 33768  

#define TYPE_REQUEST       1   // Client requests UDP port (TCP)
#define TYPE_RESPONSE      2   // Server replies with UDP port (TCP)
#define TYPE_DATA          3   // Client sends data (UDP)
#define TYPE_DATA_ACK      4   // Server acknowledges data receipt (UDP)

typedef struct {
    int msg_type;       
    int msg_length;   
    char msg[MAX_MSG_LEN];
} Message;

#endif
