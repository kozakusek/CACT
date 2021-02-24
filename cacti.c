#include "cacti.h"
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>

/* Actor's states */
#define DEAD 0
#define ALIVE 1
#define QUEUED 2

typedef struct actor {
    message_t *mailbox;
    int m_count;
    int m_size;
    int write_p;
    int read_p;
    role_t *role;
    int state;
    void* stateptr;
} actor_t;

typedef struct tpool {
    int *actors_q;
    int aq_count;
    int aq_size;
    int write_p;
    int read_p;
    pthread_mutex_t  mutex;
    pthread_cond_t   work; /* Waiting for work */
    pthread_cond_t   working; /* Main thread waits here for others to finish their work */
    size_t           working_count;
    size_t           thread_count;
    bool             stop; /* Received SIGINT */
    bool             join;
} tpool_t;

static actor_t *actors;
static int actors_count, actors_size, dead_count;
pthread_mutex_t mutex;

static tpool_t *tp;
static pthread_key_t act_key;

pthread_t threads[POOL_SIZE];

static void actors_q_push(actor_id_t actor) {
    pthread_mutex_lock(&(tp->mutex));
    actors[actor].state = QUEUED;

    if (tp->aq_count == tp->aq_size) {
        int *new_aq = calloc(3 * tp->aq_size, sizeof(int));
        if (new_aq == NULL) {
            pthread_mutex_unlock(&(tp->mutex));
            return;
        }

        int i = 0;
        for (int j = tp->read_p; j < tp->aq_size; j++) {
            new_aq[i] = tp->actors_q[j];
            i++;
        }
        for (int j = 0; j < tp->write_p; j++) {
            new_aq[i] = tp->actors_q[j];
            i++;
        }

        tp->read_p = 0;
        tp->write_p = i;

        free(tp->actors_q);
        tp->actors_q = new_aq;
        tp->aq_size *= 3;
    }

    tp->actors_q[tp->write_p] = actor;
    tp->write_p = (tp->write_p + 1) % tp->aq_size;
    tp->aq_count++;

    pthread_cond_signal(&(tp->work));
    pthread_mutex_unlock(&(tp->mutex));
}

static int actors_q_get(actor_id_t *actor) {
    if (tp->aq_count == 0)
        return -1;

    *actor = tp->actors_q[tp->read_p];
    tp->read_p = (tp->read_p + 1) % tp->aq_size;
    tp->aq_count--;

    return 0;
}

static int mailbox_init(actor_id_t actor) {
    if ((actors[actor].mailbox = calloc(10, sizeof(message_t))) == NULL)
        return -1;

    actors[actor].m_count = 0;
    actors[actor].m_size = 10;
    actors[actor].write_p = 0;
    actors[actor].read_p = 0;
    actors[actor].state = ALIVE;
    actors[actor].stateptr = NULL;

    return 0;
}

static int mailbox_push(actor_id_t actor, message_t message) {
    pthread_mutex_lock(&mutex);

    if (actors[actor].m_count == ACTOR_QUEUE_LIMIT) {
        pthread_mutex_unlock(&mutex);
        return -3;
    }

    if (actors[actor].m_count == actors[actor].m_size) {
        message_t *new_mailbox = calloc(3 * actors[actor].m_size, sizeof(message_t));
        if (new_mailbox == NULL) {
            pthread_mutex_unlock(&mutex);
            return -4;
        }

        int i = 0;
        for (int j = actors[actor].read_p; j < actors[actor].m_size; j++) {
            new_mailbox[i] = actors[actor].mailbox[j];
            i++;
        }
        for (int j = 0; j < actors[actor].write_p; j++) {
            new_mailbox[i] = actors[actor].mailbox[j];
            i++;
        }

        actors[actor].read_p = 0;
        actors[actor].write_p = i;

        free(actors[actor].mailbox);
        actors[actor].mailbox = new_mailbox;
        actors[actor].m_size *= 3;
    }

    actors[actor].mailbox[actors[actor].write_p] = message;
    actors[actor].write_p = (actors[actor].write_p + 1) % actors[actor].m_size;
    actors[actor].m_count++;

    if (actors[actor].state == ALIVE && actors[actor].m_count > 0)
        actors_q_push(actor);

    pthread_mutex_unlock(&mutex);

    return 0;
}

