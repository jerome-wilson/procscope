/*
 * MarketPulse - Real-Time Stock Monitoring and Insight Engine
 * IPC Header - Shared Memory and Semaphore definitions
 * 
 * System Programming Concepts:
 * - shmget(), shmat(), shmdt(), shmctl() - Shared memory
 * - semget(), semop(), semctl() - Semaphores
 * - Structured shared data for inter-process communication
 */

#ifndef IPC_H
#define IPC_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <time.h>

/* ============== IPC Keys ============== */

#define SHM_KEY_BASE    0x4D50      /* "MP" for MarketPulse */
#define SEM_KEY_BASE    0x4D51
#define MSG_KEY_BASE    0x4D52

/* Shared memory segment IDs */
#define SHM_STOCK_DATA  (SHM_KEY_BASE + 1)
#define SHM_CONFIG      (SHM_KEY_BASE + 2)
#define SHM_STATS       (SHM_KEY_BASE + 3)
#define SHM_MESSAGES    (SHM_KEY_BASE + 4)

/* Semaphore set IDs */
#define SEM_STOCK_DATA  (SEM_KEY_BASE + 1)
#define SEM_CONFIG      (SEM_KEY_BASE + 2)
#define SEM_MESSAGES    (SEM_KEY_BASE + 3)

/* ============== Constants ============== */

#define MAX_SHARED_STOCKS   50
#define MAX_MESSAGES        100
#define MAX_MSG_LENGTH      256
#define MAX_WORKERS         20

/* Semaphore indices within a set */
#define SEM_MUTEX       0       /* Mutual exclusion */
#define SEM_READERS     1       /* Reader count */
#define SEM_WRITERS     2       /* Writer access */
#define NUM_SEMS        3

/* ============== Shared Data Structures ============== */

/* Stock data in shared memory */
typedef struct {
    char symbol[16];
    char name[64];
    double current_price;
    double previous_close;
    double change;
    double change_percent;
    double high;
    double low;
    double open_price;
    time_t last_update;
    int valid;
    int worker_pid;             /* PID of worker handling this stock */
} SharedStockData;

/* Worker process status */
typedef struct {
    pid_t pid;
    int stock_index;            /* Index in stock array */
    char symbol[16];
    int status;                 /* 0=stopped, 1=running, 2=error */
    time_t start_time;
    time_t last_heartbeat;
    int request_count;
    int error_count;
} WorkerStatus;

/* System statistics in shared memory */
typedef struct {
    time_t start_time;
    int total_requests;
    int successful_requests;
    int failed_requests;
    int active_workers;
    int total_alerts_triggered;
    double avg_response_time_ms;
    int rate_limit_hits;
} SystemStats;

/* Message types for inter-process communication */
typedef enum {
    MSG_NONE = 0,
    MSG_STOCK_UPDATE,           /* Stock data updated */
    MSG_ALERT_TRIGGER,          /* Alert threshold crossed */
    MSG_WORKER_START,           /* Worker started */
    MSG_WORKER_STOP,            /* Worker stopped */
    MSG_WORKER_ERROR,           /* Worker encountered error */
    MSG_CONFIG_RELOAD,          /* Configuration reloaded */
    MSG_SHUTDOWN,               /* System shutdown */
    MSG_AI_INSIGHT,             /* AI generated insight */
    MSG_RATE_LIMIT              /* Rate limit warning */
} MessageType;

/* Message structure for message bus */
typedef struct {
    MessageType type;
    pid_t sender_pid;
    time_t timestamp;
    char symbol[16];
    double value;
    char text[MAX_MSG_LENGTH];
} IPCMessage;

/* Message queue in shared memory */
typedef struct {
    IPCMessage messages[MAX_MESSAGES];
    int head;                   /* Read position */
    int tail;                   /* Write position */
    int count;                  /* Number of messages */
} MessageQueue;

/* Main shared memory structure */
typedef struct {
    /* Header */
    int magic;                  /* Magic number for validation */
    int version;
    pid_t master_pid;
    time_t created_at;
    
    /* Stock data array */
    SharedStockData stocks[MAX_SHARED_STOCKS];
    int stock_count;
    
    /* Worker status array */
    WorkerStatus workers[MAX_WORKERS];
    int worker_count;
    
    /* System statistics */
    SystemStats stats;
    
    /* Message queue */
    MessageQueue msg_queue;
    
    /* Flags */
    volatile int shutdown_flag;
    volatile int reload_config_flag;
    volatile int debug_mode;
} SharedMemory;

/* ============== Semaphore Operations ============== */

/* Union for semctl operations - only define if not already defined */
#if !defined(__APPLE__) && !defined(_SEM_SEMUN_UNDEFINED)
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};
#endif

/* ============== Function Declarations ============== */

/* Shared Memory Functions */
int shm_create(key_t key, size_t size);
int ipc_shm_open(key_t key);
void *shm_attach(int shmid);
int shm_detach(void *addr);
int shm_destroy(int shmid);

/* High-level shared memory management */
SharedMemory *shm_init_master(void);
SharedMemory *shm_attach_worker(void);
void shm_cleanup(SharedMemory *shm);

/* Semaphore Functions */
int sem_create(key_t key, int num_sems);
int sem_open(key_t key);
int sem_destroy(int semid);

/* Semaphore Operations */
int sem_wait(int semid, int sem_num);
int sem_signal(int semid, int sem_num);
int sem_trywait(int semid, int sem_num);

/* Reader-Writer Lock using semaphores */
int rwlock_read_lock(int semid);
int rwlock_read_unlock(int semid);
int rwlock_write_lock(int semid);
int rwlock_write_unlock(int semid);

/* Stock Data Operations (thread-safe) */
int shm_update_stock(SharedMemory *shm, int semid, const SharedStockData *stock);
int shm_get_stock(SharedMemory *shm, int semid, const char *symbol, SharedStockData *stock);
int shm_add_stock(SharedMemory *shm, int semid, const char *symbol);
int shm_remove_stock(SharedMemory *shm, int semid, const char *symbol);
int shm_find_stock_index(SharedMemory *shm, const char *symbol);

/* Worker Status Operations */
int shm_register_worker(SharedMemory *shm, int semid, pid_t pid, const char *symbol);
int shm_unregister_worker(SharedMemory *shm, int semid, pid_t pid);
int shm_update_worker_heartbeat(SharedMemory *shm, int semid, pid_t pid);
int shm_get_dead_workers(SharedMemory *shm, int semid, pid_t *dead_pids, int max_count);

/* Message Queue Operations */
int msg_send(SharedMemory *shm, int semid, const IPCMessage *msg);
int msg_receive(SharedMemory *shm, int semid, IPCMessage *msg);
int msg_peek(SharedMemory *shm, int semid, IPCMessage *msg);
int msg_count(SharedMemory *shm, int semid);
void msg_clear(SharedMemory *shm, int semid);

/* Statistics Operations */
void stats_increment_requests(SharedMemory *shm, int semid, int success);
void stats_update_response_time(SharedMemory *shm, int semid, double time_ms);
void stats_increment_alerts(SharedMemory *shm, int semid);

/* Utility Functions */
void shm_print_status(SharedMemory *shm);
int shm_validate(SharedMemory *shm);

/* Global accessors */
int get_global_semid(void);
SharedMemory *get_global_shm(void);

/* ============== Magic Numbers ============== */

#define SHM_MAGIC       0x4D505348  /* "MPSH" */
#define SHM_VERSION     1

#endif /* IPC_H */