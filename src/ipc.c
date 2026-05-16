/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * IPC Implementation - Shared Memory and Semaphore operations
 * 
 * System Programming Concepts Demonstrated:
 * - shmget() - Create/access shared memory segment
 * - shmat()  - Attach shared memory to process address space
 * - shmdt()  - Detach shared memory
 * - shmctl() - Control operations on shared memory
 * - semget() - Create/access semaphore set
 * - semop()  - Perform semaphore operations (wait/signal)
 * - semctl() - Control operations on semaphores
 */

#include "procscope.h"
#include "ipc.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Global IPC identifiers */
static int g_shmid = -1;
static int g_semid = -1;
static SharedMemory *g_shm = NULL;

/* ============== Shared Memory Functions ============== */

/*
 * Create a new shared memory segment
 * 
 * Parameters:
 *   key  - IPC key for the segment
 *   size - Size of the segment in bytes
 * 
 * Returns: Shared memory ID on success, -1 on failure
 */
int shm_create(key_t key, size_t size) {
    int shmid;
    
    /* Create shared memory segment with read/write permissions */
    /* IPC_CREAT - Create if doesn't exist */
    /* IPC_EXCL  - Fail if already exists (for exclusive creation) */
    /* 0666     - Read/write for owner, group, others */
    shmid = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);
    
    if (shmid == -1) {
        if (errno == EEXIST) {
            /* Segment already exists, try to open it */
            shmid = shmget(key, size, 0666);
            if (shmid == -1) {
                perror("shmget (open existing)");
                return -1;
            }
            /* Remove old segment and create new */
            if (shmctl(shmid, IPC_RMID, NULL) == -1) {
                perror("shmctl IPC_RMID");
                return -1;
            }
            /* Try creating again */
            shmid = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);
            if (shmid == -1) {
                perror("shmget (recreate)");
                return -1;
            }
        } else {
            perror("shmget");
            return -1;
        }
    }
    
    return shmid;
}

/*
 * Open an existing shared memory segment
 */
int ipc_shm_open(key_t key) {
    int shmid;

    shmid = shmget(key, 0, 0666);
    if (shmid == -1) {
        return -1;
    }

    return shmid;
}

/*
 * Attach shared memory segment to process address space
 * 
 * Returns: Pointer to shared memory, NULL on failure
 */
void *shm_attach(int shmid) {
    void *addr;
    
    addr = shmat(shmid, NULL, 0);
    if (addr == (void *)-1) {
        perror("shmat");
        return NULL;
    }
    
    return addr;
}

/*
 * Detach shared memory from process address space
 */
int shm_detach(void *addr) {
    if (shmdt(addr) == -1) {
        perror("shmdt");
        return -1;
    }
    return 0;
}

/*
 * Destroy shared memory segment
 */
int shm_destroy(int shmid) {
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl IPC_RMID");
        return -1;
    }
    return 0;
}

/* ============== Semaphore Functions ============== */

/*
 * Create a new semaphore set
 * 
 * Parameters:
 *   key      - IPC key for the semaphore set
 *   num_sems - Number of semaphores in the set
 * 
 * Returns: Semaphore ID on success, -1 on failure
 */
int sem_create(key_t key, int num_sems) {
    int semid;
    union semun arg;
    unsigned short *values;
    int i;
    
    /* Create semaphore set */
    semid = semget(key, num_sems, IPC_CREAT | IPC_EXCL | 0666);
    
    if (semid == -1) {
        if (errno == EEXIST) {
            /* Already exists, remove and recreate */
            semid = semget(key, num_sems, 0666);
            if (semid != -1) {
                semctl(semid, 0, IPC_RMID);
            }
            semid = semget(key, num_sems, IPC_CREAT | IPC_EXCL | 0666);
            if (semid == -1) {
                perror("semget (recreate)");
                return -1;
            }
        } else {
            perror("semget");
            return -1;
        }
    }
    
    /* Initialize semaphore values */
    values = malloc(num_sems * sizeof(unsigned short));
    if (values == NULL) {
        perror("malloc");
        return -1;
    }
    
    /* Set initial values: mutex=1, others=0 */
    for (i = 0; i < num_sems; i++) {
        values[i] = (i == SEM_MUTEX) ? 1 : 0;
    }
    
    arg.array = values;
    if (semctl(semid, 0, SETALL, arg) == -1) {
        perror("semctl SETALL");
        free(values);
        return -1;
    }
    
    free(values);
    return semid;
}

