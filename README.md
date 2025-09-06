# Butterfly Railway on gem5 — Quick Start



## 1) Clone & Build

```bash
# 1) Get the code
git clone https://github.com/syzygy-12/Butterfly-Railway.git 
cd Butterfly-Railway

# 2) Build gem5 (NULL ISA is enough for Garnet experiments)
scons build/NULL/gem5.opt -j"$(nproc)"
```

## 2) Run a Single Experiment

The key command to launch a run is:

```bash
./build/NULL/gem5.opt configs/example/garnet_synth_traffic.py \
  --network=garnet \
  --num-cpus=64 \
  --num-dirs=64 \
  --topology=Butterfly_railway \
  --routing-algorithm=7 \
  --sim-cycles=100000 \
  --inj-vnet=0 \
  --synthetic=uniform_random \
  --injectionrate=0.2
```

Change --num-cpus/--num-dirs to your node count (e.g., 16, 64).

Set --synthetic to one of: uniform_random, transpose, hotspot, etc.

Adjust --injectionrate and --sim-cycles as needed.

--routing-algorithm=7 should match your Butterfly Railway adaptive routing implementation.

## 3) Extract Key Metrics from stats.txt

After a successful run, metrics are in m5out/stats.txt.
Use grep + awk to extract values (with a safe fallback):

```bash
packets_injected=$(grep "packets_injected::total" m5out/stats.txt | awk '{print $2}' || echo "0")
packets_received=$(grep "packets_received::total" m5out/stats.txt | awk '{print $2}' || echo "0")
avg_packet_latency=$(grep "average_packet_latency" m5out/stats.txt | awk '{print $2}' || echo "0")
avg_network_latency=$(grep "average_packet_network_latency" m5out/stats.txt | awk '{print $2}' || echo "0")
avg_queueing_latency=$(grep "average_packet_queueing_latency" m5out/stats.txt | awk '{print $2}' || echo "0")
avg_hops=$(grep "average_hops" m5out/stats.txt | awk '{print $2}' || echo "0")
```

Optionally compute quick rates (requires bc):

```bash
SIM_CYCLES=100000
NUM_NODES=64
reception_rate=$(echo "scale=6; $packets_received / $NUM_NODES / $SIM_CYCLES" | bc)
throughput=$(echo "scale=6; $packets_received / $SIM_CYCLES" | bc)
```

## 4) Save a One-Line CSV Record

Create a results folder + header once:

```bash
mkdir -p lab4/results
echo "traffic_pattern,packets_injected,packets_received,avg_packet_latency,avg_network_latency,avg_queueing_latency,avg_hops,reception_rate,throughput" > lab4/results/test.csv
```

Append your current run (example with uniform_random):

```bash
pattern="uniform_random"
echo "$pattern,$packets_injected,$packets_received,$avg_packet_latency,$avg_network_latency,$avg_queueing_latency,$avg_hops,$reception_rate,$throughput" >> lab4/results/test.csv
```

## 5) Common Tweaks

Topology: --topology=Butterfly_railway, --topology=Butterfly

Nodes: keep --num-cpus == --num-dirs

Traffic: --synthetic={uniform_random|transpose|hotspot|...}

Routing: --routing-algorithm=7 is adaptive routing, --routing-algorithm=6 is deterministic routing.

Runtime: increase --sim-cycles or adjust your shell timeout if used

## 6) Troubleshooting

If m5out/stats.txt is missing, the run likely failed; check terminal output.

Ensure the Butterfly Railway topology file is correctly registered and reachable by gem5's config path.

Verify you built build/NULL/gem5.opt successfully and are running the correct binary.