static int mailbox_get(actor_id_t actor, message_t *message) {
    pthread_mutex_lock(&mutex);
    if (actors[actor].m_count == 0) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }

    *message = actors[actor].mailbox[actors[actor].read_p];
    actors[actor].read_p = (actors[actor].read_p + 1) % actors[actor].m_size;
    actors[actor].m_count--;

    pthread_mutex_unlock(&mutex);
    return 0;
}

static actor_t *init_actors() {
    actors = calloc(10, sizeof(actor_t));
    if (actors == NULL)
        return NULL;

    if (pthread_mutex_init(&mutex, NULL)) {
        free(actors);
        return NULL;
    }

    actors_count = 0;
    actors_size = 10;
    dead_count = 0;

    return actors;
}

static void dealloc_actors() {
    if (actors == NULL)
        return;

    for (int i = 0; i < actors_count; i++) {
        free(actors[i].mailbox);
    }

    free(actors);
    actors = NULL;
    pthread_mutex_destroy(&mutex);

    actors_count = 0;
    actors_size = 0;
    dead_count = 0;
}

static void dealloc_tpool() {
    if (tp == NULL)
        return;

    free(tp->actors_q);
    pthread_mutex_destroy(&(tp->mutex));
    pthread_cond_destroy(&(tp->work));
    pthread_cond_destroy(&(tp->working));
    pthread_key_delete(act_key);

    free(tp);
    tp = NULL;
}

static void add_actor(actor_id_t parent, role_t *const role) {
    pthread_mutex_lock(&mutex);

    if (actors_count == CAST_LIMIT) {
        pthread_mutex_unlock(&mutex);
        return;
    }

    if (actors_count == actors_size) {
        actor_t *temp;
        if ((temp = realloc(actors, 3 * actors_size * sizeof(actor_t))) == NULL) {
            free(actors);
            actors = NULL;
            pthread_mutex_unlock(&mutex);
            return;
        }
        actors = temp;
        actors_size *= 3;
    }

    mailbox_init(actors_count);
    actors[actors_count].role = role;

    message_t message = {MSG_HELLO, 0, (void*)parent};
    actors_count++;

    pthread_mutex_unlock(&mutex);

    mailbox_push(actors_count - 1, message);
}

static void function(actor_id_t actor) {
    message_t message;
    mailbox_get(actor, &message);

    if (message.message_type == MSG_GODIE) {
        pthread_mutex_lock(&mutex);
        actors[actor].state = DEAD;
        dead_count++;
    } else if (message.message_type == MSG_SPAWN && !tp->stop) {
        add_actor(actor, (role_t*)message.data);
    } else {
        actors[actor].role->prompts[message.message_type](&actors[actor].stateptr,
                message.nbytes, message.data);
    }

    if (message.message_type != MSG_GODIE)
        pthread_mutex_lock(&mutex);

    actors[actor].state = actors[actor].state != DEAD ? ALIVE : DEAD;
    if (actors[actor].m_count > 0)
        actors_q_push(actor);

    pthread_mutex_unlock(&mutex);
}

static void *actor_processor(__attribute__((unused)) void *arg) {
    actor_id_t actor;
    int empty, destroy = 0;
    while (true) {
        pthread_mutex_lock(&(tp->mutex));

        while (tp->aq_count == 0 && !(tp->stop || (dead_count == actors_count && dead_count > 0)))
            pthread_cond_wait(&(tp->work), &(tp->mutex));

        if (tp->aq_count == 0 && (tp->stop || (dead_count == actors_count && dead_count > 0)))
            break;

        empty = actors_q_get(&actor);
        tp->working_count++;
        pthread_mutex_unlock(&(tp->mutex));

        if (!empty) {
            pthread_setspecific(act_key, (void*) actor);
            function(actor);
        }

        pthread_mutex_lock(&(tp->mutex));
        tp->working_count--;
        if (tp->working_count == 0 && tp->aq_count == 0 && tp->join)
            pthread_cond_signal(&(tp->working));

        pthread_mutex_unlock(&(tp->mutex));
    }

    tp->thread_count--;
    pthread_cond_signal(&(tp->working));
    if (tp->thread_count == 0 && tp->stop)
        destroy = 1;

    pthread_mutex_unlock(&(tp->mutex));

    if (destroy) {
        dealloc_tpool();
        dealloc_actors();
    }

    return NULL;
}

