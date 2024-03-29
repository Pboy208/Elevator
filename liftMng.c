#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <signal.h>
#include <pthread.h>

#define MSG_SIZE 100
#define KEY_FILE_PATH  "panel"
#define ID 'A'

#define FIFO_FILE_MNG_TO_CTRL "/tmp/fifo_mng_to_ctrl"
#define FIFO_FILE_CTRL_TO_MNG "/tmp/fifo_ctrl_to_mng"
#define BUFF_SIZE 32

int msg_id;
int mng_ctrl_fifo_fd[2]; // 0: write, 1: read

typedef struct msg_buffer {
    long msg_type;
    char msg_text[MSG_SIZE];
} MsgBuffer;

typedef struct Request {
    int floor;
    int deliveryFloor; // only for floor == 1
} Request;

Request *requestQueue = NULL;
int requestQueueSize = 0;
void enqueue(Request request);
Request dequeue();

void signal_handler(int sig) {
    msgctl(msg_id, IPC_RMID, NULL);
    exit(0);
}

Request getRequest(MsgBuffer message) {
    Request request;
    request.floor = (int) message.msg_type;
    if (request.floor == 1) {
        sscanf(message.msg_text, "%d", &request.deliveryFloor);
    }

    return request;
}

void send_message_to_panel(int floor, char message[]) {
    MsgBuffer message_to_panel;
    message_to_panel.msg_type = floor + 5;
    strcpy(message_to_panel.msg_text, message);

    msgsnd(msg_id, &message_to_panel, strlen(message_to_panel.msg_text), 0);
}

void performRequest(Request request) {
    int cur_floor = 1;

    int destication_floor;
    if (request.floor == 1) {
        destication_floor = request.deliveryFloor;
    } else {
        destication_floor = request.floor;
    }

    char buf[BUFF_SIZE];
    int sensor, on;

    // lift-up
    send_message_to_panel(1, "arrival 0");

    strcpy(buf, "lift-up");
    write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
    while (cur_floor != destication_floor) {
        int size = read(mng_ctrl_fifo_fd[1], buf, BUFF_SIZE);
        sscanf(buf, "%d%d", &sensor, &on);
        if (sensor == 6) {
            if (on) send_message_to_panel(1, "error 1");
            else send_message_to_panel(1, "error 0");
        } else if (on) {
            cur_floor = sensor;
        }
    }

    // lift-stop
    strcpy(buf, "lift-stop");
    write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
    send_message_to_panel(destication_floor, "arrival 1");
    sleep(3);

    if (request.floor != 1) {
        send_message_to_panel(destication_floor, "OK");
    }

    // lift-down
    send_message_to_panel(destication_floor, "arrival 0");
    strcpy(buf, "lift-down");
    write(mng_ctrl_fifo_fd[0], buf, BUFF_SIZE);
    while (cur_floor != 1) {
        read(mng_ctrl_fifo_fd[1], buf, BUFF_SIZE);
        sscanf(buf, "%d%d", &sensor, &on);
        if (on) {
            cur_floor = sensor;
        }
    }

    send_message_to_panel(1, "arrival 1");
    if (request.floor == 1) {
        char message[MSG_SIZE];
        sprintf(message, "OK %d", request.deliveryFloor);
        send_message_to_panel(1, message);
    }
}

void* liftCtrlCommunication() {
    mkfifo(FIFO_FILE_MNG_TO_CTRL, 0666);
    mng_ctrl_fifo_fd[0] = open(FIFO_FILE_MNG_TO_CTRL, O_WRONLY);

    mkfifo(FIFO_FILE_CTRL_TO_MNG, 0666);
    mng_ctrl_fifo_fd[1] = open(FIFO_FILE_CTRL_TO_MNG, O_RDONLY);
    
    while (1) {
        if (requestQueueSize == 0) {
            sleep(1);
            continue;
        }

        Request request = dequeue();
        performRequest(request);
    }
}

int main() {
    signal(SIGINT, signal_handler);

    MsgBuffer message;

    key_t key = ftok(KEY_FILE_PATH, ID);
    msg_id = msgget(key, 0666 | IPC_CREAT);

    pthread_t tid;
    pthread_create(&tid, NULL, &liftCtrlCommunication, NULL);

    while (1) {
        memset(&message, 0, sizeof(message));
        msgrcv(msg_id, &message, MSG_SIZE, -5, 0);
        printf("%ld: %s\n", message.msg_type, message.msg_text);

        enqueue(getRequest(message));
    }

    return 0;
}

void enqueue(Request request) {
    requestQueue = (Request*) realloc(requestQueue, (requestQueueSize + 1) * sizeof(Request));
    requestQueue[requestQueueSize] = request;
    requestQueueSize++;
}

Request dequeue() {
    Request top = requestQueue[0];
    memmove(requestQueue, requestQueue + 1, (requestQueueSize - 1) * sizeof(Request));
    requestQueue = (Request*) realloc(requestQueue, (requestQueueSize - 1) * sizeof(Request));
    requestQueueSize--;
    return top;
}
