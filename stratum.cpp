#include <Arduino.h>
#include "stratum.h"
#include "logger.h"
#include "csha256.h"
#include <cfloat>
#include "monitor.h"
#include "helper.h"
#include "esp_log.h"
#include "global.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

StratumClass::~StratumClass(){
    this->_rsp_json.garbageCollect();
}

// NEW: Generate comprehensive list of symmetrical numbers to exclude
void StratumClass::generate_symmetrical_exclusion_list() {
    _excluded_symmetrical_nonces.clear();
    
    LOG_I("Generating symmetrical number exclusion list...");
    
    // 1. PALINDROMIC NUMBERS (reads same forwards and backwards)
    // Single digit palindromes
    for(uint32_t i = 0; i <= 9; i++) {
        _excluded_symmetrical_nonces.push_back(i);
    }
    
    // Multi-digit palindromes (up to 10 digits for 32-bit range)
    for(int digits = 2; digits <= 10; digits++) {
        if(digits % 2 == 0) { // Even digits
            int half = digits / 2;
            uint32_t start = 1;
            for(int i = 1; i < half; i++) start *= 10;
            uint32_t end = start * 10;
            
            for(uint32_t i = start; i < end && i < 100000; i++) { // Limit for performance
                String first_half = String(i);
                String second_half = first_half;
                // Reverse second_half manually
                int len = second_half.length();
                for(int j = 0; j < len / 2; j++) {
                    char temp = second_half[j];
                    second_half[j] = second_half[len - 1 - j];
                    second_half[len - 1 - j] = temp;
                }
                String palindrome_str = first_half + second_half;
                uint32_t palindrome = palindrome_str.toInt();
                if(palindrome <= 0xFFFFFFFF) {
                    _excluded_symmetrical_nonces.push_back(palindrome);
                }
            }
        } else { // Odd digits
            int half = digits / 2;
            uint32_t start = 1;
            for(int i = 1; i < half; i++) start *= 10;
            uint32_t end = start * 10;
            
            for(uint32_t i = start; i < end && i < 10000; i++) { // Limit for performance
                for(int mid = 0; mid <= 9; mid++) {
                    String first_half = String(i);
                    String second_half = first_half;
                    // Reverse second_half manually
                    int len = second_half.length();
                    for(int j = 0; j < len / 2; j++) {
                        char temp = second_half[j];
                        second_half[j] = second_half[len - 1 - j];
                        second_half[len - 1 - j] = temp;
                    }
                    String palindrome_str = first_half + String(mid) + second_half;
                    uint32_t palindrome = palindrome_str.toInt();
                    if(palindrome <= 0xFFFFFFFF) {
                        _excluded_symmetrical_nonces.push_back(palindrome);
                    }
                }
            }
        }
    }
    
    // WITH this corrected version:
    uint32_t repdigit_patterns[] = {
        0, 11, 22, 33, 44, 55, 66, 77, 88, 99,
        111, 222, 333, 444, 555, 666, 777, 888, 999,
        1111, 2222, 3333, 4444, 5555, 6666, 7777, 8888, 9999,
        11111, 22222, 33333, 44444, 55555, 66666, 77777, 88888, 99999,
        111111, 222222, 333333, 444444, 555555, 666666, 777777, 888888, 999999,
        1111111, 2222222, 3333333, 4444444, 5555555, 6666666, 7777777, 8888888, 9999999,
        11111111, 22222222, 33333333, 44444444, 55555555, 66666666, 77777777, 88888888, 99999999,
        111111111, 222222222, 333333333, 444444444, 555555555, 666666666, 777777777, 888888888, 999999999,
        1111111111U, 2222222222U, 3333333333U // Remove 4444444444U as it exceeds 32-bit limit
    };
    
    for(uint32_t repdigit : repdigit_patterns) {
        if(repdigit <= 0xFFFFFFFF) {
            _excluded_symmetrical_nonces.push_back(repdigit);
        }
    }
    
    // 3. POWERS OF 2 (highly symmetrical in binary)
    for(int i = 0; i < 32; i++) {
        uint32_t power = 1U << i;
        _excluded_symmetrical_nonces.push_back(power);
        // Also exclude power - 1 (all 1s in binary)
        if(power > 1) {
            _excluded_symmetrical_nonces.push_back(power - 1);
        }
    }
    
    // 4. POWERS OF 10 (decimal symmetry)
    uint32_t power_of_10 = 1;
    while(power_of_10 <= 0xFFFFFFFF / 10) {
        _excluded_symmetrical_nonces.push_back(power_of_10);
        power_of_10 *= 10;
    }
    
    // 5. FIBONACCI NUMBERS (mathematical symmetry)
    uint32_t fib_a = 0, fib_b = 1;
    while(fib_b <= 0xFFFFFFFF) {
        _excluded_symmetrical_nonces.push_back(fib_b);
        uint32_t temp = fib_a + fib_b;
        fib_a = fib_b;
        fib_b = temp;
    }
    
    // 6. PERFECT SQUARES (geometric symmetry)
    for(uint32_t i = 0; i * i <= 0xFFFFFFFF && i < 65536; i++) {
        _excluded_symmetrical_nonces.push_back(i * i);
    }
    
    // 7. TRIANGULAR NUMBERS (n*(n+1)/2)
    for(uint32_t i = 0; i < 92681; i++) { // Limit to prevent overflow
        uint32_t triangular = i * (i + 1) / 2;
        if(triangular <= 0xFFFFFFFF) {
            _excluded_symmetrical_nonces.push_back(triangular);
        }
    }
    
    // 8. BINARY PALINDROMES (symmetrical in binary representation)
    for(uint32_t i = 1; i <= 0xFFFF; i++) { // Limit for performance
        if(is_binary_palindrome(i)) {
            _excluded_symmetrical_nonces.push_back(i);
        }
    }
    
    // 9. ASCENDING/DESCENDING DIGIT SEQUENCES
    // Ascending: 123, 1234, 12345, etc.
    for(int start_digit = 1; start_digit <= 9; start_digit++) {
        uint32_t ascending = 0;
        for(int digit = start_digit; digit <= 9; digit++) {
            ascending = ascending * 10 + digit;
            if(ascending <= 0xFFFFFFFF) {
                _excluded_symmetrical_nonces.push_back(ascending);
            }
        }
    }
    
    // Descending: 987, 9876, 98765, etc.
    for(int start_digit = 9; start_digit >= 1; start_digit--) {
        uint32_t descending = 0;
        for(int digit = start_digit; digit >= 1; digit--) {
            descending = descending * 10 + digit;
            if(descending <= 0xFFFFFFFF) {
                _excluded_symmetrical_nonces.push_back(descending);
            }
        }
    }
    
    // 10. ALTERNATING PATTERNS (0101, 1010, 1212, etc.)
    for(int a = 0; a <= 9; a++) {
        for(int b = 0; b <= 9; b++) {
            if(a != b) {
                uint32_t pattern = 0;
                for(int len = 2; len <= 8; len++) {
                    pattern = pattern * 10 + (len % 2 == 0 ? a : b);
                    if(pattern <= 0xFFFFFFFF) {
                        _excluded_symmetrical_nonces.push_back(pattern);
                    }
                }
            }
        }
    }
    
    // Remove duplicates and sort
    std::sort(_excluded_symmetrical_nonces.begin(), _excluded_symmetrical_nonces.end());
    _excluded_symmetrical_nonces.erase(
        std::unique(_excluded_symmetrical_nonces.begin(), _excluded_symmetrical_nonces.end()),
        _excluded_symmetrical_nonces.end()
    );
    
    // Limit cache size for memory efficiency
    if(_excluded_symmetrical_nonces.size() > _symmetrical_cache_size) {
        _excluded_symmetrical_nonces.resize(_symmetrical_cache_size);
    }
    
    LOG_I("Generated %d symmetrical numbers for exclusion", _excluded_symmetrical_nonces.size());
}

