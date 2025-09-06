from m5.params import *
from m5.objects import *

from common import FileSystemConfig

from topologies.BaseTopology import SimpleTopology

class Torus3D(SimpleTopology):
    description = "Torus3D"

    def __init__(self, controllers):
        self.nodes = controllers

    def makeTopology(self, options, network, IntLink, ExtLink, Router):
        print("get in Torus 3D")
        nodes = self.nodes

        num_routers = options.num_cpus

        # Determine torus dimensions: try explicit options first, else infer cubic
        if hasattr(options, "torus_kx") and hasattr(options, "torus_ky") and hasattr(options, "torus_kz"):
            kx = int(options.torus_kx)
            ky = int(options.torus_ky)
            kz = int(options.torus_kz)
            assert kx * ky * kz == num_routers, "torus_kx * torus_ky * torus_kz must equal num_routers"
        else:
            # try to form a cube K x K x K
            K = round(num_routers ** (1.0 / 3.0))
            assert K ** 3 == num_routers, "num_cpus must be a perfect cube or provide torus_kx/ky/kz"
            kx = ky = kz = int(K)

        # default values for link latency and router latency.
        link_latency = options.link_latency
        router_latency = options.router_latency

        # Create routers
        routers = [
            Router(router_id=i, latency=router_latency)
            for i in range(num_routers)
        ]
        network.routers = routers

        # helper to map (x,y,z) to router_id
        def coord_to_id(x, y, z):
            return x + y * kx + z * (kx * ky)

        # link counter to set unique link ids
        link_count = 0

        # Distribute nodes (controllers) to routers similar to mesh implementation
        cntrls_per_router, remainder = divmod(len(nodes), num_routers)
        network_nodes = []
        remainder_nodes = []
        for node_index in range(len(nodes)):
            if node_index < (len(nodes) - remainder):
                network_nodes.append(nodes[node_index])
            else:
                remainder_nodes.append(nodes[node_index])

        # Connect each node to the appropriate router (ext links)
        ext_links = []
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

        # Connect remainder nodes (DMA) to router 0
        for (i, node) in enumerate(remainder_nodes):
            assert node.type == "DMA_Controller"
            assert i < remainder
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

        # Create internal links (wrap-around torus)
        int_links = []

        # We will name ports:
        # +X: "PosX" (src_outport)  -> dst_inport: "NegX"
        # -X: "NegX" (src_outport)  -> dst_inport: "PosX"
        # similarly for Y and Z.
        # Assign weights per-dimension to allow enforcing dimension order if needed:
        weight_x = 1
        weight_y = 2
        weight_z = 3

        # X-direction links (PosX / NegX) with wrap-around
        for z in range(kz):
            for y in range(ky):
                for x in range(kx):
                    src_id = coord_to_id(x, y, z)
                    # positive X neighbor (wrap)
                    x_pos = (x + 1) % kx
                    dst_id_pos = coord_to_id(x_pos, y, z)
                    # PosX out -> NegX in (from src to dst_pos)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[src_id],
                            dst_node=routers[dst_id_pos],
                            src_outport="PosX",
                            dst_inport="NegX",
                            latency=link_latency,
                            weight=weight_x,
                        )
                    )
                    link_count += 1

                    # Negative X neighbor link (from neighbor back to src)
                    x_neg = (x - 1 + kx) % kx
                    dst_id_neg = coord_to_id(x_neg, y, z)
                    # NegX out -> PosX in (from src to dst_neg)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[src_id],
                            dst_node=routers[dst_id_neg],
                            src_outport="NegX",
                            dst_inport="PosX",
                            latency=link_latency,
                            weight=weight_x,
                        )
                    )
                    link_count += 1

        # Y-direction links (PosY / NegY) with wrap-around
        for z in range(kz):
            for y in range(ky):
                for x in range(kx):
                    src_id = coord_to_id(x, y, z)
                    # positive Y neighbor (wrap)
                    y_pos = (y + 1) % ky
                    dst_id_pos = coord_to_id(x, y_pos, z)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[src_id],
                            dst_node=routers[dst_id_pos],
                            src_outport="PosY",
                            dst_inport="NegY",
                            latency=link_latency,
                            weight=weight_y,
                        )
                    )
                    link_count += 1

                    # negative Y neighbor
                    y_neg = (y - 1 + ky) % ky
                    dst_id_neg = coord_to_id(x, y_neg, z)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[src_id],
                            dst_node=routers[dst_id_neg],
                            src_outport="NegY",
                            dst_inport="PosY",
                            latency=link_latency,
                            weight=weight_y,
                        )
                    )
                    link_count += 1

        # Z-direction links (PosZ / NegZ) with wrap-around
        for z in range(kz):
            for y in range(ky):
                for x in range(kx):
                    src_id = coord_to_id(x, y, z)
                    # positive Z neighbor (wrap)
                    z_pos = (z + 1) % kz
                    dst_id_pos = coord_to_id(x, y, z_pos)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[src_id],
                            dst_node=routers[dst_id_pos],
                            src_outport="PosZ",
                            dst_inport="NegZ",
                            latency=link_latency,
                            weight=weight_z,
                        )
                    )
                    link_count += 1

                    # negative Z neighbor
                    z_neg = (z - 1 + kz) % kz
                    dst_id_neg = coord_to_id(x, y, z_neg)
                    int_links.append(
                        IntLink(
                            link_id=link_count,
                            src_node=routers[src_id],
                            dst_node=routers[dst_id_neg],
                            src_outport="NegZ",
                            dst_inport="PosZ",
                            latency=link_latency,
                            weight=weight_z,
                        )
                    )
                    link_count += 1

        network.int_links = int_links

    # Register nodes with filesystem
    def registerTopology(self, options):
        for i in range(options.num_cpus):
            FileSystemConfig.register_node(
                [i], MemorySize(options.mem_size) // options.num_cpus, i
            )
