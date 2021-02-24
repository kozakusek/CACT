#include "cacti.h"
#include <stdio.h>
#include <stdlib.h>

void hello(void **stateptr, size_t nbytes, void *data);
void factorial(void **stateptr, size_t nbytes, void *data);
void get_info(void **stateptr, size_t nbytes, void *data);

act_t funs[3] = {
        &hello,
        &get_info,
        &factorial,
};

#define MSG_INFO 1
#define MSG_FACT 2

role_t role = {
        .nprompts = 3,
        .prompts = funs
};

actor_id_t first_actor;

typedef struct factor {
    unsigned long long exp;
    unsigned long long arg;
    unsigned long long val;
} factorial_t;


void factorial(void **stateptr, __attribute__((unused)) size_t nbytes, void *data) {
    factorial_t *ft = (factorial_t*) data;

    if (ft->arg == ft->exp) {
        printf("%llu\n", ft->val);
        send_message(actor_id_self(), (message_t) {MSG_GODIE, 0, NULL});
    } else {
        *stateptr = malloc(sizeof(*ft));
        ((factorial_t*)(*stateptr))->exp = ft->exp;
        ((factorial_t*)(*stateptr))->arg = ft->arg + 1;
        ((factorial_t*)(*stateptr))->val = ft->val * (ft->arg + 1);
        send_message(actor_id_self(), (message_t) {MSG_SPAWN, sizeof(role), &role});
    }

    if (ft->arg > 0) {
        free(ft);
    }
}

void get_info(void **stateptr, __attribute__((unused)) size_t nbytes, void *data) {
    factorial_t *ft = (factorial_t*) *stateptr;

    send_message((actor_id_t) data, (message_t) {MSG_FACT, sizeof(*ft), (void*) ft});

    send_message(actor_id_self(), (message_t) {MSG_GODIE, 0, NULL});
}

void hello(__attribute__((unused)) void **stateptr, __attribute__((unused)) size_t nbytes, void *data) {
    if (actor_id_self() == first_actor) {
        return;
    }

    send_message((actor_id_t) data, (message_t) {MSG_INFO, sizeof(actor_id_t), (void*) actor_id_self()});
}

int main() {

    int n;
    scanf("%d", &n);

    factorial_t ft = {
            .exp = n,
            .arg = 0,
            .val = 1
    };

    message_t fm = {
            .message_type = MSG_FACT,
            .nbytes = sizeof(ft),
            .data = (void *) &ft
    };

    actor_system_create(&first_actor, &role);

    send_message(first_actor, fm);

    actor_system_join(first_actor);

	return 0;
}