/*
 * Open an existing semaphore set
 */
int sem_open(key_t key) {
    int semid;
    
    semid = semget(key, 0, 0666);
    if (semid == -1) {
        perror("semget (open)");
        return -1;
    }
    
    return semid;
}

/*
 * Destroy semaphore set
 */
int sem_destroy(int semid) {
    if (semctl(semid, 0, IPC_RMID) == -1) {
        perror("semctl IPC_RMID");
        return -1;
    }
    return 0;
}

/*
 * Wait (P operation / decrement) on a semaphore
 * Blocks if semaphore value is 0
 */
int sem_wait(int semid, int sem_num) {
    struct sembuf op;
    
    op.sem_num = sem_num;
    op.sem_op = -1;         /* Decrement by 1 */
    op.sem_flg = SEM_UNDO;  /* Undo on process exit */
    
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) {
            continue;  /* Interrupted by signal, retry */
        }
        perror("semop wait");
        return -1;
    }
    
    return 0;
}

/*
 * Signal (V operation / increment) on a semaphore
 */
int sem_signal(int semid, int sem_num) {
    struct sembuf op;
    
    op.sem_num = sem_num;
    op.sem_op = 1;          /* Increment by 1 */
    op.sem_flg = SEM_UNDO;
    
    if (semop(semid, &op, 1) == -1) {
        perror("semop signal");
        return -1;
    }
    
    return 0;
}

/*
 * Try to wait on a semaphore (non-blocking)
 * Returns 0 if acquired, -1 if would block
 */
int sem_trywait(int semid, int sem_num) {
    struct sembuf op;
    
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT | SEM_UNDO;
    
    if (semop(semid, &op, 1) == -1) {
        if (errno == EAGAIN) {
            return -1;  /* Would block */
        }
        perror("semop trywait");
        return -1;
    }
    
    return 0;
}

/* ============== Reader-Writer Lock ============== */

/*
 * Acquire read lock (multiple readers allowed)
 */
int rwlock_read_lock(int semid) {
    /* Wait for mutex */
    if (sem_wait(semid, SEM_MUTEX) == -1) return -1;
    
    /* Increment reader count */
    if (sem_signal(semid, SEM_READERS) == -1) {
        sem_signal(semid, SEM_MUTEX);
        return -1;
    }
    
    /* If first reader, lock writers */
    /* Check reader count using semctl */
    int readers = semctl(semid, SEM_READERS, GETVAL);
    if (readers == 1) {
        if (sem_wait(semid, SEM_WRITERS) == -1) {
            sem_wait(semid, SEM_READERS);  /* Decrement reader count */
            sem_signal(semid, SEM_MUTEX);
            return -1;
        }
    }
    
    /* Release mutex */
    sem_signal(semid, SEM_MUTEX);
    return 0;
}

/*
 * Release read lock
 */
int rwlock_read_unlock(int semid) {
    /* Wait for mutex */
    if (sem_wait(semid, SEM_MUTEX) == -1) return -1;
    
    /* Decrement reader count */
    if (sem_wait(semid, SEM_READERS) == -1) {
        sem_signal(semid, SEM_MUTEX);
        return -1;
    }
    
    /* If last reader, unlock writers */
    int readers = semctl(semid, SEM_READERS, GETVAL);
    if (readers == 0) {
        sem_signal(semid, SEM_WRITERS);
    }
    
    /* Release mutex */
    sem_signal(semid, SEM_MUTEX);
    return 0;
}

/*
 * Acquire write lock (exclusive access)
 */
int rwlock_write_lock(int semid) {
    /* Wait for writer semaphore (exclusive) */
    return sem_wait(semid, SEM_WRITERS);
}

/*
 * Release write lock
 */
