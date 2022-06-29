#include "common.h"

#include <iostream>

using namespace std;

#define USE_PMEM

int main(int argc, char *argv[]) {
  if (argc == 1) {
    MasterOption option;
    option.num_recv_wr_per_qp = 32;
    option.num_thread = 1;
#ifdef USE_PMEM
    option.pmem_dev_path = "/dev/dax0.0";
#endif // USE_PMEM
    MasterContext *ctx = new MasterContext(option);
    while (1)
      ;
  } else {
    ClientOption option;
    option.device_id = 1;
    option.num_qp_per_mac = 4;
    option.num_thread = 4;
    option.master_ip.push_back(argv[1]);
    ClientContext *ctx = new ClientContext(option);

    TestOption test_option;
    test_option.post_list = 8;
    test_option.payload = 256;
    TestResult result;

    test_option.type = TestOption::READ;
    result = start_test(ctx, test_option);
    cout << "READ avg latency: " << result.avg_latency_us << " us" << endl;
    cout << "READ avg throughput: " << result.avg_throughput_Mops << " MOPS"
         << endl;

    test_option.type = TestOption::SEND;
    result = start_test(ctx, test_option);
    cout << "SEND avg latency: " << result.avg_latency_us << " us" << endl;
    cout << "SEND avg throughput: " << result.avg_throughput_Mops << " MOPS"
         << endl;

    test_option.type = TestOption::WRITE_WITH_IMM;
    result = start_test(ctx, test_option);
    cout << "WRITE_WITH_IMM avg latency: " << result.avg_latency_us << " us"
         << endl;
    cout << "WRITE_WITH_IMM avg throughput: " << result.avg_throughput_Mops
         << " MOPS" << endl;

#ifndef USE_PMEM
    test_option.type = TestOption::WRITE;
    result = start_test(ctx, test_option);
    cout << "WRITE avg latency: " << result.avg_latency_us << " us" << endl;
    cout << "WRITE avg throughput: " << result.avg_throughput_Mops << " MOPS"
         << endl;

    test_option.type = TestOption::CAS;
    result = start_test(ctx, test_option);
    cout << "CAS avg latency: " << result.avg_latency_us << " us" << endl;
    cout << "CAS avg throughput: " << result.avg_throughput_Mops << " MOPS"
         << endl;

    test_option.type = TestOption::FETCH_ADD;
    result = start_test(ctx, test_option);
    cout << "FETCH_ADD avg latency: " << result.avg_latency_us << " us" << endl;
    cout << "FETCH_ADD avg throughput: " << result.avg_throughput_Mops
         << " MOPS" << endl;
#endif // USE_PMEM
  }
  return 0;
}