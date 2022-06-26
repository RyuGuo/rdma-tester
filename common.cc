#include "common.h"

void open_device_and_port(ib_stat_s &ib_stat, int device_id, uint8_t ib_port,
                          uint8_t gid_idx, int num_thread, bool thread_local_cq,
                          int cqe_depth, size_t mr_size) {
  // open device and port
  ib_stat.device_list = ibv_get_device_list(&ib_stat.num_devices);
  e_assert(ib_stat.device_list != nullptr);
  e_assert(ib_stat.num_devices > 0);
  ib_stat.ib_ctx = ibv_open_device(ib_stat.device_list[device_id]);
  e_assert(ibv_query_device(ib_stat.ib_ctx, &ib_stat.device_attr) == 0);
  e_assert(ib_stat.ib_ctx != nullptr);
  e_assert(ibv_query_port(ib_stat.ib_ctx, ib_port, &ib_stat.port_attr) == 0);
  e_assert(ibv_query_gid(ib_stat.ib_ctx, ib_port, gid_idx, &ib_stat.gid) == 0);

  ib_stat.pd = ibv_alloc_pd(ib_stat.ib_ctx);
  e_assert(ib_stat.pd != nullptr);

  // create cqs
  int num_cqs = (thread_local_cq) ? num_thread : 1;
  for (int i = 0; i < num_cqs; ++i) {
    ibv_cq *cq = ibv_create_cq(ib_stat.ib_ctx, cqe_depth, nullptr, nullptr, 0);
    e_assert(cq != nullptr);
    ib_stat.cqs.push_back(cq);
  }

  // create mr
  void *_mr = malloc(mr_size);
  e_assert(_mr != nullptr);
  ib_stat.mr = ibv_reg_mr(ib_stat.pd, _mr, mr_size, ACCESS_FLAGS);
  e_assert(ib_stat.mr != nullptr);
}

ibv_qp *create_qp(ib_stat_s &ib_stat, uint32_t max_send_wr,
                  uint32_t max_recv_wr, uint32_t max_send_sge,
                  uint32_t max_recv_sge) {
  ibv_qp_init_attr qp_init_attr;
  clr_obj(qp_init_attr);
  qp_init_attr.qp_type = IBV_QPT_RC;
  qp_init_attr.sq_sig_all = 0;
  qp_init_attr.cap.max_send_wr = max_send_wr;
  qp_init_attr.cap.max_recv_wr = max_recv_wr;
  qp_init_attr.cap.max_send_sge = max_send_sge;
  qp_init_attr.cap.max_recv_sge = max_recv_sge;
  qp_init_attr.recv_cq = qp_init_attr.send_cq =
      ib_stat.cqs[rand() % ib_stat.cqs.size()];
  qp_init_attr.srq = ib_stat.srq;
  ibv_qp *qp = ibv_create_qp(ib_stat.pd, &qp_init_attr);
  e_assert(qp != nullptr);

  return qp;
}

void qp_init(QPHandle *handle, uint8_t ib_port) {
  ibv_qp_attr qp_attr;
  clr_obj(qp_attr);
  qp_attr.qp_state = IBV_QPS_INIT;
  qp_attr.pkey_index = 0;
  qp_attr.port_num = ib_port;
  qp_attr.qp_access_flags = ACCESS_FLAGS;
  e_assert(ibv_modify_qp(handle->qp, &qp_attr,
                         IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                             IBV_QP_ACCESS_FLAGS) == 0);
}

void qp_rtr_rts(QPHandle *handle, ib_stat_s &ib_stat, uint8_t ib_port) {
  ibv_qp_attr qp_attr;
  clr_obj(qp_attr);
  qp_attr.qp_state = IBV_QPS_RTR;
  qp_attr.path_mtu = IBV_MTU_256;
  qp_attr.ah_attr.dlid = handle->remote.lid;
  qp_attr.ah_attr.port_num = ib_port;
  qp_attr.dest_qp_num = handle->remote.qp_num;
  qp_attr.rq_psn = PSN;
  qp_attr.min_rnr_timer = 12;
  if (ib_stat.port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
    qp_attr.ah_attr.is_global = true; // ROCE
    qp_attr.ah_attr.grh.dgid = handle->remote.gid;
    qp_attr.ah_attr.grh.flow_label = 0;
    qp_attr.ah_attr.grh.hop_limit = 1;
    qp_attr.ah_attr.grh.sgid_index = handle->remote.gid_idx;
    qp_attr.ah_attr.grh.traffic_class = 0;
  }
  e_assert(ibv_modify_qp(handle->qp, &qp_attr,
                         IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_AV |
                             IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                             IBV_QP_MAX_DEST_RD_ATOMIC |
                             IBV_QP_MIN_RNR_TIMER) == 0);

  clr_obj(qp_attr);
  qp_attr.qp_state = IBV_QPS_RTS;
  qp_attr.timeout = 14;
  qp_attr.retry_cnt = 7;
  qp_attr.rnr_retry = 7;
  qp_attr.sq_psn = PSN;
  qp_attr.max_dest_rd_atomic = 1;
  e_assert(ibv_modify_qp(handle->qp, &qp_attr,
                         IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                             IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                             IBV_QP_MAX_QP_RD_ATOMIC) == 0);
}