// NEW: Check if a number is a binary palindrome
bool StratumClass::is_binary_palindrome(uint32_t n) {
    uint32_t reversed = 0;
    uint32_t original = n;
    
    while(n > 0) {
        reversed = (reversed << 1) | (n & 1);
        n >>= 1;
    }
    
    return original == reversed;
}

// NEW: Check if a nonce should be excluded due to symmetry
bool StratumClass::is_symmetrical_nonce(uint32_t nonce) {
    if(!_symmetrical_exclusion_enabled) return false;
    
    // Binary search for efficiency
    return std::binary_search(_excluded_symmetrical_nonces.begin(), 
                             _excluded_symmetrical_nonces.end(), 
                             nonce);
}

// NEW: Enable/disable symmetrical exclusion
void StratumClass::set_symmetrical_exclusion(bool enabled) {
    _symmetrical_exclusion_enabled = enabled;
    LOG_I("Symmetrical nonce exclusion %s", enabled ? "enabled" : "disabled");
}

// NEW: Get statistics about excluded nonces
void StratumClass::get_symmetrical_exclusion_stats() {
    LOG_I("=== Symmetrical Exclusion Statistics ===");
    LOG_I("Total excluded nonces: %d", _excluded_symmetrical_nonces.size());
    LOG_I("Exclusion enabled: %s", _symmetrical_exclusion_enabled ? "yes" : "no");
    LOG_I("Cache size limit: %d", _symmetrical_cache_size);
    
    if(_excluded_symmetrical_nonces.size() > 0) {
        LOG_I("First 10 excluded: ");
        for(int i = 0; i < 10 && i < _excluded_symmetrical_nonces.size(); i++) {
            LOG_I("  %u", _excluded_symmetrical_nonces[i]);
        }
        
        LOG_I("Last 10 excluded: ");
        int start = std::max(0, (int)_excluded_symmetrical_nonces.size() - 10);
        for(int i = start; i < _excluded_symmetrical_nonces.size(); i++) {
            LOG_I("  %u", _excluded_symmetrical_nonces[i]);
        }
    }
}