int rwlock_write_unlock(int semid) {
    return sem_signal(semid, SEM_WRITERS);
}

/* ============== High-Level Shared Memory Management ============== */

/*
 * Initialize shared memory as master process
 * Creates and initializes all shared memory structures
 */
SharedMemory *shm_init_master(void) {
    SharedMemory *shm;
    
    /* Create shared memory segment */
    g_shmid = shm_create(SHM_STOCK_DATA, sizeof(SharedMemory));
    if (g_shmid == -1) {
        fprintf(stderr, "Failed to create shared memory\n");
        return NULL;
    }
    
    /* Attach shared memory */
    shm = (SharedMemory *)shm_attach(g_shmid);
    if (shm == NULL) {
        fprintf(stderr, "Failed to attach shared memory\n");
        shm_destroy(g_shmid);
        return NULL;
    }
    
    /* Create semaphore set */
    g_semid = sem_create(SEM_STOCK_DATA, NUM_SEMS);
    if (g_semid == -1) {
        fprintf(stderr, "Failed to create semaphores\n");
        shm_detach(shm);
        shm_destroy(g_shmid);
        return NULL;
    }
    
    /* Initialize shared memory structure */
    memset(shm, 0, sizeof(SharedMemory));
    shm->magic = SHM_MAGIC;
    shm->version = SHM_VERSION;
    shm->master_pid = getpid();
    shm->created_at = time(NULL);
    shm->stock_count = 0;
    shm->worker_count = 0;
    shm->shutdown_flag = 0;
    shm->reload_config_flag = 0;
    shm->debug_mode = 0;
    
    /* Initialize statistics */
    shm->stats.start_time = time(NULL);
    shm->stats.total_requests = 0;
    shm->stats.successful_requests = 0;
    shm->stats.failed_requests = 0;
    shm->stats.active_workers = 0;
    shm->stats.total_alerts_triggered = 0;
    shm->stats.avg_response_time_ms = 0;
    shm->stats.rate_limit_hits = 0;
    
    /* Initialize message queue */
    shm->msg_queue.head = 0;
    shm->msg_queue.tail = 0;
    shm->msg_queue.count = 0;
    
    /* Initialize writer semaphore to 1 (unlocked) */
    union semun arg;
    arg.val = 1;
    semctl(g_semid, SEM_WRITERS, SETVAL, arg);
    
    g_shm = shm;
    return shm;
}

/*
 * Attach to existing shared memory as worker process
 */
SharedMemory *shm_attach_worker(void) {
    SharedMemory *shm;
    
    /* Open existing shared memory */
    g_shmid = ipc_shm_open(SHM_STOCK_DATA);
    if (g_shmid == -1) {
        return NULL;
    }
    
    /* Attach shared memory */
    shm = (SharedMemory *)shm_attach(g_shmid);
    if (shm == NULL) {
        fprintf(stderr, "Failed to attach shared memory\n");
        return NULL;
    }
    
    /* Validate shared memory */
    if (!shm_validate(shm)) {
        fprintf(stderr, "Invalid shared memory structure\n");
        shm_detach(shm);
        return NULL;
    }
    
    /* Open existing semaphore set */
    g_semid = sem_open(SEM_STOCK_DATA);
    if (g_semid == -1) {
        fprintf(stderr, "Failed to open semaphores\n");
        shm_detach(shm);
        return NULL;
    }
    
    g_shm = shm;
    return shm;
}

/*
 * Cleanup shared memory (master only)
 */
void shm_cleanup(SharedMemory *shm) {
    if (shm != NULL) {
        shm_detach(shm);
    }
    
    if (g_shmid != -1) {
        shm_destroy(g_shmid);
        g_shmid = -1;
    }
    
    if (g_semid != -1) {
        sem_destroy(g_semid);
        g_semid = -1;
    }
    
    g_shm = NULL;
}

/*
 * Validate shared memory structure
 */
int shm_validate(SharedMemory *shm) {
    if (shm == NULL) return 0;
    if (shm->magic != SHM_MAGIC) return 0;
    if (shm->version != SHM_VERSION) return 0;
    return 1;
}

