#include "common.h"

using namespace std;

ClientContext::ClientContext(ClientOption &option) : option(option) {
  srand(time(nullptr));

  open_device_and_port(ib_stat, option.device_id, option.ib_port,
                       option.gid_idx, option.num_thread,
                       option.thread_local_cq, option.cqe_depth, option.mr_size,
                       option.pmem_dev_path, &use_pmem);

  // connect to masters
  num_remote = option.master_ip.size();
  for (int i = 0; i < num_remote; ++i) {
    QPConnectBufferStructure buf;
    ssize_t n;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    e_assert(sockfd != -1);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(option.master_ip[i].c_str());
    addr.sin_port = htons(option.port);
    e_assert(connect(sockfd, (sockaddr *)&addr, sizeof(addr)) == 0);
    fprintf(stdout, "Connect to %s:%d ...\n", option.master_ip[i].c_str(),
            option.port);
    for (int j = 0; j < option.num_qp_per_mac; ++j) {
      QPHandle *handle = new QPHandle();
      e_assert(handle != nullptr);

      static int cqid_gen = 0;
      int cqid = (cqid_gen++) % ib_stat.cqs.size();
      handle->cqid = cqid;
      handle->qp =
          create_qp(ib_stat, cqid, option.max_send_wr, option.max_recv_wr,
                    option.max_send_sge, option.max_recv_sge);

      qp_init(handle, option.ib_port);

      // send local qp info
      handle->local = {
          .gid = ib_stat.gid,
          .mr_addr = (uint64_t)ib_stat.mr->addr,
          .mr_size = ib_stat.mr->length,
          .qp_num = handle->qp->qp_num,
          .lid = ib_stat.port_attr.lid,
          .gid_idx = option.gid_idx,
          .rkey = ib_stat.mr->rkey,
          .is_pmem = use_pmem,
      };

      buf.type = QPConnectBufferStructure::INFO;
      buf.info = handle->local;
      n = send(sockfd, &buf, buf.size(), 0);
      assert_eq(n, buf.size(), "%lu");

      // recv client qp info
      n = recv(sockfd, &buf, sizeof(buf), 0);
      e_assert(n == buf.size());

      handle->remote = buf.info;

      // connect local qp
      qp_rtr_rts(handle, ib_stat, option.ib_port);

      // connect one peer ok
      handles.push_back(handle);
    }

    buf.type = QPConnectBufferStructure::COMPLETE;
    buf.payload = option.payload;
    n = send(sockfd, &buf, buf.size(), 0);
    e_assert(n == buf.size());

    fprintf(stdout, "Connect to %s:%d success\n", option.master_ip[i].c_str(),
            option.port);

    // qp info exchange ok
    close(sockfd);
  }
}

ClientContext::~ClientContext() {}

