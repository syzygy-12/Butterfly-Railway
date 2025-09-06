from m5.params import *
from m5.objects import *

from common import FileSystemConfig
from topologies.BaseTopology import SimpleTopology


class Ring(SimpleTopology):
    description = "Ring"

    def __init__(self, controllers):
        self.nodes = controllers

    def makeTopology(self, options, network, IntLink, ExtLink, Router):
        nodes = self.nodes

        num_routers = options.num_cpus
        assert num_routers >= 2

        link_latency = options.link_latency
        router_latency = options.router_latency

        # evenly distribute controllers to routers
        cntrls_per_router, remainder = divmod(len(nodes), num_routers)

        routers = [
            Router(router_id=i, latency=router_latency)
            for i in range(num_routers)
        ]
        network.routers = routers

        link_count = 0
        ext_links = []

        # attach controllers
        network_nodes = []
        remainder_nodes = []
        for node_index in range(len(nodes)):
            if node_index < (len(nodes) - remainder):
                network_nodes.append(nodes[node_index])
            else:
                remainder_nodes.append(nodes[node_index])

        for (i, n) in enumerate(network_nodes):
            cntrl_level, router_id = divmod(i, num_routers)
            assert cntrl_level < cntrls_per_router
            ext_links.append(
                ExtLink(
                    link_id=link_count,
                    ext_node=n,
                    int_node=routers[router_id],
                    latency=link_latency,
                )
            )
            link_count += 1

        for (i, node) in enumerate(remainder_nodes):
            assert node.type == "DMA_Controller"
            ext_links.append(
                ExtLink(
                    link_id=link_count,
                    ext_node=node,
                    int_node=routers[0],
                    latency=link_latency,
                )
            )
            link_count += 1

        network.ext_links = ext_links

        # build bidirectional ring
        int_links = []
        for i in range(num_routers):
            next_router = (i + 1) % num_routers
            prev_router = (i - 1 + num_routers) % num_routers

            # clockwise
            int_links.append(
                IntLink(
                    link_id=link_count,
                    src_node=routers[i],
                    dst_node=routers[next_router],
                    src_outport="Clockwise",
                    dst_inport="CounterClockwise",
                    latency=link_latency,
                    weight=1,
                )
            )
            link_count += 1

            # counterclockwise
            int_links.append(
                IntLink(
                    link_id=link_count,
                    src_node=routers[i],
                    dst_node=routers[prev_router],
                    src_outport="CounterClockwise",
                    dst_inport="Clockwise",
                    latency=link_latency,
                    weight=1,
                )
            )
            link_count += 1

        network.int_links = int_links

    def registerTopology(self, options):
        for i in range(options.num_cpus):
            FileSystemConfig.register_node(
                [i], MemorySize(options.mem_size) // options.num_cpus, i
            )
