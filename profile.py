"""
## RDMA Key-Value Cache Experiment

This profile provisions two bare-metal nodes connected by a 25 Gbps link for
evaluating RDMA-based distributed caching. It is used for CS2640 final project
research comparing TCP/RPC, two-sided RDMA, and one-sided RDMA communication
for a key-value store workload.

Both nodes are configured with Ubuntu 22.04 and a Mellanox ConnectX NIC
capable of RoCE v2 (RDMA over Converged Ethernet). One node acts as the
key-value server, the other as the benchmark client.

## Setup Instructions

1. Install RDMA libraries on both nodes:

    sudo apt update && sudo apt install -y libibverbs-dev librdmacm-dev \
      ibverbs-utils rdma-core infiniband-diags perftest cmake build-essential git

2. Verify RDMA hardware is active:

    ibv_devinfo   # look for: state = PORT_ACTIVE

3. Smoke-test RDMA between nodes:

    # On server node:
    ib_send_bw -d mlx5_0

    # On client node (use the 10.10.x.x link IP shown in ibv_devinfo):
    ib_send_bw -d mlx5_0 <server-link-ip>

4. Clone and build the project:

    git clone <your-repo-url>
    cd CS2640-Final-Project
    cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

5. Run the server, then the benchmark client:

    # Server node:
    ./build/kv_server

    # Client node:
    ./build/kv_benchmark --clients 1,2,4,8 --output experiments/results.csv

Note: Extend your experiment before it expires to avoid losing data.
Sync results back locally with rsync when done.
"""

import geni.portal as portal
import geni.rspec.pg as pg

pc = portal.Context()

pc.defineParameter(
    "nodeType",
    "Node Hardware Type",
    portal.ParameterType.NODETYPE,
    "xl170",
    longDescription="Choose a node type with a Mellanox NIC for RDMA. "
                    "xl170 (Utah), c220g5 (Wisconsin), or d6515 (Utah) all work."
)

params = pc.bindParameters()
request = pc.makeRequestRSpec()

# Two nodes: server and client
server = request.RawPC("server")
server.hardware_type = params.nodeType
server.disk_image = "urn:publicid:IDN+emulab.net+image+emulab-ops:UBUNTU22-64-STD"

client = request.RawPC("client")
client.hardware_type = params.nodeType
client.disk_image = "urn:publicid:IDN+emulab.net+image+emulab-ops:UBUNTU22-64-STD"

# Link between them (CloudLab will use the RDMA-capable interface if available)
link = request.Link("rdma-link")
link.addNode(server)
link.addNode(client)
link.bandwidth = 25000000  # 25 Gbps

pc.printRequestRSpec(request)
