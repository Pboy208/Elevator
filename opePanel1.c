#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <pthread.h>

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

#define MSG_SIZE 100
#define KEY_FILE_PATH  "panel"
#define ID 'A'

int msg_id;
sem_t state_read; //, state_write;

int floor_level = 1;
int delivery_pressed[5]; // ignore index 0
int lamp_state = 1;      // 0: off, 1: arrival, 2: error

typedef struct msg_buffer {
    long msg_type;
    char msg_text[MSG_SIZE];
} MsgBuffer;

void clearScreen() {
    printf("%c[2J%c[;H",(char) 27, (char) 27);
}

void draw_panel() {
    if (lamp_state == 0)
        printf("(" WHT "O" RESET ")\n"); // off - white
    else if (lamp_state == 1)
        printf("(" GRN "O" RESET ")\n"); // arrival - green
    else
        printf("(" RED "O" RESET ")\n"); // error - red

    printf("---\n");

    for (int i = 5; i >= 2; i--) {
        if (delivery_pressed[i - 1] == 0)
            printf("(" WHT "%d" RESET ")\n", i);
        else
            printf("(" GRN "%d" RESET ")\n", i);
    }
}

int getDeliveryFloorInput() {
    int delivery_floor;
    char buffer[16];
    while (1) {
        fgets(buffer, 16, stdin);
        if (sscanf(buffer, "%d", &delivery_floor) == 1) {
            if (2 <= delivery_floor && delivery_floor <= 5) {
                return delivery_floor;
            }
        }

        printf("Invalid Floor, try again: ");
    }   
}

void* draw_ui() {
    while (1) {
        sem_wait(&state_read);

        clearScreen();
        draw_panel();
        printf("\nFloor 1 - Input delivery floor: ");
        fflush(stdout); // print immediately
    }
}

void* listen_thread() {
    MsgBuffer rcv_message;
    while (1) {
        memset(&rcv_message, 0, sizeof(rcv_message));
        msgrcv(msg_id, &rcv_message, MSG_SIZE, floor_level + 5, 0);

        if (strcmp(rcv_message.msg_text, "arrival 1") == 0) {
            lamp_state = 1;
            sem_post(&state_read);
        } else if (strcmp(rcv_message.msg_text, "arrival 0") == 0) {
            lamp_state = 0;
            sem_post(&state_read);
        } else if (strncmp(rcv_message.msg_text, "OK ", 3) == 0) {
            int i;
            sscanf(rcv_message.msg_text, "%*s%d", &i);
            delivery_pressed[i - 1] = 0;
            sem_post(&state_read);
        } else if (strcmp(rcv_message.msg_text, "error 1") == 0) {
            lamp_state = 2;
            sem_post(&state_read);
        } else if (strcmp(rcv_message.msg_text, "error 0") == 0) {
            lamp_state = 0;
            sem_post(&state_read);
        }
    }
}

int main() {
    MsgBuffer message;

    key_t key = ftok(KEY_FILE_PATH, ID);
    msg_id = msgget(key, 0666 | IPC_CREAT);
    message.msg_type = floor_level;

    sem_init(&state_read, 0, 1);

    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, &draw_ui, NULL);
    pthread_create(&tid2, NULL, &listen_thread, NULL);

    while (1) {
        int delivery_floor = getDeliveryFloorInput();

        if (delivery_pressed[delivery_floor - 1] == 0) {
            delivery_pressed[delivery_floor - 1] = 1;
            sprintf(message.msg_text, "%d", delivery_floor);
            msgsnd(msg_id, &message, strlen(message.msg_text), 0);
        }
        sem_post(&state_read);
    }

    return 0;
}