static tpool_t *init_tpool() {
    if ((tp = calloc(1, sizeof(*tp))) == NULL)
        return NULL;

    if ((tp->actors_q = calloc(10, sizeof(int))) == NULL) {
        free(tp);
        return NULL;
    }

    tp->aq_count = 0;
    tp->aq_size = 10;
    tp->read_p = 0;
    tp->write_p = 0;
    tp->stop = false;
    tp->join = false;
    tp->working_count = 0;
    tp->thread_count = POOL_SIZE;

    if (pthread_mutex_init(&(tp->mutex), NULL)) {
        free(tp->actors_q);
        free(tp);
        return NULL;
    }

    if (pthread_cond_init(&(tp->work), NULL)) {
        pthread_mutex_destroy(&(tp->mutex));
        free(tp->actors_q);
        free(tp);
        return NULL;
    }

    if (pthread_cond_init(&(tp->working), NULL)) {
        pthread_mutex_destroy(&(tp->mutex));
        pthread_cond_destroy(&(tp->work));
        free(tp->actors_q);
        free(tp);
        return NULL;
    }

    pthread_key_create(&act_key, NULL);
    for (size_t i = 0; i < POOL_SIZE; i++) {
        pthread_create(&threads[i], NULL, actor_processor, NULL);
    }

    return tp;
}

static void tpool_wait() {
    if (tp == NULL)
        return;

    pthread_mutex_lock(&(tp->mutex));
    tp->join = true;

    while (true) {
        pthread_cond_broadcast(&(tp->work));

        if (tp->thread_count > 0) {
            pthread_cond_wait(&(tp->working), &(tp->mutex));
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&(tp->mutex));
}

static void kill_system(__attribute__((unused)) int sig) {
    if (tp != NULL) {
        tp->stop = true;
        tp->join = true;
        pthread_cond_broadcast(&(tp->work));
    }
}

actor_id_t actor_id_self() {
    return (actor_id_t) pthread_getspecific(act_key);
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    tp = init_tpool();
    if (tp == NULL)
        return -1;

    actors = init_actors();
    if (actors == NULL) {
        dealloc_tpool();
        return -1;
    }

    struct sigaction action;
    action.sa_handler = &kill_system;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction (SIGINT, &action, 0) == -1) {
        dealloc_actors();
        dealloc_tpool();
        return -2;
    }

    /* Add actor to actors (create one) */
    if (mailbox_init(actors_count)) {
        dealloc_actors();
        dealloc_tpool();
        return -1;
    }

    *actor = actors_count;
    actors_count++;

    /* Assign role */
    actors[*actor].role = role;

    send_message(*actor, (message_t) {MSG_HELLO, 0, NULL});

    return 0;
}

void actor_system_join(actor_id_t actor) {
    /* Beacause there is only one system we check whether actor exits in it */
    if (actor >= actors_count && actor >= 0)
        return;

    /* then we just perform a join */
    tpool_wait();
    for (int i = 0; i < POOL_SIZE; i++) {
       pthread_join(threads[i], NULL);
    }

    /* clear memory */
    dealloc_tpool();
    dealloc_actors();
}

int send_message(actor_id_t actor, message_t message) {
    if (actor >= actors_count)
        return -2;
    else if (actors[actor].state == DEAD || tp->stop)
        return -1;
    else
        return mailbox_push(actor, message);
}
