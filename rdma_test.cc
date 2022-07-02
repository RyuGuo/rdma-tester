#include "common.h"

#include <condition_variable>
#include <iostream>
#include <mutex>

using namespace std;

int main(int argc, char *argv[]) {
#ifdef USE_MPI
  const int root_rank = 0;
  MPI_Init(&argc, &argv);
  int comm_size;
  int my_rank;
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  double sum_thoughput = 0;
#endif // USE_MPI

  if (argc == 1) {
    MasterOption option;
    option.num_recv_wr_per_qp = 32;
    option.num_thread = 1;
    option.use_dm = false;
    option.mr_size = 4L << 10;
#ifdef USE_PMEM
    option.pmem_dev_path = "/dev/dax0.0";
#endif // USE_PMEM
    MasterContext *ctx = new MasterContext(option);
    mutex m;
    unique_lock<mutex> lock(m);
    condition_variable cv;
    cv.wait(lock);
  } else {
    ClientOption option;
    option.qp_type = RC;
    option.device_id = 1;
    option.num_qp_per_mac = 4;
    option.num_thread = 1;
    option.payload = 256;
    for (int i = 1; i < argc; ++i) {
      option.master_ip.push_back(argv[i]);
    }
    ClientContext *ctx = new ClientContext(option);

    TestOption test_option;
    test_option.post_list = 1;
    TestResult result;

    if (option.qp_type == RC) {
      test_option.type = TestOption::READ;
      result = start_test(ctx, test_option);
      cout << "READ avg latency: " << result.avg_latency_us << " us" << endl;
      cout << "READ avg throughput: " << result.avg_throughput_Mops << " MOPS"
           << endl;

#ifdef USE_MPI
      MPI_Reduce(&result.avg_throughput_Mops, &sum_thoughput, 1, MPI_DOUBLE,
                 MPI_SUM, root_rank, MPI_COMM_WORLD);
      if (my_rank == root_rank) {
        cout << "READ TOTAL THROUGHPUT: " << sum_thoughput << " MOPS" << endl;
      }
#endif // USE_MPI
    }

    test_option.type = TestOption::SEND;
    result = start_test(ctx, test_option);
    cout << "SEND avg latency: " << result.avg_latency_us << " us" << endl;
    cout << "SEND avg throughput: " << result.avg_throughput_Mops << " MOPS"
         << endl;

#ifdef USE_MPI
    MPI_Reduce(&result.avg_throughput_Mops, &sum_thoughput, 1, MPI_DOUBLE,
               MPI_SUM, root_rank, MPI_COMM_WORLD);
    if (my_rank == root_rank) {
      cout << "READ TOTAL THROUGHPUT: " << sum_thoughput << " MOPS" << endl;
    }
#endif // USE_MPI

    if (option.qp_type == RC) {
      test_option.type = TestOption::WRITE_WITH_IMM;
      result = start_test(ctx, test_option);
      cout << "WRITE_WITH_IMM avg latency: " << result.avg_latency_us << " us"
           << endl;
      cout << "WRITE_WITH_IMM avg throughput: " << result.avg_throughput_Mops
           << " MOPS" << endl;

#ifdef USE_MPI
      MPI_Reduce(&result.avg_throughput_Mops, &sum_thoughput, 1, MPI_DOUBLE,
                 MPI_SUM, root_rank, MPI_COMM_WORLD);
      if (my_rank == root_rank) {
        cout << "READ TOTAL THROUGHPUT: " << sum_thoughput << " MOPS" << endl;
      }
#endif // USE_MPI
    }

#ifndef USE_PMEM
    if (option.qp_type == RC) {
      test_option.type = TestOption::WRITE;
      result = start_test(ctx, test_option);
      cout << "WRITE avg latency: " << result.avg_latency_us << " us" << endl;
      cout << "WRITE avg throughput: " << result.avg_throughput_Mops << " MOPS"
           << endl;

#ifdef USE_MPI
      MPI_Reduce(&result.avg_throughput_Mops, &sum_thoughput, 1, MPI_DOUBLE,
                 MPI_SUM, root_rank, MPI_COMM_WORLD);
      if (my_rank == root_rank) {
        cout << "READ TOTAL THROUGHPUT: " << sum_thoughput << " MOPS" << endl;
      }
#endif // USE_MPI
    }

    if (option.qp_type == RC) {
      test_option.type = TestOption::CAS;
      result = start_test(ctx, test_option);
      cout << "CAS avg latency: " << result.avg_latency_us << " us" << endl;
      cout << "CAS avg throughput: " << result.avg_throughput_Mops << " MOPS"
           << endl;

#ifdef USE_MPI
      MPI_Reduce(&result.avg_throughput_Mops, &sum_thoughput, 1, MPI_DOUBLE,
                 MPI_SUM, root_rank, MPI_COMM_WORLD);
      if (my_rank == root_rank) {
        cout << "READ TOTAL THROUGHPUT: " << sum_thoughput << " MOPS" << endl;
      }
#endif // USE_MPI
    }

    if (option.qp_type == RC) {
      test_option.type = TestOption::FETCH_ADD;
      result = start_test(ctx, test_option);
      cout << "FETCH_ADD avg latency: " << result.avg_latency_us << " us"
           << endl;
      cout << "FETCH_ADD avg throughput: " << result.avg_throughput_Mops
           << " MOPS" << endl;
#ifdef USE_MPI
      MPI_Reduce(&result.avg_throughput_Mops, &sum_thoughput, 1, MPI_DOUBLE,
                 MPI_SUM, root_rank, MPI_COMM_WORLD);
      if (my_rank == root_rank) {
        cout << "READ TOTAL THROUGHPUT: " << sum_thoughput << " MOPS" << endl;
      }
#endif // USE_MPI
    }
#endif // USE_PMEM

#ifdef USE_MPI
    MPI_Finalize();
#endif // USE_MPI
  }
  return 0;
}