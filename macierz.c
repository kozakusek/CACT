#include "cacti.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int *matrix;
int *times;
int rows, cols;
actor_id_t first_actor;

int indx(int r, int c) {
    return r * cols + c;
}

void nap(int time) {
    usleep(time * 1000);
}

typedef struct data {
    int cur_row;
    int cur_col;
    int cur_sum;
    actor_id_t next;
} data_t;

void calculate(void **stateptr, __attribute__((unused)) size_t nbytes, void *data);
void get_info(void **stateptr, __attribute__((unused)) size_t nbytes, void *data);
void hello(__attribute__((unused)) void **stateptr, __attribute__((unused)) size_t nbytes, void *data);
void clear(void **stateptr, __attribute__((unused)) size_t nbytes, __attribute__((unused)) void *data);

act_t funs[4] = {
        &hello,
        &get_info,
        &calculate,
        &clear
};

role_t role = {
        .nprompts = 4,
        .prompts = funs
};

#define MSG_INFO 1
#define MSG_CALC 2
#define MSG_CLEAR 3

void calculate(void **stateptr, __attribute__((unused)) size_t nbytes, void *data) {
    data_t *dt = (data_t*) data;

    if (dt->cur_row == 0)
        *stateptr = calloc(1, sizeof(data_t));

    if ((((data_t*)(*stateptr))->cur_col = (dt->cur_col + 1) % cols) == 0) {
        ((data_t *) (*stateptr))->cur_row = dt->cur_row + 1;

        nap(times[indx(dt->cur_row, dt->cur_col)]);
        printf("%d\n", dt->cur_sum + matrix[indx(dt->cur_row , dt->cur_col)]);

        if (dt->cur_row + 1 < rows) {
            send_message(first_actor, (message_t) {MSG_CALC, sizeof(data_t), *stateptr});
        } else {
            send_message(first_actor, (message_t) {MSG_CLEAR, 0, NULL});
        }
    } else {
        nap(times[indx(dt->cur_row, dt->cur_col)]);
        ((data_t *) (*stateptr))->cur_row = dt->cur_row;
        ((data_t*)(*stateptr))->cur_sum = dt->cur_sum + matrix[indx(dt->cur_row, dt->cur_col)];

        if (dt->cur_row == 0) {
            send_message(actor_id_self(), (message_t) {MSG_SPAWN, sizeof(role_t), &role});
        } else {
            send_message(((data_t *) (*stateptr))->next, (message_t) {MSG_CALC, sizeof(data_t), *stateptr});
        }
    }

}

void get_info(void **stateptr, __attribute__((unused)) size_t nbytes, void *data) {
    data_t *dt = (data_t*) *stateptr;

    ((data_t*)(*stateptr))->next = (actor_id_t) data;

    send_message((actor_id_t) data, (message_t) {MSG_CALC, sizeof(*dt), (void*) dt});
}

void hello(__attribute__((unused)) void **stateptr, __attribute__((unused)) size_t nbytes, void *data) {
    if (actor_id_self() == first_actor) {
        return;
    }

    send_message((actor_id_t) data, (message_t) {MSG_INFO, sizeof(actor_id_t), (void*) actor_id_self()});
}

void clear(void **stateptr, __attribute__((unused)) size_t nbytes, __attribute__((unused)) void *data) {
    if (((data_t*)(*stateptr))->next != 0)
        send_message(((data_t*)(*stateptr))->next, (message_t) {MSG_CLEAR, 0, NULL});

    free(*stateptr);

    send_message(actor_id_self(), (message_t) {MSG_GODIE, 0, NULL});
}

int main() {
    scanf("%d", &rows);
    scanf("%d", &cols);

    if ((matrix = malloc(sizeof(int) * rows * cols)) == NULL) {
        return 1;
    }

    if ((times = malloc(sizeof(int) * rows * cols)) == NULL) {
        free(matrix);
        return 1;
    }

    for (int i = 0; i < rows * cols; i++) {
        scanf("%d", &matrix[i]);
        scanf("%d", &times[i]);
    }

    data_t data = {
            .cur_col = 0,
            .cur_row = 0,
            .cur_sum = 0
    };

    message_t fm = {
            .message_type = MSG_CALC,
            .nbytes = sizeof(data),
            .data = (void*) &data
    };

    actor_system_create(&first_actor, &role);

    send_message(first_actor, fm);

    actor_system_join(first_actor);

    free(matrix);
    free(times);
	return 0;
}
