#ifndef __COMMON_H__
#define __COMMON_H__

#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <errno.h>
#include <infiniband/verbs.h>
#include <mutex>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define DEFAULT_PORT 8989

struct MasterOption {
  uint16_t port = DEFAULT_PORT;

  int device_id = 0;
  uint8_t ib_port = 1;
  uint8_t gid_idx = 1;
  int cqe_depth = 100;

  int num_thread = 1;
  bool thread_local_cq = true;
  size_t mr_size = 1L << 20;
  int num_poll_entries = 16;

  bool use_srq = false;
  uint32_t max_wr = 16;
  uint32_t max_sge = 1;
  uint32_t srq_limit = 4;

  uint32_t max_send_wr = 100;
  uint32_t max_recv_wr = 100;
  uint32_t max_send_sge = 1;
  uint32_t max_recv_sge = 1;
};

struct ClientOption {
  std::vector<std::string> master_ip;
  uint16_t port = DEFAULT_PORT;

  int device_id = 0;
  uint8_t ib_port = 1;
  uint8_t gid_idx = 1;
  int cqe_depth = 100;
  int num_qp_per_mac = 1;
  int num_poll_entries = 16;

  int num_thread = 1;
  bool thread_local_cq = true;
  size_t mr_size = 1L << 20;

  uint32_t max_send_wr = 100;
  uint32_t max_recv_wr = 100;
  uint32_t max_send_sge = 1;
  uint32_t max_recv_sge = 1;
};

struct TestOption {
  enum TestType {
    WRITE,
    READ,
    SEND,
    WRITE_WITH_IMM,
    CAS,
    FETCH_ADD,
  };

  TestType type;
  uint32_t duration_s = 10;
  int post_list = 1;
  size_t payload = 64;
};

struct TestResult {
  TestOption option;
  std::vector<double> latency_us;
  std::vector<double> throughput_Mops;
  double avg_latency_us;
  double avg_throughput_Mops;
};

struct QPHandle {
  ibv_qp *qp;
  int cqid;

  struct peer_info_s {
    ibv_gid gid;
    uint64_t mr_addr;
    size_t mr_size;
    uint32_t qp_num;
    uint16_t lid;
    uint8_t gid_idx;
    uint32_t rkey;
    size_t payload;
  };

  peer_info_s local;
  peer_info_s remote;
};

struct ib_stat_s {
  int num_devices;
  ibv_device **device_list;
  ibv_device_attr device_attr;
  ibv_context *ib_ctx;
  ibv_port_attr port_attr;
  ibv_gid gid;
  ibv_pd *pd;
  ibv_mr *mr;
  ibv_srq *srq;
  std::vector<ibv_cq *> cqs;
};

struct MasterContext {
  MasterOption option;
  ib_stat_s ib_stat;

  std::thread listen_th;

  int listenfd;
  std::vector<std::thread> poll_th;
  std::vector<QPHandle *> handles;

  size_t max_payload;

  bool alive;

  MasterContext(MasterOption &option);
  ~MasterContext();
};

struct ClientContext {
  ClientOption option;
  ib_stat_s ib_stat;

  int num_remote;
  int num_handles;
  std::vector<std::vector<QPHandle *>> handles;

  ClientContext(ClientOption &option);
  ~ClientContext();
};

#define PSN 0
#define ACCESS_FLAGS                                                           \
  (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | \
   IBV_ACCESS_REMOTE_ATOMIC)

#define clr_obj(o) memset(&o, 0, sizeof(o))
#define e_assert(expr)                                                         \
  (static_cast<bool>(expr) ? void(0) : ({                                      \
    perror("");                                                                \
    __assert_fail(#expr, __FILE__, __LINE__, __ASSERT_FUNCTION);               \
  }))
#define timeval_diff(e, s)                                                     \
  ((e).tv_sec - (s).tv_sec * 1000000 + (e).tv_usec - (s).tv_usec)
#define vector_sum(v, field)                                                   \
  ({                                                                           \
    decltype((v)[0].field) sum = 0;                                            \
    for (auto &item : v)                                                       \
      sum += item.field;                                                       \
    sum;                                                                       \
  })

template <typename T>
std::vector<T> flat(std::vector<std::vector<T>> &v) {
  std::vector<T> r;
  for (auto &vv : v) {
    r.insert(r.end(), vv.begin(), vv.end());
  }
  return r;
}

void open_device_and_port(ib_stat_s &ib_stat, int device_id, uint8_t ib_port,
                          uint8_t gid_idx, int num_thread, bool thread_local_cq,
                          int cqe_depth, size_t mr_size);

ibv_qp *create_qp(ib_stat_s &ib_stat, int cqid, uint32_t max_send_wr,
                  uint32_t max_recv_wr, uint32_t max_send_sge,
                  uint32_t max_recv_sge);
void qp_init(QPHandle *handle, uint8_t ib_port);
void qp_rtr_rts(QPHandle *handle, ib_stat_s &ib_stat, uint8_t ib_port);

TestResult start_test(ClientContext *ctx, TestOption &option);
uint64_t rand_pick_mr_addr(uint64_t mr_addr, size_t mr_size, size_t payload);

#endif // __COMMON_H__