// ENHANCED: Nonce range management methods with symmetrical exclusion
void StratumClass::configure_nonce_ranges(uint32_t num_workers) {
    _total_workers = num_workers;
    _nonce_ranges.clear();
    
    // NEW: Generate symmetrical exclusion list first
    generate_symmetrical_exclusion_list();
    
    uint32_t range_size = 0xFFFFFFFF / num_workers;
    for(uint32_t i = 0; i < num_workers; i++) {
        nonce_range_t range;
        range.worker_id = i;
        range.start = i * range_size;
        range.end = (i == num_workers - 1) ? 0xFFFFFFFF : (i + 1) * range_size - 1;
        range.current = range.start;
        _nonce_ranges.push_back(range);
    }
    
    LOG_I("Configured %d nonce ranges for workers with symmetrical exclusion", num_workers);
    for(uint32_t i = 0; i < num_workers; i++) {
        LOG_D("Worker %d: range 0x%08x - 0x%08x", i, _nonce_ranges[i].start, _nonce_ranges[i].end);
    }
    
    // NEW: Show exclusion statistics
    get_symmetrical_exclusion_stats();
}

uint32_t StratumClass::get_next_nonce(uint32_t worker_id) {
    if(worker_id >= _nonce_ranges.size()) {
        LOG_W("Invalid worker_id %d, max workers: %d", worker_id, _nonce_ranges.size());
        return 0;
    }
    
    // NEW: Symmetrical exclusion logic
    uint32_t attempts = 0;
    uint32_t max_attempts = 1000000000; // Prevent infinite loop
    
    while(attempts < max_attempts) {
        uint32_t nonce = _nonce_ranges[worker_id].current++;
        
        // Reset if we've exhausted the range
        if(_nonce_ranges[worker_id].current > _nonce_ranges[worker_id].end) {
            _nonce_ranges[worker_id].current = _nonce_ranges[worker_id].start;
            LOG_D("Worker %d nonce range reset to start", worker_id);
        }
        
        // NEW: Check if this nonce should be excluded
        if(!is_symmetrical_nonce(nonce)) {
            return nonce; // Return non-symmetrical nonce
        }
        
        attempts++;
        LOG_D("Worker %d skipped symmetrical nonce: %u", worker_id, nonce);
    }
    
    // If we can't find a non-symmetrical nonce, return the current one anyway
    LOG_W("Worker %d: Could not find non-symmetrical nonce after %d attempts", worker_id, max_attempts);
    return _nonce_ranges[worker_id].current++;
}

bool StratumClass::reset_nonce_range(uint32_t worker_id) {
    if(worker_id >= _nonce_ranges.size()) {
        return false;
    }
    
    _nonce_ranges[worker_id].current = _nonce_ranges[worker_id].start;
    LOG_D("Worker %d nonce range manually reset", worker_id);
    return true;
}

void StratumClass::reset_all_nonce_ranges() {
    for(auto& range : _nonce_ranges) {
        range.current = range.start;
    }
    LOG_D("All nonce ranges reset");
}

uint32_t StratumClass::get_nonce_range_progress(uint32_t worker_id) {
    if(worker_id >= _nonce_ranges.size()) return 0;
    
    uint64_t total_range = (uint64_t)_nonce_ranges[worker_id].end - _nonce_ranges[worker_id].start + 1;
    uint64_t current_progress = (uint64_t)_nonce_ranges[worker_id].current - _nonce_ranges[worker_id].start;
    
    return (uint32_t)((current_progress * 100) / total_range);
}

bool StratumClass::submit_with_worker(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t worker_id, uint32_t version) {
    uint32_t nonce = get_next_nonce(worker_id);
    if(nonce == 0) {
        LOG_E("Failed to get nonce for worker %d", worker_id);
        return false;
    }
    
    return submit(pool_job_id, extranonce2, ntime, nonce, version);
}

// MODIFIED: Enhanced reset methods to include nonce range reset
void StratumClass::reset(){
    this->_rsp_str = "";
    this->_rsp_json.clear();
    this->_msg_rsp_map.clear();
    this->_sub_info.extranonce1 = "";
    this->_sub_info.extranonce2 = "0";
    this->_sub_info.extranonce2_size = 0;
    this->_sub_info = {"", "", 0};
    this->_is_subscribed = false;
    this->_is_authorized = false;
    this->_pool_difficulty = DEFAULT_POOL_DIFFICULTY;
    this->_vr_mask = 0xffffffff;
    this->_suggest_diff_support = true;
    this->_gid = 1;
    
    // Reset nonce ranges
    this->reset_all_nonce_ranges();
}

