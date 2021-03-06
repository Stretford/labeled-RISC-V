// See LICENSE for license details.

#include "verilated.h"
#if VM_TRACE
#include "verilated_vcd_c.h"
#endif
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static uint64_t trace_count = 0;
bool verbose;
bool done_reset;

void handle_sigterm(int sig)
{
  printf("Interrupted\n");
  exit(0);
}

double sc_time_stamp()
{
  return trace_count;
}

// JTAGDTM.cc use this to get timestamp
uint64_t get_time_stamp()
{
  return trace_count;
}

extern "C" int vpi_get_vlog_info(void* arg)
{
  return 0;
}

// TCP port on which to accpet jtag debug connection
int port = 0;
FILE *trace = NULL;
FILE *replay_trace = NULL;

int main(int argc, char** argv)
{
  unsigned random_seed = (unsigned)time(NULL) ^ (unsigned)getpid();
  uint64_t max_cycles = -1;
  uint64_t start = 0;
  int ret = 0;
  FILE *vcdfile = NULL;
  bool print_cycles = false;

  for (int i = 1; i < argc; i++)
  {
    std::string arg = argv[i];
    if (arg.substr(0, 2) == "-v") {
      const char* filename = argv[i]+2;
      vcdfile = strcmp(filename, "-") == 0 ? stdout : fopen(filename, "w");
      if (!vcdfile)
        abort();
    } else if (arg.substr(0, 2) == "-s")
      random_seed = atoi(argv[i]+2);
    else if (arg == "+verbose")
      verbose = true;
    else if (arg.substr(0, 12) == "+max-cycles=")
      max_cycles = atoll(argv[i]+12);
    else if (arg.substr(0, 7) == "+start=")
      start = atoll(argv[i]+7);
    else if (arg.substr(0, 12) == "+cycle-count")
      print_cycles = true;
    else if (arg.substr(0, 2) == "-p")
      port = atoi(argv[i]+2);
    else if (arg.substr(0, 7) == "+trace=") {
      const char* filename = argv[i]+7;
      trace = strcmp(filename, "-") == 0 ? stdout : fopen(filename, "w");
      if (!trace)
        abort();
	}
    else if (arg.substr(0, 14) == "+replay_trace=") {
      const char* filename = argv[i]+14;
      replay_trace = strcmp(filename, "-") == 0 ? stdout : fopen(filename, "r");
      if (!replay_trace)
        abort();
	}
  }

  if (verbose)
    fprintf(stderr, "using random seed %u\n", random_seed);

  if (replay_trace && trace) {
    fprintf(stderr, "You can not simutaneously trace and replay trace");
	abort();
  }

  srand(random_seed);
  srand48(random_seed);

  Verilated::randReset(2);
  VTestHarness *tile = new VTestHarness;

#if VM_TRACE
  Verilated::traceEverOn(true); // Verilator must compute traced signals
  std::unique_ptr<VerilatedVcdFILE> vcdfd(new VerilatedVcdFILE(vcdfile));
  std::unique_ptr<VerilatedVcdC> tfp(new VerilatedVcdC(vcdfd.get()));
  if (vcdfile) {
    tile->trace(tfp.get(), 99);  // Trace 99 levels of hierarchy
    tfp->open("");
  }
#endif

  void init_jtag_vpi(void);
  init_jtag_vpi();
  signal(SIGTERM, handle_sigterm);

  // reset for several cycles to handle pipelined reset
  for (int i = 0; i < 10; i++) {
    tile->reset = 1;
    tile->clock = 0;
    tile->eval();
    tile->clock = 1;
    tile->eval();
    tile->reset = 0;
  }
  done_reset = true;

  while (!tile->io_success && trace_count < max_cycles) {
    tile->clock = 0;
    tile->eval();
#if VM_TRACE
    bool dump = tfp && trace_count >= start;
    if (dump)
      tfp->dump(static_cast<vluint64_t>(trace_count * 2));
#endif

    tile->clock = 1;
    tile->eval();
#if VM_TRACE
    if (dump)
      tfp->dump(static_cast<vluint64_t>(trace_count * 2 + 1));
#endif
    trace_count++;
  }

#if VM_TRACE
  if (tfp)
    tfp->close();
#endif

  if (vcdfile)
    fclose(vcdfile);

  if (trace)
    fclose(trace);

  if (replay_trace)
    fclose(replay_trace);

  if (trace_count == max_cycles)
  {
    fprintf(stderr, "*** FAILED *** (timeout, seed %d) after %ld cycles\n", random_seed, trace_count);
    ret = 2;
  }
  else if (verbose || print_cycles)
  {
    fprintf(stderr, "Completed after %ld cycles\n", trace_count);
  }

  delete tile;
  return ret;
}
