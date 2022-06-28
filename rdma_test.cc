#include "common.h"

#include <iostream>

using namespace std;

int main(int argc, char *argv[]){
  if (argc == 1) {
    MasterOption option;
    MasterContext *ctx = new MasterContext(option);
    while (1);
  } else {
    ClientOption option;
    option.num_qp_per_mac = 2;
    option.master_ip.push_back(argv[1]);
    ClientContext *ctx = new ClientContext(option);

    TestOption test_option;
    test_option.type = TestOption::FETCH_ADD;
    TestResult result = start_test(ctx, test_option);
    cout << "avg latency: " << result.avg_latency_us << " us" << endl;
    cout << "avg throughput: " << result.avg_throughput_Mops << " MOPS" << endl;
  }
  return 0;
}