void StratumClass::reset(pool_info_t pConfig, stratum_info_t sConfig){
    if(this->pool == NULL) return;
    delete this->pool;
    
    this->pool = new PoolClass(pConfig);

    this->_stratum_info = sConfig;
    this->_rsp_str = "";
    this->_rsp_json.clear();
    this->_msg_rsp_map.clear();
    this->_sub_info.extranonce1 = "";
    this->_sub_info.extranonce2 = "0";
    this->_sub_info.extranonce2_size = 0;
    this->_sub_info = {"", "", 0};
    this->_is_subscribed = false;
    this->_is_authorized = false;
    this->_pool_difficulty = DEFAULT_POOL_DIFFICULTY;
    this->_vr_mask = 0xffffffff;
    this->_suggest_diff_support = true;
    this->_gid = 1;
    
    // Reset nonce ranges
    this->reset_all_nonce_ranges();
}

uint32_t StratumClass::_get_msg_id(){
    return this->_gid++;
}

bool StratumClass::_parse_rsp(){
    DeserializationError error = deserializeJson(this->_rsp_json, this->_rsp_str);
    if (error) {
        LOG_E("Failed to parse JSON: %s => %s", error.c_str(), this->_rsp_str.c_str());
        return false;
    }
    return true;
}

bool StratumClass::_clear_rsp_id_cache(){
    if(this->_msg_rsp_map.size() > this->_max_rsp_id_cache){
        for(auto it = this->_msg_rsp_map.begin(); it != this->_msg_rsp_map.end();){
            if(it->first < this->_gid - this->_max_rsp_id_cache){
                it = this->_msg_rsp_map.erase(it);
                LOG_D("Message ID [%d] [%s] cleared from cache, cache size %d", it->first, it->second.method.c_str(), this->_msg_rsp_map.size());
            }else{
                it++;
            }
        }
    }
    return true;
}

bool StratumClass::hello_pool(uint32_t hello_interval, uint32_t lost_max_time){
    this->_clear_rsp_id_cache();//clear cache of msg id
    if((millis() - this->pool->get_last_write_ms() > hello_interval) && this->_suggest_diff_support){
        uint32_t id = this->_get_msg_id();
        String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.suggest_difficulty\", \"params\": [" + String(this->_pool_difficulty, 4) + "]}\n";
        if(this->pool->write(payload) != 0){
            this->_msg_rsp_map[id] = {"mining.suggest_difficulty", false, millis()};
            LOG_D("Hello pool...");
            return true;
        }
        else{
            LOG_W("Failed to send mining.suggest_difficulty, last sent to pool %lu s ago, reconnecting...", (millis() - this->pool->get_last_write_ms()) / 1000);
            this->reset();
            this->pool->end();
            return false;
        }
    }
    if(millis() - this->pool->get_last_read_ms() > lost_max_time){
        LOG_W("It seems pool inactive, last received from pool %lu s ago, reconnecting...", (millis() - this->pool->get_last_read_ms()) / 1000);
        this->reset();
        this->pool->end();
        return false;
    }
    return true;
}

stratum_method_data StratumClass::listen_methods(){
    this->_rsp_str = this->pool->readline();
    if(this->_rsp_str == ""){
        return {-1, STRATUM_DOWN_PARSE_ERROR, "", ""};
    }

    if(!this->_parse_rsp()){
        return {-1, STRATUM_DOWN_PARSE_ERROR, "", ""};
    }

    int32_t id = (this->_rsp_json["id"] == nullptr) ? -1 : this->_rsp_json["id"];

    if(this->_rsp_json.containsKey("method")){
        if(this->_rsp_json["method"] == "mining.notify"){
            return {id, STRATUM_DOWN_NOTIFY, "mining.notify", this->_rsp_str};
        }
        if(this->_rsp_json["method"] == "mining.set_difficulty"){
            return {id, STRATUM_DOWN_SET_DIFFICULTY, "mining.set_difficulty", this->_rsp_str};
        }
        if(this->_rsp_json["method"] == "mining.set_version_mask"){
            return {id, STRATUM_DOWN_SET_VERSION_MASK, "mining.set_version_mask", this->_rsp_str};
        }
        if(this->_rsp_json["method"] == "mining.set_extranonce"){
            return {id, STRATUM_DOWN_SET_EXTRANONCE, "mining.set_extranonce", this->_rsp_str};
        }
    }
    else{
        if(this->_rsp_json["error"].isNull()){
            return {id, STRATUM_DOWN_SUCCESS, "", this->_rsp_str};
        }else{
            //'suggest_difficulty' method didn't support 
            if(4 == id){
                this->_suggest_diff_support = false;
                LOG_W("Pool doesn't support suggest_difficulty!");
            }
            return {id, STRATUM_DOWN_ERROR, "", this->_rsp_str};
        }
    }
    return {id, STRATUM_DOWN_UNKNOWN, "", this->_rsp_str};
}

String StratumClass::get_sub_extranonce1(){
    return this->_sub_info.extranonce1;
}

