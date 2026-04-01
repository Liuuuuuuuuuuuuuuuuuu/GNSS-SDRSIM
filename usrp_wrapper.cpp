#include "usrp_wrapper.h"
#include <uhd/usrp/multi_usrp.hpp>
#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <chrono>

static uhd::usrp::multi_usrp::sptr usrp_dev;
static uhd::tx_streamer::sptr tx_stream;
static uhd::tx_metadata_t md;

static std::queue<std::vector<int16_t>> tx_queue;
static std::mutex mtx;
static std::condition_variable cv_consumer; // 喚醒發射執行緒
static std::condition_variable cv_producer; // 喚醒主運算執行緒
static bool is_running = false;
static std::thread tx_worker_thread;
static bool first_send = true;
static bool scheduled_has_time = false;
static double scheduled_device_time = 0.0;
static int g_sample_format = USRP_SAMPLE_FMT_SHORT;

static void clear_tx_queue_locked() {
    std::queue<std::vector<int16_t>> empty;
    tx_queue.swap(empty);
}

static void reset_schedule_locked() {
    first_send = true;
    scheduled_has_time = false;
    scheduled_device_time = 0.0;
}

static bool wait_ref_lock_if_available() {
    if (!usrp_dev) return false;
    try {
        auto names = usrp_dev->get_mboard_sensor_names(0);
        bool has_ref_locked = false;
        for (const auto &n : names) {
            if (n == "ref_locked") {
                has_ref_locked = true;
                break;
            }
        }
        if (!has_ref_locked) return true;

        for (int i = 0; i < 20; ++i) {
            if (usrp_dev->get_mboard_sensor("ref_locked", 0).to_bool()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    } catch (...) {
        return false;
    }
}

// 【關鍵】：設定緩衝區最大容量 (50 代表大約 50 毫秒的預先緩衝)
const size_t MAX_QUEUE_SIZE = 5000; 

// --- 背景發射執行緒 ---
void tx_worker() {
    sched_param sch_params;
    sch_params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch_params);

    while (is_running || !tx_queue.empty()) {
        std::vector<int16_t> buffer;
        int sample_format = USRP_SAMPLE_FMT_SHORT;
        
        {
            std::unique_lock<std::mutex> lock(mtx);
            // 等待資料進來
            cv_consumer.wait(lock, [] { return !tx_queue.empty() || !is_running; });
            
            if (tx_queue.empty()) continue;
            
            buffer = std::move(tx_queue.front());
            tx_queue.pop();
            sample_format = g_sample_format;
        }

        std::vector<int8_t> buffer_sc8;
        if (sample_format == USRP_SAMPLE_FMT_BYTE) {
            buffer_sc8.resize(buffer.size());
            for (size_t i = 0; i < buffer.size(); ++i) {
                int v = (int)buffer[i];
                // 16-bit IQ is generated around full scale; compress to signed 8-bit for sc8 streaming.
                v = (v >= 0) ? ((v + 128) / 256) : ((v - 128) / 256);
                if (v > 127) v = 127;
                if (v < -128) v = -128;
                buffer_sc8[i] = (int8_t)v;
            }
        }
        
        // 【背壓解鎖】：我們拿走了一包資料，喚醒主程式繼續算！
        cv_producer.notify_one();

        // 穩定發射
        if (tx_stream) {
            size_t num_tx_samps = 0;
            size_t total_samps = buffer.size() / 2;
            
            while (num_tx_samps < total_samps) {
                // If this is the very first send, and a scheduled time was set,
                // attach the time_spec to metadata so the USRP will start at
                // the scheduled device time.
                if (first_send) {
                    if (scheduled_has_time) {
                        md.has_time_spec = true;
                        md.time_spec = uhd::time_spec_t(scheduled_device_time);
                    } else {
                        md.has_time_spec = false;
                    }
                }

                const void *payload = (sample_format == USRP_SAMPLE_FMT_BYTE)
                    ? (const void *)(buffer_sc8.data() + (num_tx_samps * 2))
                    : (const void *)(buffer.data() + (num_tx_samps * 2));

                size_t sent = tx_stream->send(
                    payload,
                    total_samps - num_tx_samps,
                    md
                );
                if (sent == 0) {
                    // Avoid infinite loop if device stops accepting samples.
                    break;
                }
                num_tx_samps += sent;
                if (first_send) {
                    md.start_of_burst = false;
                    // clear time_spec after first burst has been queued
                    md.has_time_spec = false;
                    scheduled_has_time = false;
                    first_send = false;
                }
            }
        }
    }
}

extern "C" {

int usrp_init(double freq, double rate, double gain, int use_external_clk, int sample_format) {
    try {
        std::cout << "\n==========================================" << std::endl;
        std::cout << "[USRP] 啟動「真・即時同步 (Real-Time with Backpressure)」" << std::endl;
        
        usrp_dev = uhd::usrp::multi_usrp::make("num_send_frames=1024,send_frame_size=8192");
        usrp_dev->set_clock_source(use_external_clk ? "external" : "internal");
        usrp_dev->set_tx_rate(rate);
        usrp_dev->set_tx_freq(freq);
        usrp_dev->set_tx_gain(gain);
        usrp_dev->set_tx_antenna("TX/RX");

        std::cout << "[USRP] Clock source: " << (use_external_clk ? "external" : "internal") << std::endl;

        bool use_sc8 = (sample_format == USRP_SAMPLE_FMT_BYTE);
        uhd::stream_args_t stream_args(use_sc8 ? "sc8" : "sc16", use_sc8 ? "sc8" : "sc12");
        tx_stream = usrp_dev->get_tx_stream(stream_args);
        std::cout << "[USRP] Sample format: " << (use_sc8 ? "byte(sc8)" : "short(sc16)") << std::endl;

        if (use_external_clk) {
            bool locked = wait_ref_lock_if_available();
            if (!locked) {
                std::cerr << "[USRP] 警告: external ref lock 未確認，仍嘗試持續發射" << std::endl;
            }
        }

        md.start_of_burst = true;
        md.end_of_burst   = false;
        md.has_time_spec  = false; 
        {
            std::lock_guard<std::mutex> lock(mtx);
            clear_tx_queue_locked();
            reset_schedule_locked();
            g_sample_format = use_sc8 ? USRP_SAMPLE_FMT_BYTE : USRP_SAMPLE_FMT_SHORT;
            is_running = true;
        }
        tx_worker_thread = std::thread(tx_worker);

        std::cout << "[USRP] 節流機制已啟動，確保 CPU 運算與 USRP 發射完美同步...\n" << std::endl;
        return 0;

    } catch (const std::exception &e) {
        std::cerr << "❌ [USRP] 初始化錯誤: " << e.what() << std::endl;
        return -1;
    }
}

void usrp_schedule_start_in(double seconds_from_now) {
    if (!usrp_dev) return;
    try {
        double dev_now = usrp_dev->get_time_now().get_real_secs();
        {
            std::lock_guard<std::mutex> lock(mtx);
            scheduled_device_time = dev_now + seconds_from_now;
            scheduled_has_time = true;
        }
        std::cout << "[USRP] 已排程在 " << seconds_from_now << " 秒後啟動 (device time=" << scheduled_device_time << ")\n";
    } catch (const std::exception &e) {
        std::cerr << "[USRP] 排程失敗: " << e.what() << std::endl;
    }
}

size_t usrp_send(const int16_t *iq_data, size_t num_samples) {
    if (!iq_data || num_samples == 0) return 0;
    if (!is_running || !tx_stream) {
        return 0;
    }

    std::vector<int16_t> new_data(iq_data, iq_data + (num_samples * 2));
    
    {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 【背壓煞車】：如果 Queue 塞滿了，強制暫停 CPU 運算，把效能讓給發射器！
        cv_producer.wait(lock, [] { return tx_queue.size() < MAX_QUEUE_SIZE || !is_running; });
        if (!is_running) return 0;
        
        tx_queue.push(std::move(new_data));
    }
    
    // 呼叫發射器起床工作
    cv_consumer.notify_one(); 

    return num_samples;
}

void usrp_close(void) {
    std::cout << "\n[USRP] 停止運算，準備安全關閉..." << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(mtx);
        is_running = false;
        clear_tx_queue_locked();
    }
    cv_consumer.notify_all();
    cv_producer.notify_all(); // 防呆，避免主程式卡死
    
    if (tx_worker_thread.joinable()) {
        tx_worker_thread.join();
    }

    if (tx_stream) {
        md.end_of_burst = true;
        int16_t dummy = 0;
        tx_stream->send(&dummy, 0, md);
        tx_stream.reset();
    }
    if (usrp_dev) usrp_dev.reset();
    {
        std::lock_guard<std::mutex> lock(mtx);
        reset_schedule_locked();
    }
    std::cout << "[USRP] 🚀 硬體安全關閉。" << std::endl;
}

} // extern "C"