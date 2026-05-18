#pragma once

// Header-only ICMP pinger wrapping ESP-IDF esp_ping_* (lwip ping_sock).
//
// Usage (lambda callback):
//   Pinger p;                       // default target 8.8.8.8
//   p.onResult([](uint32_t tx, uint32_t rx, uint32_t loss, uint32_t dur){
//       // tx packets sent, rx received, loss percent 0..100, dur ms
//   });
//   p.start();                      // single-shot; caller schedules repeats
//
// Or with a free function:
//
//   void onPingDone(uint32_t tx, uint32_t rx, uint32_t loss, uint32_t dur) {
//       // tx packets sent, rx received, loss percent 0..100, dur ms
//       log_i("ping %lu/%lu rx, %lu%% loss, %lu ms",
//             (unsigned long)rx, (unsigned long)tx,
//             (unsigned long)loss, (unsigned long)dur);
//   }
//
//   Pinger p;
//   p.onResult(onPingDone);
//   p.start();
//
// Target is dotted-quad IPv4 only. No DNS.
//
// NOTE: ResultCb is invoked from the ESP-IDF ping task, NOT the Arduino
// loop() context. Keep the callback short and reentrant-safe (no Serial
// printing contention, no heavy work). If you need main-loop dispatch, set
// a flag in the callback and handle it from loop().

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <functional>

#include "lwip/inet.h"
#include "ping/ping_sock.h"

class Pinger {
public:
    using ResultCb = std::function<void(uint32_t tx, uint32_t rx,
                                        uint32_t loss, uint32_t dur)>;

    explicit Pinger(const char* target = "8.8.8.8") : _target(target) {}

    ~Pinger() { stop(); }

    Pinger(const Pinger&) = delete;
    Pinger& operator=(const Pinger&) = delete;

    void onResult(ResultCb cb) { _cb = std::move(cb); }

    void setTarget(const char* target) { _target = target; }

    bool busy() const { return _running.load(); }

    bool start(uint32_t count = 4, uint32_t interval_ms = 1000,
               uint32_t timeout_ms = 1000) {
        bool expected = false;
        if (!_running.compare_exchange_strong(expected, true)) {
            return false;
        }

        ip_addr_t dst{};
        if (!resolve(_target.c_str(), dst)) {
            log_w("Pinger: resolve failed for %s", _target.c_str());
            _running = false;
            return false;
        }

        esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
        cfg.target_addr = dst;
        cfg.count       = count;
        cfg.interval_ms = interval_ms;
        cfg.timeout_ms  = timeout_ms;

        esp_ping_callbacks_t cbs = {
            .cb_args          = this,
            .on_ping_success  = &Pinger::on_success_tr,
            .on_ping_timeout  = &Pinger::on_timeout_tr,
            .on_ping_end      = &Pinger::on_end_tr,
        };

        if (esp_ping_new_session(&cfg, &cbs, &_handle) != ESP_OK) {
            log_e("Pinger: esp_ping_new_session failed");
            _handle  = nullptr;
            _running = false;
            return false;
        }
        if (esp_ping_start(_handle) != ESP_OK) {
            log_e("Pinger: esp_ping_start failed");
            esp_ping_delete_session(_handle);
            _handle  = nullptr;
            _running = false;
            return false;
        }
        return true;
    }

    void stop() {
        if (_handle) {
            esp_ping_stop(_handle);
            esp_ping_delete_session(_handle);
            _handle = nullptr;
        }
        _running = false;
    }

private:
    static bool resolve(const char* host, ip_addr_t& out) {
        ip4_addr_t v4{};
        if (!ip4addr_aton(host, &v4)) {
            return false;
        }
        out.type       = IPADDR_TYPE_V4;
        out.u_addr.ip4 = v4;
        return true;
    }

    static void on_success_tr(esp_ping_handle_t hdl, void* args) {
        uint16_t seq;
        uint8_t  ttl;
        uint32_t t, len;
        ip_addr_t ip;
        esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO,   &seq, sizeof(seq));
        esp_ping_get_profile(hdl, ESP_PING_PROF_TTL,     &ttl, sizeof(ttl));
        esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &t,   sizeof(t));
        esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE,    &len, sizeof(len));
        esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR,  &ip,  sizeof(ip));
        log_d("Pinger: %lu bytes from %s seq=%u ttl=%u time=%lu ms",
              (unsigned long)len, ipaddr_ntoa(&ip), seq, ttl,
              (unsigned long)t);
        (void)args;
    }

    static void on_timeout_tr(esp_ping_handle_t hdl, void* args) {
        uint16_t seq;
        ip_addr_t ip;
        esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO,  &seq, sizeof(seq));
        esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &ip,  sizeof(ip));
        log_d("Pinger: timeout seq=%u from %s", seq, ipaddr_ntoa(&ip));
        (void)args;
    }

    static void on_end_tr(esp_ping_handle_t hdl, void* args) {
        auto* self = static_cast<Pinger*>(args);
        uint32_t tx = 0, rx = 0, dur = 0;
        esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST,  &tx,  sizeof(tx));
        esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY,    &rx,  sizeof(rx));
        esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &dur, sizeof(dur));
        uint32_t loss = tx ? (100 * (tx - rx) / tx) : 0;

        if (self && self->_cb) {
            self->_cb(tx, rx, loss, dur);
        }
        esp_ping_delete_session(hdl);
        if (self) {
            self->_handle  = nullptr;
            self->_running = false;
        }
    }

    String              _target;
    esp_ping_handle_t   _handle{nullptr};
    std::atomic<bool>   _running{false};
    ResultCb            _cb;
};
