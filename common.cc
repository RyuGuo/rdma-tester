#include "common.h"
#include <cstdlib>
#include <infiniband/verbs.h>

uint64_t rand_pick_mr_addr(uint64_t mr_addr, size_t mr_size, size_t payload) {
  return mr_addr + rand() % (mr_size - payload);
}

void open_device_and_port(ib_stat_s &ib_stat, int device_id, uint8_t ib_port,
                          uint8_t gid_idx, int num_thread, bool thread_local_cq,
                          int cqe_depth, size_t mr_size,
                          std::string &pmem_dev_path, bool *is_pmemp,
                          bool use_dm) {
  // open device and port
  ibv_query_device_ex_input input;
  input.comp_mask = 0;
  ib_stat.device_list = ibv_get_device_list(&ib_stat.num_devices);
  e_assert(ib_stat.device_list != nullptr);
  e_assert(ib_stat.num_devices > 0);
  ib_stat.ib_ctx = ibv_open_device(ib_stat.device_list[device_id]);
  e_assert(ib_stat.ib_ctx != nullptr);
  e_assert(ibv_query_device_ex(ib_stat.ib_ctx, &input,
                               &ib_stat.device_attr_ex) == 0);
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
  void *_mr;
  if (use_dm) {
    assert_expr(mr_size, <=, ib_stat.device_attr_ex.max_dm_size, "%lu");
    ibv_alloc_dm_attr dm_attr;
    clr_obj(dm_attr);
    dm_attr.length = mr_size;
    dm_attr.log_align_req = 3;
    ibv_dm *dm = ibv_alloc_dm(ib_stat.ib_ctx, &dm_attr);
    e_assert(dm != nullptr);
    ib_stat.mr = ibv_reg_dm_mr(ib_stat.pd, dm, 0, mr_size,
                               ACCESS_FLAGS | IBV_ACCESS_ZERO_BASED);
    e_assert(ib_stat.mr != nullptr);
    *is_pmemp = false;
  } else {
#ifndef USE_PMEM
    _mr = aligned_alloc(4096, mr_size);
    *is_pmemp = false;
#else
    if (pmem_dev_path.size() == 0) {
      _mr = malloc(mr_size);
      *is_pmemp = false;
    } else {
      size_t mapped_len = 0;
      int is_pmem = 0;
      _mr = pmem_map_file(pmem_dev_path.c_str(), 0, PMEM_FILE_CREATE, 0600,
                          &mapped_len, &is_pmem);
      e_assert(is_pmem == 1);
      e_assert(mapped_len >= mr_size);
      *is_pmemp = true;
    }
#endif // USE_PMEM
    e_assert(_mr != nullptr);
    memset(_mr, 0, mr_size);
    ib_stat.mr = ibv_reg_mr(ib_stat.pd, _mr, mr_size, ACCESS_FLAGS);
    e_assert(ib_stat.mr != nullptr);
  }
}

ibv_qp *create_qp(ib_stat_s &ib_stat, int cqid, QPType qp_type,
                  uint32_t max_send_wr, uint32_t max_recv_wr,
                  uint32_t max_send_sge, uint32_t max_recv_sge) {
  ibv_qp_init_attr qp_init_attr;
  clr_obj(qp_init_attr);
  qp_init_attr.qp_type = (qp_type == RC) ? IBV_QPT_RC : IBV_QPT_UD;
  qp_init_attr.sq_sig_all = 0;
  qp_init_attr.cap.max_send_wr = max_send_wr;
  qp_init_attr.cap.max_recv_wr = max_recv_wr;
  qp_init_attr.cap.max_send_sge = max_send_sge;
  qp_init_attr.cap.max_recv_sge = max_recv_sge;
  qp_init_attr.recv_cq = qp_init_attr.send_cq = ib_stat.cqs[cqid];
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

  switch (handle->qp->qp_type) {
  case IBV_QPT_RC:
    qp_attr.qp_access_flags = ACCESS_FLAGS;
    e_assert(ibv_modify_qp(handle->qp, &qp_attr,
                           IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                               IBV_QP_ACCESS_FLAGS) == 0);
    break;
  case IBV_QPT_UD:
    qp_attr.qkey = QKEY;
    e_assert(ibv_modify_qp(handle->qp, &qp_attr,
                           IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_QKEY |
                               IBV_QP_PORT) == 0);
    break;
  default:
    fprintf(stderr, "Invalid QP Type: %d\n", handle->qp->qp_type);
    exit(1);
  }
}

void qp_rtr_rts(QPHandle *handle, ib_stat_s &ib_stat, uint8_t ib_port) {
  ibv_qp_attr qp_attr;
  ibv_ah_attr ah_attr;
  clr_obj(qp_attr);
  clr_obj(ah_attr);

  ah_attr.dlid = handle->remote.lid;
  ah_attr.port_num = ib_port;
  if (ib_stat.port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
    ah_attr.is_global = true; // RoCE
    ah_attr.grh.dgid = handle->remote.gid;
    ah_attr.grh.flow_label = 0;
    ah_attr.grh.hop_limit = 1;
    ah_attr.grh.sgid_index = handle->remote.gid_idx;
    ah_attr.grh.traffic_class = 0;
  }

  switch (handle->qp->qp_type) {
  case IBV_QPT_RC:
    qp_attr.qp_state = IBV_QPS_RTR;
    qp_attr.path_mtu = IBV_MTU_512;
    qp_attr.dest_qp_num = handle->remote.qp_num;
    qp_attr.rq_psn = PSN;
    qp_attr.min_rnr_timer = 12;
    qp_attr.max_dest_rd_atomic = 8;
    qp_attr.ah_attr = ah_attr;
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
    qp_attr.max_rd_atomic = 8;
    e_assert(ibv_modify_qp(handle->qp, &qp_attr,
                           IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                               IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                               IBV_QP_MAX_QP_RD_ATOMIC) == 0);
    break;
  case IBV_QPT_UD:
    qp_attr.qp_state = IBV_QPS_RTR;
    qp_attr.path_mtu = IBV_MTU_512;

    e_assert(ibv_modify_qp(handle->qp, &qp_attr, IBV_QP_STATE) == 0);

    clr_obj(qp_attr);
    qp_attr.qp_state = IBV_QPS_RTS;
    qp_attr.sq_psn = PSN;
    e_assert(
        ibv_modify_qp(handle->qp, &qp_attr, IBV_QP_STATE | IBV_QP_SQ_PSN) == 0);

    handle->ah = ibv_create_ah(handle->qp->pd, &ah_attr);
    e_assert(handle->ah != nullptr);
    break;
  default:
    fprintf(stderr, "Invalid QP Type: %d\n", handle->qp->qp_type);
    exit(1);
  }
}