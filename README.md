# RDMA Tester

`rdma_test.cc` is a simple test benchmark for RDMA.

```shell
mkdir build
cmake ..
make
```

For master: `./rdma_test`

For client: `./rdma_test <master_ip> ...`

## Feature

* Multiple Master Test

  string[] `ClientOption::master_ip`

* Persistent Memory

  macro `-DUSE_PMEM`

  string `Option::pmem_dev_path`
  
* DDIO

  bool `Option::use_ddio`

* RDMA NIC Device Memory

  bool `Option::use_dm`

* MPI

  macro `-DUSE_MPI`