String StratumClass::get_sub_extranonce2() {
    uint64_t ext2 = strtoull(this->_sub_info.extranonce2.c_str(), NULL, 16); 
    ext2++;
    ext2 = ext2 & ((1ULL << (8 * this->_sub_info.extranonce2_size)) - 1);

    char buffer[2 * this->_sub_info.extranonce2_size + 1];
    snprintf(buffer, sizeof(buffer), "%0*llx", 2 * this->_sub_info.extranonce2_size, ext2);
    String next_ext2(buffer);

    this->_sub_info.extranonce2 = next_ext2;
    return next_ext2;
}

bool StratumClass::clear_sub_extranonce2(){
    this->_sub_info.extranonce2 = "0";
    return (this->_sub_info.extranonce2 == "0");
}   

void StratumClass::set_sub_extranonce1(String extranonce1){
    this->_sub_info.extranonce1 = extranonce1;
}

void StratumClass::set_sub_extranonce2_size(int size){
    this->_sub_info.extranonce2_size = size;
}

bool StratumClass::subscribe(){
    this->_sub_info.extranonce2 = "";
    this->_sub_info.extranonce2_size = 0;
    this->_is_subscribed = false;
    
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.subscribe\", \"params\": [\"" +  g_nmaxe.board.hw_model + "/" + CURRENT_FW_VERSION +"\"]}\n";
    if(this->pool->write(payload) == 0){
        LOG_E("Failed to send mining.subscribe request");
        return false;
    }


    //wait for response
    uint32_t start = millis();
    while (true){
        this->_rsp_str = this->pool->readline(100);
        if(this->_rsp_str == "" ) {
            if(millis() - start > 1000*10){
                LOG_E("Failed to read mining.subscribe response");
                return false;
            }
        }else{
            break;
        }
    }
    
    if(!this->_parse_rsp()){
        LOG_E("Failed to parse mining.subscribe response");
        return false;
    }
    this->_sub_info.extranonce1 = String((const char*)this->_rsp_json["result"][1]);
    this->_sub_info.extranonce2_size = this->_rsp_json["result"][2];
    this->_is_subscribed = true;
    this->_msg_rsp_map[id] = {"mining.subscribe", false, millis()};
    log_i("Sending mining.subscribe : %s", payload.c_str());
    LOG_I("extranonce1 : %s", this->_sub_info.extranonce1.c_str());
    LOG_I("extranonce2 size : %d", this->_sub_info.extranonce2_size);
    return true;
}

bool StratumClass::authorize(){
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.authorize\", \"params\": [\"" + this->_stratum_info.user+ "\", \"" + this->_stratum_info.pwd + "\"]}\n";
    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.authorize request");
        return false;
    }
    this->_msg_rsp_map[id] = {"mining.authorize", false, millis()};
    log_i("Sending mining.authorize : %s", payload.c_str());
    delay(100);
    return true;
}

bool StratumClass::suggest_difficulty(){
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.suggest_difficulty\", \"params\": [" + String(this->_pool_difficulty, 4) + "]}\n";
    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.suggest_difficulty request");
        return false;
    }
    this->_msg_rsp_map[id] = {"mining.suggest_difficulty", false, millis()};
    log_i("Sending mining.suggest_difficulty : %s", payload.c_str());
    delay(100);
    return true;
}

bool StratumClass::config_version_rolling(){
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.configure\", \"params\": [[\"version-rolling\"], {\"version-rolling.mask\": \"ffffffff\"}]}\n";
    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.configure request");
        return false;
    }
    this->_msg_rsp_map[id] = {"mining.configure", false, millis()};
    log_i("Sending mining.configure : %s", payload.c_str());
    delay(100);
    return true;
}

bool StratumClass::submit(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version){
    uint32_t msgid = this->_get_msg_id();
    char version_str[9] = {0,}, nonce_str[9] = {0,};
    sprintf(version_str, "%08x", version);
    sprintf(nonce_str, "%08x", nonce);

    String payload = "{\"id\": " + String(msgid) + ", \"method\": \"mining.submit\", \"params\": [\"" + 
    this->_stratum_info.user + "\", \"" + 
    pool_job_id + "\", \"" + 
    extranonce2 + "\", \"" + 
    String(ntime, 16) + "\", \"" + 
    String(nonce_str) + "\", \"" + 
    String(version_str) + "\"]}\n";

    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.submit request");
        return false;
    }
    this->_msg_rsp_map[msgid] = {"mining.submit", false, millis()};
    // log_i("%s", payload.c_str());

    //wait for response from pool
    uint32_t start = millis();
    while(true){
        if(this->_msg_rsp_map[msgid].status) return true;
        if(millis() - start > 1000*20) return false;
        delay(1);
    }
    return false;
}

