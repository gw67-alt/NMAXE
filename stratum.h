#ifndef STRATUM_H
#define STRATUM_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <deque>
#include <map>
#include <stdint.h>
#include "pool.h"

#define DEFAULT_POOL_DIFFICULTY 1.0
#define HELLO_POOL_INTERVAL_MS 30000
#define POOL_INACTIVITY_TIME_MS 60000

// Stratum method types
typedef enum {
    STRATUM_DOWN_PARSE_ERROR = -1,
    STRATUM_DOWN_NOTIFY = 0,
    STRATUM_DOWN_SET_DIFFICULTY = 1,
    STRATUM_DOWN_SET_VERSION_MASK = 2,
    STRATUM_DOWN_SET_EXTRANONCE = 3,
    STRATUM_DOWN_SUCCESS = 4,
    STRATUM_DOWN_ERROR = 5,
    STRATUM_DOWN_UNKNOWN = 6
} stratum_method_type_t;

// Structures
struct nonce_range_t {
    uint32_t worker_id;
    uint32_t start;
    uint32_t end;
    uint32_t current;
};

struct pool_job_data_t {
    String id;
    String prevhash;
    String coinb1;
    String coinb2;
    JsonArray merkle_branch;
    String version;
    String nbits;
    String ntime;
    bool clean_jobs;
    unsigned long stamp;
    
    pool_job_data_t() {
        clean_jobs = false;
        stamp = millis();
    }
};

struct stratum_method_data {
    int32_t id;
    stratum_method_type_t type;
    String method;
    String raw;
};

struct stratum_rsp {
    String method;
    bool status;
    unsigned long stamp;
};

struct stratum_subscription_info {
    String extranonce1;
    String extranonce2;
    int extranonce2_size;
};

struct stratum_info_t {
    String user;
    String pwd;
};


class StratumClass {
public:
    StratumClass();
    ~StratumClass();
    
    // Pool management
    PoolClass* pool = nullptr;
    
    // Semaphores for thread synchronization
    SemaphoreHandle_t new_job_xsem = nullptr;
    SemaphoreHandle_t clear_job_xsem = nullptr;
    
    // Nonce range management
    void configure_nonce_ranges(uint32_t num_workers);
    uint32_t get_next_nonce(uint32_t worker_id);
    bool reset_nonce_range(uint32_t worker_id);
    void reset_all_nonce_ranges();
    uint32_t get_nonce_range_progress(uint32_t worker_id);
    bool submit_with_worker(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t worker_id, uint32_t version);
    
    // Pool communication
    bool subscribe();
    bool authorize();
    bool suggest_difficulty();
    bool config_version_rolling();
    bool submit(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version);
    bool hello_pool(uint32_t hello_interval, uint32_t lost_max_time);
    
    // Message handling
    stratum_method_data listen_methods();
    bool set_msg_rsp_map(uint32_t id, bool status);
    bool del_msg_rsp_map(uint32_t id);
    stratum_rsp get_method_rsp_by_id(uint32_t id);
    
    // Job cache management
    size_t push_job_cache(pool_job_data_t job);
    size_t get_job_cache_size();
    size_t clear_job_cache();
    pool_job_data_t pop_job_cache();
    
    // Subscription management
    String get_sub_extranonce1();
    String get_sub_extranonce2();
    bool clear_sub_extranonce2();
    void set_sub_extranonce1(String extranonce1);
    void set_sub_extranonce2_size(int size);
    
    // Status getters/setters
    bool is_subscribed() { return _is_subscribed; }
    bool is_authorized() { return _is_authorized; }
    void set_authorize(bool auth) { _is_authorized = auth; }
    
    float get_pool_difficulty() { return _pool_difficulty; }
    void set_pool_difficulty(float diff) { _pool_difficulty = diff; }
    
    uint32_t get_version_mask() { return _vr_mask; }
    void set_version_mask(uint32_t mask) { _vr_mask = mask; }
    
    bool is_submit_timeout();
    
    // Reset methods
    void reset();
    void reset(pool_info_t pConfig, stratum_info_t sConfig);
    
private:
    // Core state
    DynamicJsonDocument _rsp_json{1024 * 4};
    String _rsp_str;
    stratum_info_t _stratum_info;
    stratum_subscription_info _sub_info;
    
    // Status flags
    bool _is_subscribed = false;
    bool _is_authorized = false;
    bool _suggest_diff_support = true;
    
    // Pool settings
    float _pool_difficulty = DEFAULT_POOL_DIFFICULTY;
    uint32_t _vr_mask = 0xffffffff;
    
    // Message handling
    uint32_t _gid = 1;
    std::map<uint32_t, stratum_rsp> _msg_rsp_map;
    size_t _max_rsp_id_cache = 100;
    
    // Job cache
    std::deque<pool_job_data_t> _pool_job_cache;
    size_t _pool_job_cache_size = 5;
    
    // Nonce range management
    std::vector<nonce_range_t> _nonce_ranges;
    uint32_t _total_workers = 0;
    
    // Internal methods
    uint32_t _get_msg_id();
    bool _parse_rsp();
    bool _clear_rsp_id_cache();
};

// Thread function
void stratum_thread_entry(void *args);

#endif

