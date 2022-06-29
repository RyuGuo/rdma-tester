#include "common.h"

using namespace std;

static vector<ibv_recv_wr> rrs;
static vector<ibv_sge> sges;

static void ready_for_recv_resource(MasterContext &ctx) {
  rrs.resize(ctx.option.max_recv_wr);
  sges.resize(ctx.option.max_recv_wr);

  for (int i = 0; i < ctx.option.max_recv_wr; ++i) {
    // align of 64B
    sges[i].addr = rand_pick_mr_addr((uint64_t)ctx.ib_stat.mr->addr,
                                     ctx.ib_stat.mr->length, ctx.max_payload) &
                   ~0x3fUL;
    sges[i].length = ctx.max_payload;
    sges[i].lkey = ctx.ib_stat.mr->lkey;
    rrs[i].sg_list = &sges[i];
    rrs[i].num_sge = 1;
    rrs[i].next = nullptr;
    rrs[i].wr_id = i;
  }
}

static void rdma_connect_listener(MasterContext &ctx) {
  ctx.listenfd = socket(AF_INET, SOCK_STREAM, 0);
  e_assert(ctx.listenfd != -1);
  sockaddr_in listen_addr;
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_addr.s_addr = inet_addr("0.0.0.0");
  listen_addr.sin_port = htons(ctx.option.port);
  int reuse = 1;
  e_assert(setsockopt(ctx.listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse,
                      sizeof(reuse)) == 0);
  e_assert(bind(ctx.listenfd, (sockaddr *)&listen_addr, sizeof(listen_addr)) ==
           0);
  assert(listen(ctx.listenfd, 10) == 0);

  fprintf(stdout, "Wait for Connect...\n");

  while (ctx.alive) {
    ssize_t n;
    sockaddr_in client_addr;
    socklen_t len = sizeof(sockaddr);
    int sockfd = accept(ctx.listenfd, (sockaddr *)&client_addr, &len);
    e_assert(sockfd != -1);

    fprintf(stdout, "Connect from %s ...\n", inet_ntoa(client_addr.sin_addr));

    while (ctx.alive) {
      QPConnectBufferStructure buf;

      // recv client qp info
      n = recv(sockfd, &buf, sizeof(buf), 0);
      assert_eq(n, buf.size(), "%lu");

      if (buf.type == QPConnectBufferStructure::INFO) {
        QPHandle *handle = new QPHandle();
        e_assert(handle != nullptr);

        handle->remote = buf.info;

        // connect local qp
        static int cqid_gen = 0;
        int cqid = (cqid_gen++) % ctx.ib_stat.cqs.size();
        handle->cqid = cqid;
        handle->qp = create_qp(ctx.ib_stat, cqid, ctx.option.max_send_wr,
                               ctx.option.max_recv_wr, ctx.option.max_send_sge,
                               ctx.option.max_recv_sge);

        qp_init(handle, ctx.option.ib_port);
        qp_rtr_rts(handle, ctx.ib_stat, ctx.option.ib_port);

        // send local qp info
        handle->local = {
            .gid = ctx.ib_stat.gid,
            .mr_addr = (uint64_t)ctx.ib_stat.mr->addr,
            .mr_size = ctx.ib_stat.mr->length,
            .qp_num = handle->qp->qp_num,
            .lid = ctx.ib_stat.port_attr.lid,
            .gid_idx = ctx.option.gid_idx,
            .rkey = ctx.ib_stat.mr->rkey,
            .is_pmem = ctx.use_pmem,
        };

        buf.type = QPConnectBufferStructure::INFO;
        buf.info = handle->local;
        n = send(sockfd, &buf, buf.size(), 0);
        e_assert(n == buf.size());

        // connect one peer ok
        ctx.handles.push_back(handle);
        ctx.handle_map.insert(make_pair(handle->qp->qp_num, handle));
      } else if (buf.type == QPConnectBufferStructure::COMPLETE) {
        ctx.max_payload = max(ctx.max_payload, buf.payload);

        if (ctx.ready_for_resource) {
          ready_for_recv_resource(ctx);
        }

        // poll client qps
        ibv_recv_wr *bad_wr;
        for (int i = 0; i < ctx.option.num_recv_wr_per_qp; ++i) {
          if (ctx.option.use_srq)
            assert(ibv_post_srq_recv(ctx.ib_stat.srq, &rrs[rand() % rrs.size()],
                                     &bad_wr) == 0);
          else
            assert(ibv_post_recv(ctx.handles.back()->qp,
                                 &rrs[rand() % rrs.size()], &bad_wr) == 0);
        }
        close(sockfd);
        ctx.ready_for_resource = false;
        break;
      }
    }
    fprintf(stdout, "Connect from %s success\n",
            inet_ntoa(client_addr.sin_addr));
  }
}

