#include "common.h"
#include <arpa/inet.h>
#include <cassert>
#include <infiniband/verbs.h>

using namespace std;

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
    int n;
    sockaddr_in client_addr;
    socklen_t len = sizeof(sockaddr);
    int sockfd = accept(ctx.listenfd, (sockaddr *)&client_addr, &len);
    e_assert(sockfd != -1);

    fprintf(stdout, "Connect from %s ...\n", inet_ntoa(client_addr.sin_addr));

    QPHandle *handle = new QPHandle();
    e_assert(handle != nullptr);

    // recv client qp info
    n = recv(sockfd, &handle->remote, sizeof(handle->remote), 0);
    e_assert(n == sizeof(handle->remote));

    // connect local qp
    handle->qp =
        create_qp(ctx.ib_stat, ctx.option.max_send_wr, ctx.option.max_recv_wr,
                  ctx.option.max_send_sge, ctx.option.max_recv_sge);

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
    };
    n = send(sockfd, &handle->local, sizeof(handle->local), 0);
    e_assert(n == sizeof(handle->local));

    // connect one peer ok
    ctx.handles.push_back(handle);
    
    fprintf(stdout, "Connect from %s success\n",
            inet_ntoa(client_addr.sin_addr));
  }
}

static void poll_worker(MasterContext &ctx) {
  while (ctx.alive) {
  }
}

MasterContext::MasterContext(MasterOption &option) : option(option) {
  srand(time(nullptr));

  open_device_and_port(ib_stat, option.device_id, option.ib_port,
                       option.gid_idx, option.num_thread,
                       option.thread_local_cq, option.cqe_depth,
                       option.mr_size);

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
    poll_th.push_back(thread(poll_worker, std::ref(*this)));
  }
}

MasterContext::~MasterContext() { alive = false; }