TestResult start_test(ClientContext *ctx, TestOption &option) {
  TestResult result;
  result.option = option;
  timeval start_time, end_time;
  bool test_over = false;
  pthread_barrier_t b;
  pthread_barrier_init(&b, nullptr, ctx->option.num_thread + 1);

  // ready for test resource
  struct __ {
    uint64_t c = 0;
    uint64_t padding[7];
  };
  vector<__> complete_cnt(ctx->option.num_thread);
  // sge [handle][sge list]
  vector<vector<ibv_sge>> v_sge(ctx->handles.size());
  // wr [handle][wr list]
  vector<vector<ibv_send_wr>> v_wr(ctx->handles.size());
  // rr [handle][rr list]
  vector<vector<ibv_recv_wr>> v_rr(ctx->handles.size());
  for (int i = 0; i < ctx->handles.size(); ++i) {
    auto &sges = v_sge[i];
    auto &wrs = v_wr[i];
    auto &rrs = v_rr[i];
    sges.resize(option.post_list);
    wrs.resize(option.post_list);
    rrs.resize(option.post_list);

    for (int j = 0; j < option.post_list; ++j) {
      auto &sge = sges[j];
      auto &wr = wrs[j];
      auto &rr = rrs[j];
      clr_obj(sge);
      clr_obj(wr);
      clr_obj(rr);
      sge.addr = rand_pick_mr_addr((uint64_t)ctx->ib_stat.mr->addr,
                                   ctx->ib_stat.mr->length, option.payload);
      sge.length = option.payload;
      sge.lkey = ctx->ib_stat.mr->lkey;
      wr.sg_list = &sge;
      wr.num_sge = 1;
      wr.next = &wrs[j + 1];
      wr.wr.rdma = {
          .remote_addr = rand_pick_mr_addr(ctx->handles[i]->remote.mr_addr,
                                           ctx->handles[i]->remote.mr_size,
                                           option.payload),
          .rkey = ctx->handles[i]->remote.rkey,
      };

      rr.num_sge = 0;
      rr.next = &rrs[j + 1];

      if (!ctx->handles[i]->remote.is_pmem) {
        switch (option.type) {
        case TestOption::WRITE:
          wr.opcode = IBV_WR_RDMA_WRITE;
          break;
        case TestOption::WRITE_WITH_IMM:
          wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
          break;
        case TestOption::READ:
          wr.opcode = IBV_WR_RDMA_READ;
          break;
        case TestOption::SEND:
          wr.opcode = IBV_WR_SEND;
          break;
        case TestOption::CAS:
          wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
          wr.wr.atomic.remote_addr =
              rand_pick_mr_addr(ctx->handles[i]->remote.mr_addr,
                                ctx->handles[i]->remote.mr_size,
                                option.payload) &
              ~0x7UL;
          wr.wr.atomic.rkey = ctx->handles[i]->remote.rkey;
          wr.wr.atomic.compare_add = 0;
          wr.wr.atomic.swap = 0;
          break;
        case TestOption::FETCH_ADD:
          wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
          wr.wr.atomic.remote_addr =
              rand_pick_mr_addr(ctx->handles[i]->remote.mr_addr,
                                ctx->handles[i]->remote.mr_size,
                                option.payload) &
              ~0x7UL;
          wr.wr.atomic.rkey = ctx->handles[i]->remote.rkey;
          wr.wr.atomic.compare_add = 1;
          break;
        default:
          fprintf(stderr, "Invalid Test Type: %d\n", option.type);
          exit(1);
        }
      } else {
        switch (option.type) {
        case TestOption::WRITE_WITH_IMM:
          wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
          // align of 64B
          wr.wr.rdma.remote_addr &= ~0x3fUL;
          // write offset
          wr.imm_data =
              wr.wr.rdma.remote_addr - ctx->handles[i]->remote.mr_addr;
          break;
        case TestOption::READ:
          wr.opcode = IBV_WR_RDMA_READ;
          break;
        case TestOption::SEND:
          wr.opcode = IBV_WR_SEND;
          break;
        default:
          fprintf(stderr, "Invalid Test Type: %d\n", option.type);
          exit(1);
        }
      }
    }
    wrs.back().send_flags = IBV_SEND_SIGNALED;
    wrs.back().next = nullptr;
    rrs.back().next = nullptr;
  }

  // resource [cq][handle]
  struct __resource_t {
    QPHandle *handle;
    vector<ibv_send_wr> *wrs;
    vector<ibv_recv_wr> *rrs;
  };
  vector<vector<__resource_t>> resource_th(ctx->ib_stat.cqs.size());
  for (int j = 0; j < ctx->handles.size(); ++j) {
    resource_th[ctx->handles[j]->cqid].push_back(
        __resource_t{ctx->handles[j], &v_wr[j], &v_rr[j]});
  }

  // sync point

  // start test threads
  vector<thread> test_th;
  for (int tid = 0; tid < ctx->option.num_thread; ++tid) {
    test_th.push_back(thread([&test_over, tid, &ctx, &b, &start_time, &end_time,
                              &complete_cnt, &resource_th, &option]() {
      int cqid = tid % ctx->ib_stat.cqs.size();
      auto &resource = resource_th[cqid];
      ibv_send_wr *bad_wr;
      ibv_recv_wr *bad_rr;
      ibv_wc *wcs = new ibv_wc[ctx->option.num_poll_entries];
      unordered_map<uint32_t, int> batch_recv_cnt_map;
      for (int i = 0; i < resource.size(); ++i) {
        resource[i].wrs->back().wr_id = i;
        for (int j = 0; j < resource[i].rrs->size(); ++j) {
          resource[i].rrs->at(j).wr_id = i;
        }
      }

      // local thread sync point
      pthread_barrier_wait(&b);
      gettimeofday(&start_time, nullptr);

      for (int i = 0; i < resource.size(); ++i) {
        e_assert(ibv_post_send(resource[i].handle->qp,
                               &resource[i].wrs->front(), &bad_wr) == 0);
        if (resource[i].handle->remote.is_pmem &&
            option.type != TestOption::READ) {
          e_assert(ibv_post_recv(resource[i].handle->qp,
                                 &resource[i].rrs->front(), &bad_rr) == 0);
        }
      }
      while (!test_over) {
        int n = ibv_poll_cq(ctx->ib_stat.cqs[cqid],
                            ctx->option.num_poll_entries, wcs);
        for (int i = 0; i < n; ++i) {
          if (wcs[i].status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Poll CQ Error: status: %d\n", wcs[i].status);
          } else {
            uint64_t wid = wcs[i].wr_id;
            bool is_pmem = resource[wid].handle->remote.is_pmem;
            bool pmem_exec = is_pmem && option.type != TestOption::READ;
            if (pmem_exec && (wcs[i].opcode & IBV_WC_RECV) == 0) {
              continue;
            }

            ++complete_cnt[tid].c;

            if (pmem_exec) {
              // wait for post num recv
              int rc = ++batch_recv_cnt_map[wcs[i].imm_data];
              if (rc == option.post_list) {
                batch_recv_cnt_map[wcs[i].imm_data] = 0;
              } else {
                continue;
              }
            }

            e_assert(ibv_post_send(resource[wid].handle->qp,
                                   &resource[wid].wrs->front(), &bad_wr) == 0);
            if (pmem_exec) {
              e_assert(ibv_post_recv(resource[wid].handle->qp,
                                     &resource[wid].rrs->front(),
                                     &bad_rr) == 0);
            }
          }
        }
      }
      delete[] wcs;

      // test end
      gettimeofday(&end_time, nullptr);
    }));
  }

  pthread_barrier_wait(&b);
  uint64_t last_c = 0, c;
  timeval last_time;
  gettimeofday(&last_time, nullptr);
  for (uint32_t d = 0; d < option.duration_s; ++d) {
    usleep(1000000);
    timeval now_time;
    gettimeofday(&now_time, nullptr);
    uint64_t c = vector_sum(complete_cnt, .c) - last_c;
    uint64_t diff = timeval_diff(now_time, last_time);
    result.cnt_duration.push_back(c);
    fprintf(stdout, "Thoughput: %f Mops\n", 1.0 * c / diff);
    last_c += c;
    last_time = now_time;
  }
  pthread_barrier_destroy(&b);
  test_over = true;
  for (int tid = 0; tid < ctx->option.num_thread; ++tid) {
    test_th[tid].join();
  }

  uint64_t diff = timeval_diff(end_time, start_time);

  result.cnt = vector_sum(complete_cnt, .c);
  result.avg_latency_us = 1.0 * diff / result.cnt;
  result.avg_throughput_Mops = 1.0 * result.cnt / diff;

  return result;
}