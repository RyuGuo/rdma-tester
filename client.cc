#include "common.h"

ClientContext::ClientContext(ClientOption &option) : option(option) {
  srand(time(nullptr));

  open_device_and_port(ib_stat, option.device_id, option.ib_port,
                       option.gid_idx, option.num_thread,
                       option.thread_local_cq, option.cqe_depth,
                       option.mr_size);

  // connect to masters
  handles.resize(option.master_ip.size());
  for (int i = 0; i < option.master_ip.size(); ++i) {
    for (int j = 0; j < option.qp_num_per_mac; ++j) {
      int n;
      int sockfd = socket(AF_INET, SOCK_STREAM, 0);
      e_assert(sockfd != -1);
      sockaddr_in addr;
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = inet_addr(option.master_ip[i].c_str());
      addr.sin_port = htons(option.port);
      e_assert(connect(sockfd, (sockaddr *)&addr, sizeof(addr)) == 0);

      fprintf(stdout, "Connect to %s:%d ...\n", option.master_ip[i].c_str(),
              option.port);

      QPHandle *handle = new QPHandle();
      e_assert(handle != nullptr);

      handle->qp = create_qp(ib_stat, option.max_send_wr, option.max_recv_wr,
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
      };
      n = send(sockfd, &handle->local, sizeof(handle->local), 0);
      e_assert(n == sizeof(handle->local));

      // recv client qp info
      n = recv(sockfd, &handle->remote, sizeof(handle->remote), 0);
      e_assert(n == sizeof(handle->remote));

      // connect local qp
      qp_rtr_rts(handle, ib_stat, option.ib_port);

      // connect one peer ok
      handles[i].push_back(handle);

      // qp info exchange ok
      close(sockfd);

      fprintf(stdout, "Connect to %s:%d success\n", option.master_ip[i].c_str(),
              option.port);
    }
  }
}

ClientContext::~ClientContext() {}