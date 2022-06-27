#include "common.h"

int main(int argc, char *argv[]){
  if (argc == 1) {
    MasterOption option;
    MasterContext *ctx = new MasterContext(option);
    while (1);
  } else {
    ClientOption option;
    option.num_qp_per_mac = 2;
    option.master_ip.push_back("127.0.0.1");
    ClientContext *ctx = new ClientContext(option);
  }
  return 0;
}