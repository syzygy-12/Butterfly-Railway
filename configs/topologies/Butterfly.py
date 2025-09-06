from m5.params import *
from m5.objects import *

from common import FileSystemConfig
from topologies.BaseTopology import SimpleTopology


class Butterfly(SimpleTopology):
    description = "Butterfly"

    def __init__(self, controllers):
        self.nodes = controllers

    # ----------------------------------------------------------
    # create butterfly network
    # ----------------------------------------------------------
    def makeTopology(self, options, network, IntLink, ExtLink, Router):
        nodes = self.nodes

        # k 级 butterfly 需要 2^k 个路由器
        num_routers = options.num_cpus
        k = 0
        while (1 << k) < num_routers:
            k += 1

        link_latency = options.link_latency
        router_latency = options.router_latency

        # 创建路由器
        routers = [Router(router_id=i, latency=router_latency)
                   for i in range(num_routers)]
        network.routers = routers

        # 将 node 分给路由器
        cntrls_per_router, remainder = divmod(len(nodes), num_routers)
        assert remainder == 0, "controllers must be divisible by #routers"

        ext_links = []
        link_count = 0

        # 每个路由器接 cntrls_per_router 个控制器
        for node_idx, node in enumerate(nodes):
            router_id = node_idx // cntrls_per_router
            ext_links.append(
                ExtLink(
                    link_id=link_count,
                    ext_node=node,
                    int_node=routers[router_id],
                    latency=link_latency,
                )
            )
            link_count += 1
        network.ext_links = ext_links

        # ------------------------------------------------------
        # 内部链路：k 级 butterfly
        # 每级 i 路由器 r 与 r ^ (1 << i) 相连
        # ------------------------------------------------------
        int_links = []

        for stage in range(k):
            for r in range(num_routers):
                peer = r ^ (1 << stage)

                # 避免重复建链：只让较小 id 的发起
                if r < peer:
                    # 权重递增保证无死锁
                    weight = 50 - stage

                    # r -> peer  (上链路)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[r],
                            dst_node=routers[peer],
                            src_outport=f"Up_s{stage}",
                            dst_inport=f"Down_s{stage}",
                            latency=link_latency,
                            weight=weight,
                        )
                    )
                    link_count += 1

                    # peer -> r  (下链路)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[peer],
                            dst_node=routers[r],
                            src_outport=f"Down_s{stage}",
                            dst_inport=f"Up_s{stage}",
                            latency=link_latency,
                            weight=weight,
                        )
                    )
                    link_count += 1

        network.int_links = int_links

    # ----------------------------------------------------------
    # 注册节点
    # ----------------------------------------------------------
    def registerTopology(self, options):
        for i in range(options.num_cpus):
            FileSystemConfig.register_node(
                [i],
                MemorySize(options.mem_size) // options.num_cpus,
                i
            )