/* ============== Stock Data Operations ============== */

/*
 * Find stock index by symbol
 * Returns index or -1 if not found
 */
int shm_find_stock_index(SharedMemory *shm, const char *symbol) {
    int i;
    for (i = 0; i < shm->stock_count; i++) {
        if (strcmp(shm->stocks[i].symbol, symbol) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Add a new stock to shared memory
 * Thread-safe with semaphore protection
 */
int shm_add_stock(SharedMemory *shm, int semid, const char *symbol) {
    int index;
    
    /* Acquire write lock */
    if (rwlock_write_lock(semid) == -1) return -1;
    
    /* Check if already exists */
    index = shm_find_stock_index(shm, symbol);
    if (index >= 0) {
        rwlock_write_unlock(semid);
        return index;  /* Already exists */
    }
    
    /* Check capacity */
    if (shm->stock_count >= MAX_SHARED_STOCKS) {
        rwlock_write_unlock(semid);
        return -1;  /* Full */
    }
    
    /* Add new stock */
    index = shm->stock_count;
    memset(&shm->stocks[index], 0, sizeof(SharedStockData));
    strncpy(shm->stocks[index].symbol, symbol, sizeof(shm->stocks[index].symbol) - 1);
    shm->stocks[index].valid = 0;
    shm->stocks[index].worker_pid = 0;
    shm->stock_count++;
    
    /* Release write lock */
    rwlock_write_unlock(semid);
    
    return index;
}

/*
 * Update stock data in shared memory
 * Thread-safe with semaphore protection
 */
int shm_update_stock(SharedMemory *shm, int semid, const SharedStockData *stock) {
    int index;
    
    /* Acquire write lock */
    if (rwlock_write_lock(semid) == -1) return -1;
    
    /* Find stock */
    index = shm_find_stock_index(shm, stock->symbol);
    if (index < 0) {
        /* Add new stock if not found */
        if (shm->stock_count >= MAX_SHARED_STOCKS) {
            rwlock_write_unlock(semid);
            return -1;
        }
        index = shm->stock_count++;
    }
    
    /* Update stock data */
    memcpy(&shm->stocks[index], stock, sizeof(SharedStockData));
    shm->stocks[index].last_update = time(NULL);
    
    /* Release write lock */
    rwlock_write_unlock(semid);
    
    return 0;
}

/*
 * Get stock data from shared memory
 * Thread-safe with semaphore protection
 */
int shm_get_stock(SharedMemory *shm, int semid, const char *symbol, SharedStockData *stock) {
    int index;
    
    /* Acquire read lock */
    if (rwlock_read_lock(semid) == -1) return -1;
    
    /* Find stock */
    index = shm_find_stock_index(shm, symbol);
    if (index < 0) {
        rwlock_read_unlock(semid);
        return -1;
    }
    
    /* Copy stock data */
    memcpy(stock, &shm->stocks[index], sizeof(SharedStockData));
    
    /* Release read lock */
    rwlock_read_unlock(semid);
    
    return 0;
}

/*
 * Remove stock from shared memory
 */
int shm_remove_stock(SharedMemory *shm, int semid, const char *symbol) {
    int index, i;
    
    /* Acquire write lock */
    if (rwlock_write_lock(semid) == -1) return -1;
    
    /* Find stock */
    index = shm_find_stock_index(shm, symbol);
    if (index < 0) {
        rwlock_write_unlock(semid);
        return -1;
    }
    
    /* Shift remaining stocks */
    for (i = index; i < shm->stock_count - 1; i++) {
        memcpy(&shm->stocks[i], &shm->stocks[i + 1], sizeof(SharedStockData));
    }
    shm->stock_count--;
    
    /* Release write lock */
    rwlock_write_unlock(semid);
    
    return 0;
}

/* ============== Worker Status Operations ============== */

/*
 * Register a worker process
 */
int shm_register_worker(SharedMemory *shm, int semid, pid_t pid, const char *symbol) {
    int i;
    
    if (rwlock_write_lock(semid) == -1) return -1;
    
    /* Find empty slot or existing entry */
    for (i = 0; i < MAX_WORKERS; i++) {
        if (shm->workers[i].pid == 0 || shm->workers[i].pid == pid) {
            shm->workers[i].pid = pid;
            strncpy(shm->workers[i].symbol, symbol, sizeof(shm->workers[i].symbol) - 1);
            shm->workers[i].status = 1;  /* Running */
            shm->workers[i].start_time = time(NULL);
            shm->workers[i].last_heartbeat = time(NULL);
            shm->workers[i].request_count = 0;
            shm->workers[i].error_count = 0;
            
            if (i >= shm->worker_count) {
                shm->worker_count = i + 1;
            }
            shm->stats.active_workers++;
            
            rwlock_write_unlock(semid);
            return i;
        }
    }
    
    rwlock_write_unlock(semid);
    return -1;  /* No slots available */
}

/*
 * Unregister a worker process
 */
int shm_unregister_worker(SharedMemory *shm, int semid, pid_t pid) {
    int i;
    
    if (rwlock_write_lock(semid) == -1) return -1;
    
    for (i = 0; i < shm->worker_count; i++) {
        if (shm->workers[i].pid == pid) {
            shm->workers[i].pid = 0;
            shm->workers[i].status = 0;
            shm->stats.active_workers--;
            rwlock_write_unlock(semid);
            return 0;
        }
    }
    
    rwlock_write_unlock(semid);
    return -1;
}

/*
 * Update worker heartbeat
 */
int shm_update_worker_heartbeat(SharedMemory *shm, int semid, pid_t pid) {
    int i;
    
    if (rwlock_write_lock(semid) == -1) return -1;
    
    for (i = 0; i < shm->worker_count; i++) {
        if (shm->workers[i].pid == pid) {
            shm->workers[i].last_heartbeat = time(NULL);
            rwlock_write_unlock(semid);
            return 0;
        }
    }
    
    rwlock_write_unlock(semid);
    return -1;
}

/*
 * Get list of dead workers (no heartbeat for > 30 seconds)
 */
int shm_get_dead_workers(SharedMemory *shm, int semid, pid_t *dead_pids, int max_count) {
    int i, count = 0;
    time_t now = time(NULL);
    
    if (rwlock_read_lock(semid) == -1) return -1;
    
    for (i = 0; i < shm->worker_count && count < max_count; i++) {
        if (shm->workers[i].pid > 0 && shm->workers[i].status == 1) {
            if (now - shm->workers[i].last_heartbeat > 30) {
                dead_pids[count++] = shm->workers[i].pid;
            }
        }
    }
    
    rwlock_read_unlock(semid);
    return count;
}

/* ============== Message Queue Operations ============== */

/*
 * Send a message to the queue
 */
int msg_send(SharedMemory *shm, int semid, const IPCMessage *msg) {
    if (rwlock_write_lock(semid) == -1) return -1;
    
    if (shm->msg_queue.count >= MAX_MESSAGES) {
        /* Queue full, drop oldest message */
        shm->msg_queue.head = (shm->msg_queue.head + 1) % MAX_MESSAGES;
        shm->msg_queue.count--;
    }
    
    /* Add message to tail */
    memcpy(&shm->msg_queue.messages[shm->msg_queue.tail], msg, sizeof(IPCMessage));
    shm->msg_queue.tail = (shm->msg_queue.tail + 1) % MAX_MESSAGES;
    shm->msg_queue.count++;
    
    rwlock_write_unlock(semid);
    return 0;
}

/*
 * Receive a message from the queue (removes it)
 */
int msg_receive(SharedMemory *shm, int semid, IPCMessage *msg) {
    if (rwlock_write_lock(semid) == -1) return -1;
    
    if (shm->msg_queue.count == 0) {
        rwlock_write_unlock(semid);
        return -1;  /* Queue empty */
    }
    
    /* Get message from head */
    memcpy(msg, &shm->msg_queue.messages[shm->msg_queue.head], sizeof(IPCMessage));
    shm->msg_queue.head = (shm->msg_queue.head + 1) % MAX_MESSAGES;
    shm->msg_queue.count--;
    
    rwlock_write_unlock(semid);
    return 0;
}

/*
 * Peek at next message without removing
 */
int msg_peek(SharedMemory *shm, int semid, IPCMessage *msg) {
    if (rwlock_read_lock(semid) == -1) return -1;
    
    if (shm->msg_queue.count == 0) {
        rwlock_read_unlock(semid);
        return -1;
    }
    
    memcpy(msg, &shm->msg_queue.messages[shm->msg_queue.head], sizeof(IPCMessage));
    
    rwlock_read_unlock(semid);
    return 0;
}

/*
 * Get message count
 */
int msg_count(SharedMemory *shm, int semid) {
    int count;
    
    if (rwlock_read_lock(semid) == -1) return -1;
    count = shm->msg_queue.count;
    rwlock_read_unlock(semid);
    
    return count;
}

/*
 * Clear all messages
 */
void msg_clear(SharedMemory *shm, int semid) {
    if (rwlock_write_lock(semid) == -1) return;
    
    shm->msg_queue.head = 0;
    shm->msg_queue.tail = 0;
    shm->msg_queue.count = 0;
    
    rwlock_write_unlock(semid);
}

/* ============== Statistics Operations ============== */

void stats_increment_requests(SharedMemory *shm, int semid, int success) {
    if (rwlock_write_lock(semid) == -1) return;
    
    shm->stats.total_requests++;
    if (success) {
        shm->stats.successful_requests++;
    } else {
        shm->stats.failed_requests++;
    }
    
    rwlock_write_unlock(semid);
}

void stats_update_response_time(SharedMemory *shm, int semid, double time_ms) {
    if (rwlock_write_lock(semid) == -1) return;
    
    /* Running average */
    double n = shm->stats.total_requests;
    if (n > 0) {
        shm->stats.avg_response_time_ms = 
            (shm->stats.avg_response_time_ms * (n - 1) + time_ms) / n;
    }
    
    rwlock_write_unlock(semid);
}

void stats_increment_alerts(SharedMemory *shm, int semid) {
    if (rwlock_write_lock(semid) == -1) return;
    shm->stats.total_alerts_triggered++;
    rwlock_write_unlock(semid);
}

/* ============== Utility Functions ============== */

/*
 * Print shared memory status (for debugging)
 */
void shm_print_status(SharedMemory *shm) {
    int i;
    
    printf("\n=== Shared Memory Status ===\n");
    printf("Magic: 0x%X (valid: %s)\n", shm->magic, shm->magic == SHM_MAGIC ? "yes" : "no");
    printf("Version: %d\n", shm->version);
    printf("Master PID: %d\n", shm->master_pid);
    printf("Created: %s", ctime(&shm->created_at));
    printf("Stocks: %d\n", shm->stock_count);
    printf("Workers: %d (active: %d)\n", shm->worker_count, shm->stats.active_workers);
    printf("Messages in queue: %d\n", shm->msg_queue.count);
    printf("Shutdown flag: %d\n", shm->shutdown_flag);
    
    printf("\n--- Stocks ---\n");
    for (i = 0; i < shm->stock_count; i++) {
        printf("  [%d] %s: $%.2f (valid: %d, worker: %d)\n",
               i, shm->stocks[i].symbol, shm->stocks[i].current_price,
               shm->stocks[i].valid, shm->stocks[i].worker_pid);
    }
    
    printf("\n--- Statistics ---\n");
    printf("  Total requests: %d\n", shm->stats.total_requests);
    printf("  Successful: %d\n", shm->stats.successful_requests);
    printf("  Failed: %d\n", shm->stats.failed_requests);
    printf("  Avg response time: %.2f ms\n", shm->stats.avg_response_time_ms);
    printf("  Alerts triggered: %d\n", shm->stats.total_alerts_triggered);
    printf("============================\n\n");
}

/* Get global semaphore ID */
int get_global_semid(void) {
    return g_semid;
}

/* Get global shared memory pointer */
SharedMemory *get_global_shm(void) {
    return g_shm;
}