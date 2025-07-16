#ifndef STRATUM_H_
#define STRATUM_H_
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <map>
#include <vector>
#include <deque>
#include "helper.h"
#include "pool.h"   

#define  DEFAULT_POOL_DIFFICULTY   (512)
#define  HELLO_POOL_INTERVAL_MS    (1000*30)
#define  LOST_POOL_TIMEOUT_MS      (1000*60*5)
#define  SUBMIT_TIMEOUT_MS         (1000*60*2)

typedef uint32_t stratum_msg_rsp_id_t;

typedef enum {
    STRATUM_DOWN_SUCCESS,
    STRATUM_DOWN_NOTIFY,
    STRATUM_DOWN_SET_DIFFICULTY,
    STRATUM_DOWN_SET_VERSION_MASK,
    STRATUM_DOWN_SET_EXTRANONCE,
    STRATUM_DOWN_UNKNOWN,
    STRATUM_DOWN_ERROR,
    STRATUM_DOWN_PARSE_ERROR
} stratum_method_down;

typedef struct{
    String   method;
    bool     status;
    uint32_t stamp;
}stratum_rsp;

typedef struct{
    String      user;
    String      pwd;
}stratum_info_t;

typedef struct {
    int32_t                  id;
    stratum_method_down      type;
    String                   name;
    String                   raw;
} stratum_method_data;

typedef struct {
    String id;
    String prevhash;
    String coinb1;
    String coinb2;
    String nbits;
    JsonArray merkle_branch;
    String version;
    String ntime;
    bool clean_jobs;
}pool_job_data_t;

typedef struct {
    String extranonce1;
    String extranonce2;
    int extranonce2_size;
} stratum_subscribe_info_t;

class StratumClass{
private:
// Add these to your existing StratumClass private section:
private:
    String _current_prevhash;
    uint32_t _base_nonce;
    std::vector<uint32_t> _probe_iterations;

    struct nonce_range_t {
        uint32_t start;
        uint32_t end;
        uint32_t current;
        uint32_t worker_id;
    };
    uint32_t _total_workers = 1;

    stratum_info_t                                  _stratum_info;
    bool                                            _is_subscribed;
    bool                                            _is_authorized;
    uint32_t                                        _gid;
    uint32_t                                        _get_msg_id();
    String                                          _rsp_str;
    bool                                            _parse_rsp();
    bool                                            _clear_rsp_id_cache();
    bool                                            _suggest_diff_support;
    uint32_t                                        _vr_mask;//version rolling mask
    double                                          _pool_difficulty;
    StaticJsonDocument<4096>                        _rsp_json;
    stratum_subscribe_info_t                        _sub_info;
    uint32_t                                        _max_rsp_id_cache;
    uint8_t                                         _pool_job_cache_size;
    std::deque<pool_job_data_t>                     _pool_job_cache;
    std::map<stratum_msg_rsp_id_t, stratum_rsp>     _msg_rsp_map;
public:

    // Nonce range management methods
    void configure_nonce_ranges(uint32_t num_workers);
    uint32_t get_next_nonce(uint32_t worker_id);
    bool reset_nonce_range(uint32_t worker_id);
    void reset_all_nonce_ranges();
    uint32_t get_nonce_range_progress(uint32_t worker_id);
    bool submit_with_worker(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t worker_id, uint32_t version);
    PoolClass  *pool;
    SemaphoreHandle_t new_job_xsem, clear_job_xsem;

    StratumClass(){};
    StratumClass(pool_info_t pConfig, stratum_info_t sConfig, uint8_t job_cached_max): 
     _stratum_info(sConfig), _pool_job_cache_size(job_cached_max){
        this->pool = new PoolClass(pConfig);
        this->_max_rsp_id_cache = 20;
        this->_pool_difficulty = DEFAULT_POOL_DIFFICULTY;
        this->_gid = 1;
        this->_rsp_str = "";
        this->_vr_mask = 0xffffffff;
        this->_rsp_json.clear();
        this->_sub_info = {"", "", 0};
        this->_msg_rsp_map.clear();
        this->_suggest_diff_support = true;
        this->_is_subscribed = false;
        this->_is_authorized = false;
        this->new_job_xsem   = xSemaphoreCreateCounting(5,0);
        this->clear_job_xsem = xSemaphoreCreateCounting(1,0);
    };
    ~StratumClass();

    void reset();
    void reset(pool_info_t pConfig, stratum_info_t sConfig);
    bool subscribe();
    bool authorize();
    bool suggest_difficulty();
    bool config_version_rolling();
    bool submit(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version);
    bool hello_pool(uint32_t hello_interval, uint32_t lost_max_time);
    stratum_method_data listen_methods();
    // NEW: Symmetrical nonce exclusion methods
    void generate_symmetrical_exclusion_list();
    bool is_binary_palindrome(uint32_t n);
    bool is_symmetrical_nonce(uint32_t nonce);
    void set_symmetrical_exclusion(bool enabled);
    void get_symmetrical_exclusion_stats();
    size_t push_job_cache(pool_job_data_t job);
    pool_job_data_t pop_job_cache();

    size_t get_job_cache_size();
    size_t clear_job_cache();
    
    stratum_rsp get_method_rsp_by_id(uint32_t id);
    bool set_msg_rsp_map(uint32_t id, bool status);
    bool del_msg_rsp_map(uint32_t id);
    bool is_submit_timeout();

    void   set_sub_extranonce1(String extranonce1);
    void   set_sub_extranonce2_size(int size);
    String get_sub_extranonce1();
    String get_sub_extranonce2();
    bool   clear_sub_extranonce2();



    bool is_subscribed(){
        return this->_is_subscribed;
    }
    bool is_authorized(){
        return this->_is_authorized;
    }
    void set_authorize(bool status){
        this->_is_authorized = status;
    }
    void set_subscribe(bool status){
        this->_is_subscribed = status;
    }
    void set_version_mask(uint32_t mask){
        this->_vr_mask = mask;
    }
    uint32_t get_version_mask(){
        return this->_vr_mask;
    }
    void set_pool_difficulty(double diff){
        this->_pool_difficulty = diff;
    }
    double get_pool_difficulty(){
        return this->_pool_difficulty;
    }
};

void stratum_thread_entry(void *args);
#endif
