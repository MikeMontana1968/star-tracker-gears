// =====================================================================
//  LogRing -- the last N log lines in RAM, each with a sequence number,
//  for GET /api/log?since=N (FIRMWARE.md s7). A client remembers the
//  `next` it was given and asks again from there; lines that fell off the
//  ring while it was away are counted, not silently skipped.
//  Header-only, no Arduino: host-tested in test/test_net.
// =====================================================================
#pragma once
#include <stdint.h>
#include <string.h>

namespace net {

class LogRing {
 public:
  static constexpr int N = 64, LEN = 112;
  struct Entry { uint32_t seq; int64_t utc; char msg[LEN]; };

  void add(int64_t utc, const char *msg) {
    Entry &e = e_[next_ % N];
    e.seq = next_++;
    e.utc = utc;
    strncpy(e.msg, msg, LEN - 1);
    e.msg[LEN - 1] = 0;
  }
  uint32_t next() const { return next_; }           // the seq the next line will get
  uint32_t oldest() const { return next_ > (uint32_t)N ? next_ - N : 1; }

  // calls f(entry) for every line with seq >= since, oldest first; returns
  // how many asked-for lines are already gone
  template <class F> uint32_t since(uint32_t since, F f) const {
    uint32_t from = since < oldest() ? oldest() : since, lost = from - (since ? since : 1);
    if (since == 0) lost = 0;
    for (uint32_t s = from; s < next_; s++) f(e_[s % N]);
    return lost;
  }

 private:
  Entry e_[N] = {};
  uint32_t next_ = 1;
};

}  // namespace net