bool StratumClass::is_submit_timeout(){
    bool timeout = true, has_submit = false;
    
    //cache size check
    if(this->_msg_rsp_map.size() <= this->_max_rsp_id_cache / 2) return false;

    //check if there is any submit request
    for(auto it = this->_msg_rsp_map.begin(); it != this->_msg_rsp_map.end();it++){
        if(it->second.method == "mining.submit"){
            has_submit = true;
            break;
        }
    }
    if(!has_submit) return false;

    //timeout check, if all submit has no response, return true
    for(auto it = this->_msg_rsp_map.begin(); it != this->_msg_rsp_map.end();it++){
        if((it->second.method == "mining.submit") && (it->second.status)){//submit response received
            timeout = false;
            break;
        }
    }
    return timeout;
}

size_t StratumClass::push_job_cache(pool_job_data_t job){
    LOG_D("");
    if (this->_pool_job_cache.size() >= this->_pool_job_cache_size) {
        LOG_D("Job [%s] popped from cache...", this->_pool_job_cache.front().id.c_str());
        this->_pool_job_cache.pop_front();
    }
    this->_pool_job_cache.push_back(job);
    LOG_D("---Job cache [%02d]---", this->_pool_job_cache.size());
    for(size_t i =0; i < this->_pool_job_cache.size(); i++){
        LOG_D("Job id : %s", this->_pool_job_cache[i].id.c_str());
    }
    LOG_D("--------------------");
    return this->_pool_job_cache.size();
}

size_t StratumClass::get_job_cache_size(){
    return this->_pool_job_cache.size();
}

size_t StratumClass::clear_job_cache(){
    this->_pool_job_cache.clear();
    return this->_pool_job_cache.size();
}

pool_job_data_t StratumClass::pop_job_cache(){
    if(this->_pool_job_cache.empty()){
        return pool_job_data_t();
    }
    pool_job_data_t job = this->_pool_job_cache.front();
    this->_pool_job_cache.pop_front();
    return job;
}

bool StratumClass::set_msg_rsp_map(uint32_t id, bool status){
    auto it = this->_msg_rsp_map.find(id);
    if(it == this->_msg_rsp_map.end()){
        LOG_E("Message ID [%d] not found in response map", id);
        return false;
    }
    LOG_D("Message [%s] with ID [%d] status set to [%s]", it->second.method.c_str(), id, status ? "true" : "false");
    it->second.status = status;
    return true;
}

bool StratumClass::del_msg_rsp_map(uint32_t id){
    auto it = this->_msg_rsp_map.find(id);
    if(it == this->_msg_rsp_map.end()){
        LOG_E("Message ID [%d] not found in response map", id);
        return false;
    }
    LOG_D("Message [%s] with ID [%d] deleted from response map, cache size %d", it->second.method.c_str(), id, this->_msg_rsp_map.size());
    this->_msg_rsp_map.erase(it);
    return true;
}

stratum_rsp StratumClass::get_method_rsp_by_id(uint32_t id){
    stratum_rsp rsp = {
        .method = "",
        .status = false
    };
    if (!this->_msg_rsp_map.empty()) {
       if(this->_msg_rsp_map.find(id) != this->_msg_rsp_map.end()){
           rsp = this->_msg_rsp_map[id];
       }
    }
    return rsp;
}