static void poll_worker(MasterContext &ctx, int tid) {
  ibv_wc *wcs = new ibv_wc[ctx.option.num_poll_entries];
  ibv_send_wr wr;
  clr_obj(wr);
  wr.num_sge = 0;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.send_flags = IBV_SEND_SIGNALED;
  ibv_send_wr *bad_wr;
  ibv_recv_wr *bad_rr;

  while (ctx.alive) {
    int n = ibv_poll_cq(ctx.ib_stat.cqs[tid % ctx.ib_stat.cqs.size()],
                        ctx.option.num_poll_entries, wcs);
    for (int i = 0; i < n; ++i) {
      if (wcs[i].status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Poll CQ Error: status: %d\n", wcs[i].status);
      } else {
        if ((wcs[i].opcode & IBV_WC_RECV) == 0) {
          continue;
        }
#ifdef USE_PMEM
        if (ctx.use_pmem) {
          void *clp;
          if (wcs[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
            clp = (void *)((uint64_t)ctx.ib_stat.mr->addr + wcs[i].imm_data);
          } else {
            clp = (void *)rrs[wcs[i].wr_id].sg_list->addr;
          }
          pmem_persist(clp, wcs[i].byte_len);
          // repeat reply
          wr.imm_data = wcs[i].qp_num;
          e_assert(ibv_post_send(ctx.handle_map[wcs[i].qp_num]->qp, &wr,
                                 &bad_wr) == 0);
        }
#endif // USE_PMEM
        if (ctx.option.use_srq)
          e_assert(ibv_post_srq_recv(ctx.ib_stat.srq, &rrs[wcs[i].wr_id],
                                     &bad_rr) == 0);
        else
          e_assert(ibv_post_recv(ctx.handle_map[wcs[i].qp_num]->qp,
                                 &rrs[wcs[i].wr_id], &bad_rr) == 0);
      }
    }
  }

  delete[] wcs;
}

MasterContext::MasterContext(MasterOption &option)
    : option(option), ready_for_resource(true) {
  srand(time(nullptr));

  open_device_and_port(ib_stat, option.device_id, option.ib_port,
                       option.gid_idx, option.num_thread,
                       option.thread_local_cq, option.cqe_depth, option.mr_size,
                       option.pmem_dev_path, &use_pmem);

  // create srq
  if (option.use_srq) {
    ibv_srq_init_attr init_attr;
    clr_obj(init_attr);
    init_attr.attr.max_wr = option.max_wr;
    init_attr.attr.max_sge = option.max_sge;
    init_attr.attr.srq_limit = option.srq_limit;
    ib_stat.srq = ibv_create_srq(ib_stat.pd, &init_attr);
    e_assert(ib_stat.srq != nullptr);
  }

  alive = true;

  // tcp listen
  listen_th = thread(rdma_connect_listener, std::ref(*this));

  // cq poll
  for (int i = 0; i < option.num_thread; ++i) {
    poll_th.push_back(thread(poll_worker, std::ref(*this), i));
  }
}

MasterContext::~MasterContext() {
  alive = false;
  for (int i = 0; i < option.num_thread; ++i) {
    poll_th[i].join();
  }
  listen_th.join();
}