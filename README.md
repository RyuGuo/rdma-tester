# RDMA Tester

`rdma_test.cc` is a simple test benchmark for RDMA.

```shell
mkdir build
cmake ..
make
```

For master: `./rdma_test`

For client: `./rdma_test <master_ip> ...`

We also provide **persistent memory** and **MPI** test. You can use/unuse their macro to open/close these features.