void stratum_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    g_nmaxe.stratum->set_pool_difficulty(DEFAULT_POOL_DIFFICULTY);
    StaticJsonDocument<1024*4> json;
    while(true){
        static int w_retry = 0, w_maxRetries = 24;
        if(g_nmaxe.connection.wifi.status_param.status != WL_CONNECTED){
            w_retry++;
            LOG_W("WiFi reconnecting %d/%d...", w_retry, w_maxRetries);
            if(w_retry >= w_maxRetries) ESP.restart();

            xSemaphoreGive(g_nmaxe.connection.wifi.reconnect_xsem);
            g_nmaxe.stratum->reset();
            delay(5000);
            continue;
        } else w_retry = 0;
        
        static uint16_t p_retry = 0, p_maxRetries = 5;
        if(!g_nmaxe.stratum->pool->is_connected()){
            static bool    first_connect = true;
            if(first_connect){
                LOG_I("Pool connecting...");
                first_connect = false;
            }else LOG_W("Lost connection to pool, reconnecting %d/%d...", p_retry, p_maxRetries);
            
            if(++p_retry % p_maxRetries == 0){
                static bool sel_fallback = true;
                if(sel_fallback){
                    sel_fallback = false;
                    g_nmaxe.connection.pool_use    = g_nmaxe.connection.pool_fallback;
                    g_nmaxe.connection.stratum_use = g_nmaxe.connection.stratum_fallback;
                    LOG_W(">>>> Set pool to fallback [%s:%d] <<<<", g_nmaxe.connection.pool_use.url.c_str(), g_nmaxe.connection.pool_use.port);
                }else{
                    sel_fallback = true;
                    g_nmaxe.connection.pool_use    = g_nmaxe.connection.pool_primary;
                    g_nmaxe.connection.stratum_use = g_nmaxe.connection.stratum_primary;
                    LOG_W(">>>> Set pool to primary [%s:%d] <<<<", g_nmaxe.connection.pool_use.url.c_str(), g_nmaxe.connection.pool_use.port);
                }
            }
            g_nmaxe.stratum->reset(g_nmaxe.connection.pool_use, g_nmaxe.connection.stratum_use);
            g_nmaxe.stratum->pool->begin(g_nmaxe.connection.pool_use.ssl);
            g_nmaxe.stratum->pool->connect();
            g_nmaxe.mstatus.diff.last = 0;
            delay(5000);
            continue;
        }else p_retry = 0;

        if(!g_nmaxe.stratum->is_subscribed()){
            if(!g_nmaxe.stratum->subscribe()){
                LOG_W("Failed to subscribe to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!g_nmaxe.stratum->authorize()){
                LOG_W("Failed to authorize to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!g_nmaxe.stratum->config_version_rolling()){
                LOG_W("Failed to config version rolling, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!g_nmaxe.stratum->suggest_difficulty()){
                LOG_W("Failed to suggest difficulty to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
        }

        if(!g_nmaxe.stratum->hello_pool(HELLO_POOL_INTERVAL_MS, POOL_INACTIVITY_TIME_MS)){
            LOG_W("Pool is inactive, retrying in 5 seconds...");
            delay(5000);
            continue;
        }

        while(g_nmaxe.stratum->pool->available()){
            g_nmaxe.connection.stratum_update = millis();//pool is alive
            stratum_method_data method = g_nmaxe.stratum->listen_methods();
            switch (method.type){
                case STRATUM_DOWN_PARSE_ERROR:   
                    LOG_E("Stratum parse error, id : %d, raw : %s", method.id, method.raw.c_str());
                    break;
                case STRATUM_DOWN_NOTIFY:{
                        LOG_D("Stratum notify, id : %d => %s", method.id, method.raw.c_str());
                        pool_job_data_t job;
                        json.clear();
                        DeserializationError error = deserializeJson(json, method.raw);
                        if (error) {
                            LOG_E("Failed to parse JSON: %s", error.c_str());
                            break;
                        }
                        job.id = String((const char*) json["params"][0]);
                        job.prevhash = String((const char*) json["params"][1]);
                        job.coinb1 = String((const char*) json["params"][2]);
                        job.coinb2 = String((const char*) json["params"][3]);
                        job.merkle_branch = json["params"][4];
                        job.version = String((const char*) json["params"][5]);
                        job.nbits = String((const char*) json["params"][6]);
                        job.ntime = String((const char*) json["params"][7]);
                        job.clean_jobs = json["params"][8]; 

                        LOG_D("Job ID            : %s", job.id.c_str());
                        LOG_D("Prevhash          : %s", job.prevhash.c_str());
                        LOG_D("Coinb1            : %s", job.coinb1.c_str());
                        LOG_D("Coinb2            : %s", job.coinb2.c_str());
                        for(int i = 0; i < job.merkle_branch.size(); i++){
                            LOG_D("Merkle branch[%02d] : %s", i, job.merkle_branch[i].as<String>().c_str());
                        }
                        LOG_D("Version           : %s", job.version.c_str());
                        LOG_D("Nbits             : %s", job.nbits.c_str());
                        LOG_D("Ntime             : %s", job.ntime.c_str());
                        LOG_D("Clean jobs        : %s", job.clean_jobs ? "true" : "false");
                        LOG_D("Stamp             : %lu", job.stamp);
                        LOG_D("Version mask      : 0x%08x", g_nmaxe.stratum->get_version_mask());
                        LOG_D("Pool difficulty   : %s", formatNumber(g_nmaxe.stratum->get_pool_difficulty(), 5).c_str());

                        if(job.clean_jobs){
                            g_nmaxe.stratum->clear_job_cache();
                            xSemaphoreGive(g_nmaxe.stratum->clear_job_xsem);
                        }
                        size_t cached_size = g_nmaxe.stratum->push_job_cache(job);
                        
                        //Give the new job semaphore to the other threads
                        xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//asic tx thread
                        static bool first_job = true;
                        if(first_job){
                            //first job will release the asic rx , monitor and ui thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//asic tx thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//asic rx thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//ui thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//monitor thread
                            first_job = false;
                        }
                    }         
                    break;
                case STRATUM_DOWN_SET_DIFFICULTY: {
                    LOG_D("Stratum set difficulty, id : %d => %s", method.id, method.raw.c_str());
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if(error){
                        LOG_E("Failed to parse JSON: %s", error.c_str());
                        break;
                    }
                    if(json["method"] == "mining.set_difficulty"){
                        if(json["params"].size() > 0){
                            g_nmaxe.stratum->set_pool_difficulty(json["params"][0]);
                            LOG_D("Pool difficulty set : %s", formatNumber(json["params"][0], 5).c_str());
                        }else{
                            LOG_W("Pool difficulty not found in params");
                        }
                    }
                }
                    break;
                case STRATUM_DOWN_SET_VERSION_MASK:{
                    LOG_D("Stratum set version mask , id : %d => %s", method.id, method.raw.c_str());
                    g_nmaxe.stratum->set_msg_rsp_map(method.id, true);
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if (error) {
                        LOG_E("Failed to parse JSON: %s", error.c_str());
                        break;
                    }
                    if(json["method"] == "mining.set_version_mask"){
                        if(json["params"].size() > 0){
                            g_nmaxe.stratum->set_version_mask(strtoul(json["params"][0].as<const char*>(), NULL, 16));
                            LOG_L("Version mask set to %s", json["params"][0].as<const char*>());
                        }else{
                            g_nmaxe.stratum->set_version_mask(0xffffffff);
                            LOG_W("Version mask not found in params");
                        }
                    }else{
                        g_nmaxe.stratum->set_version_mask(0xffffffff);
                        LOG_W("Version rolling key not found in response");
                    }
                    g_nmaxe.stratum->del_msg_rsp_map(method.id);
                }
                    break;
                case STRATUM_DOWN_SET_EXTRANONCE:{
                        LOG_L("Stratum set extranonce => %s", method.id, method.raw.c_str());
                        json.clear();
                        DeserializationError error = deserializeJson(json, method.raw);
                        if (error) {
                            LOG_E("Failed to parse JSON: %s", error.c_str());
                            break;
                        }
                        g_nmaxe.stratum->set_sub_extranonce1(json["params"][0]);
                        g_nmaxe.stratum->set_sub_extranonce2_size(json["params"][1]);
                    }
                    break;
                case STRATUM_DOWN_SUCCESS: 
                    if(method.id != -1){
                        g_nmaxe.stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = g_nmaxe.stratum->get_method_rsp_by_id(method.id);
                        if(rsp.method == "mining.submit"){
                            uint32_t latency = millis() - rsp.stamp;
                            if (rsp.status == true){
                                g_nmaxe.mstatus.share_accepted++;
                                LOG_L("#%d share accepted, %ldms", g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected, latency);      
                            }
                            else {
                                g_nmaxe.mstatus.share_rejected++;
                                LOG_E("#%d share rejected, %ldms", g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected, latency);
                            }
                        }
                        else if(rsp.method == "mining.configure"){
                            json.clear();
                            DeserializationError error = deserializeJson(json, method.raw);
                            if (error) {
                                LOG_E("Failed to parse JSON: %s", error.c_str());
                            } else {
                                g_nmaxe.stratum->set_version_mask(0xffffffff);
                                if (json["result"]["version-rolling"] == true) {
                                    if (json["result"].containsKey("version-rolling.mask")) {
                                        g_nmaxe.stratum->set_version_mask(strtoul(json["result"]["version-rolling.mask"].as<const char*>(), NULL, 16));
                                        LOG_I("Version mask set to %s", json["result"]["version-rolling.mask"].as<const char*>());
                                    } else {
                                        LOG_W("Version mask not found in response");
                                    }
                                } else {
                                    LOG_W("Version rolling not supported");
                                }
                            }
                        }
                        else if(rsp.method == "mining.authorize"){
                            DeserializationError error = deserializeJson(json, method.raw);
                            if (error) {
                                LOG_E("Failed to parse JSON: %s", error.c_str());
                            }
                            else{
                                if(json.containsKey("result")){
                                    g_nmaxe.stratum->set_authorize(json["result"]);
                                    LOG_W("Authorization %s ", json["result"] ? "success" : "failed");
                                }
                            }
                        }
                        else{
                            LOG_D("Stratum success, id : %d => %s", method.id, method.raw.c_str());
                        }
                    }
                    break;
                case STRATUM_DOWN_ERROR: 
                    if(method.id != -1){
                        g_nmaxe.stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = g_nmaxe.stratum->get_method_rsp_by_id(method.id);
                        if(rsp.method == "mining.submit"){
                            uint32_t latency = millis() - rsp.stamp;
                            g_nmaxe.mstatus.share_rejected++;
                            LOG_E("#%d share rejected, %ldms", g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected, latency);
                        }
                        else if(rsp.method == "mining.authorize"){
                            g_nmaxe.stratum->set_authorize(false);
                            LOG_E("Authorization failed, id %d => %s", method.id, method.raw.c_str());
                        }
                        else{
                            LOG_E("Unknown error response, id : %d => %s", method.id, method.raw.c_str());
                        }
                    }
                    break;
                case STRATUM_DOWN_UNKNOWN:                   
                    LOG_E("Stratum unknown, id : %d => %s", method.id, method.raw.c_str());
                    break;
                default :
                    LOG_E("Stratum unknown, id : %d => %s", method.id, method.raw.c_str());
                    break;
            }
            delay(5);
        }
        delay(50);
    }
}
