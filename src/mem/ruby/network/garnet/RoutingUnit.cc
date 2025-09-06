/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "mem/ruby/network/garnet/RoutingUnit.hh"

#include "base/cast.hh"
#include "base/compiler.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet/InputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/slicc_interface/Message.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

RoutingUnit::RoutingUnit(Router *router)
{
    m_router = router;
    m_routing_table.clear();
    m_weight_table.clear();
}

void
RoutingUnit::addRoute(std::vector<NetDest>& routing_table_entry)
{
    if (routing_table_entry.size() > m_routing_table.size()) {
        m_routing_table.resize(routing_table_entry.size());
    }
    for (int v = 0; v < routing_table_entry.size(); v++) {
        m_routing_table[v].push_back(routing_table_entry[v]);
    }
}

void
RoutingUnit::addWeight(int link_weight)
{
    m_weight_table.push_back(link_weight);
}

bool
RoutingUnit::supportsVnet(int vnet, std::vector<int> sVnets)
{
    // If all vnets are supported, return true
    if (sVnets.size() == 0) {
        return true;
    }

    // Find the vnet in the vector, return true
    if (std::find(sVnets.begin(), sVnets.end(), vnet) != sVnets.end()) {
        return true;
    }

    // Not supported vnet
    return false;
}

/*
 * This is the default routing algorithm in garnet.
 * The routing table is populated during topology creation.
 * Routes can be biased via weight assignments in the topology file.
 * Correct weight assignments are critical to provide deadlock avoidance.
 */
int
RoutingUnit::lookupRoutingTable(int vnet, NetDest msg_destination)
{
    // First find all possible output link candidates
    // For ordered vnet, just choose the first
    // (to make sure different packets don't choose different routes)
    // For unordered vnet, randomly choose any of the links
    // To have a strict ordering between links, they should be given
    // different weights in the topology file

    int output_link = -1;
    int min_weight = INFINITE_;
    std::vector<int> output_link_candidates;
    int num_candidates = 0;

    // Identify the minimum weight among the candidate output links
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

        if (m_weight_table[link] <= min_weight)
            min_weight = m_weight_table[link];
        }
    }

    // Collect all candidate output links with this minimum weight
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

            if (m_weight_table[link] == min_weight) {
                num_candidates++;
                output_link_candidates.push_back(link);
            }
        }
    }

    if (output_link_candidates.size() == 0) {
        fatal("Fatal Error:: No Route exists from this Router.");
        exit(0);
    }

    // Randomly select any candidate output link
    int candidate = 0;
    if (!(m_router->get_net_ptr())->isVNetOrdered(vnet))
        candidate = rand() % num_candidates;

    output_link = output_link_candidates.at(candidate);
    return output_link;
}


void
RoutingUnit::addInDirection(PortDirection inport_dirn, int inport_idx)
{
    m_inports_dirn2idx[inport_dirn] = inport_idx;
    m_inports_idx2dirn[inport_idx]  = inport_dirn;
}

void
RoutingUnit::addOutDirection(PortDirection outport_dirn, int outport_idx)
{
    m_outports_dirn2idx[outport_dirn] = outport_idx;
    m_outports_idx2dirn[outport_idx]  = outport_dirn;
}

// outportCompute() is called by the InputUnit
// It calls the routing table by default.
// A template for adaptive topology-specific routing algorithm
// implementations using port directions rather than a static routing
// table is provided here.

int
RoutingUnit::outportCompute(RouteInfo route, int inport,
                            PortDirection inport_dirn)
{
    int outport = -1;

    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        return outport;
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
    RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    switch (routing_algorithm) {
        case TABLE_:  outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
        case XY_:     outport =
            outportComputeXY(route, inport, inport_dirn); break;
        // any custom algorithm
        case CUSTOM_: outport =
            outportComputeCustom(route, inport, inport_dirn); break;
        case RING_:   outport =
            outportComputeRing(route, inport, inport_dirn); break;
        case TORUS_3D_: outport =
            outportComputeTorus3D(route, inport, inport_dirn); break;
        case TORUS_3D_ADAPTIVE_: outport = 
            outportComputeTorus3DAdaptive(route, inport, inport_dirn); break;
        case BUTTERFLY_: outport =
            outportComputeButterfly(route, inport, inport_dirn); break;
        case BUTTERFLY_RAILWAY_: outport =
            outportComputeButterflyRailway(route, inport, inport_dirn); 
            break;
        default: outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
    }

    assert(outport != -1);
    return outport;
}

// XY routing implemented using port directions
// Only for reference purpose in a Mesh
// By default Garnet uses the routing table
int
RoutingUnit::outportComputeXY(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    [[maybe_unused]] int num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));

    if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "West");
            outport_dirn = "East";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "East");
            outport_dirn = "West";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            // "Local" or "South" or "West" or "East"
            assert(inport_dirn != "North");
            outport_dirn = "North";
        } else {
            // "Local" or "North" or "West" or "East"
            assert(inport_dirn != "South");
            outport_dirn = "South";
        }
    } else {
        // x_hops == 0 and y_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == 0");
    }

    return m_outports_dirn2idx[outport_dirn];
}

int
RoutingUnit::outportComputeRing(RouteInfo route,
                                int inport,
                                PortDirection inport_dirn)
{

    PortDirection outport_dirn = "Unknown";

    int num_routers = m_router->get_net_ptr()->getNumRouters();
    int my_id = m_router->get_id();
    int dest_id = route.dest_router;

    assert(my_id != dest_id);

    // Compute distance in clockwise and counterclockwise
    int cw_dist  = (dest_id - my_id + num_routers) % num_routers;
    int ccw_dist = (my_id - dest_id + num_routers) % num_routers;

    // Pick shorter path (ties -> clockwise)
    if (cw_dist <= ccw_dist) {
        assert(inport_dirn == "Local" || inport_dirn == "CounterClockwise");
        outport_dirn = "Clockwise";
    } else {
        assert(inport_dirn == "Local" || inport_dirn == "Clockwise");
        outport_dirn = "CounterClockwise";
    }

    return m_outports_dirn2idx[outport_dirn];
}

int
RoutingUnit::outportComputeTorus3D(RouteInfo route,
                                   int inport,
                                   PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    // Get topology dimensions
    int kx = 4;
    int ky = 4;
    int kz = 4;
    assert(kx > 0 && ky > 0 && kz > 0);

    // My coordinates
    int my_id = m_router->get_id();
    int my_x =  my_id % kx;
    int my_y = (my_id / kx) % ky;
    int my_z =  my_id / (kx * ky);

    // Destination coordinates
    int dest_id = route.dest_router;
    int dest_x =  dest_id % kx;
    int dest_y = (dest_id / kx) % ky;
    int dest_z =  dest_id / (kx * ky);

    // Sanity: not same router
    assert(my_id != dest_id);

    //----------------------
    // Compute torus deltas
    //----------------------
    auto torus_delta = [](int cur, int dest, int dim) {
        int d = (dest - cur + dim) % dim;
        int forward = d;
        int backward = dim - d;
        // return {dist, dir}: dir = +1 → positive, -1 → negative
        if (forward <= backward)
            return std::make_pair(forward, +1);
        else
            return std::make_pair(backward, -1);
    };

    auto [dx, dir_x] = torus_delta(my_x, dest_x, kx);
    auto [dy, dir_y] = torus_delta(my_y, dest_y, ky);
    auto [dz, dir_z] = torus_delta(my_z, dest_z, kz);

    //----------------------
    // XYZ dimension order
    //----------------------
    //----------------------
    // Dimension-order (X -> Y -> Z) deterministic minimal routing
    // This function is intended for DOR-like (escape VC) behavior.
    // It returns a single outport dirn among PosX/NegX/PosY/NegY/PosZ/NegZ.
    // We intentionally DO NOT assert on inport_dirn string values here because
    // torus wrap-around and multi-hop can produce many legal inport_dirn values.
    //----------------------
    if (dx > 0) {
        if (dir_x > 0) {
            outport_dirn = "PosX";
        } else {
            outport_dirn = "NegX";
        }
    } else if (dy > 0) {
        if (dir_y > 0) {
            outport_dirn = "PosY";
        } else {
            outport_dirn = "NegY";
        }
    } else if (dz > 0) {
        if (dir_z > 0) {
            outport_dirn = "PosZ";
        } else {
            outport_dirn = "NegZ";
        }
    } else {
        // should not happen because we asserted my_id != dest_id earlier
        panic("RoutingUnit::outportComputeTorus3D: already at destination?");
    }

    return m_outports_dirn2idx[outport_dirn];
}

int
RoutingUnit::outportComputeTorus3DAdaptive(RouteInfo route, int inport,
                                           PortDirection inport_dirn)
{
    int kx = 4;
    int ky = 4;
    int kz = 4;

    int cur_id = m_router->get_id();
    int cur_x = cur_id % kx;
    int cur_y = (cur_id / kx) % ky;
    int cur_z = cur_id / (kx * ky);

    int dest_x = route.dest_router % kx;
    int dest_y = (route.dest_router / kx) % ky;
    int dest_z = route.dest_router / (kx * ky);

    int dx = dest_x - cur_x;
    int dy = dest_y - cur_y;
    int dz = dest_z - cur_z;

    // wrap-around for torus distances
    auto wrap = [](int d, int k) {
        if (d > k/2) d -= k;
        if (d < -k/2) d += k;
        return d;
    };
    dx = wrap(dx, kx);
    dy = wrap(dy, ky);
    dz = wrap(dz, kz);

    // -------------------------------
    // Step 1: already at destination
    // -------------------------------
    if (dx == 0 && dy == 0 && dz == 0) {
        return m_outports_dirn2idx["Local"];
    }

    // -------------------------------
    // Step 2: choose a plane
    // -------------------------------
    std::vector<PortDirection> candidates;

    if (dx != 0 || dy != 0) {
        // XY plane
        if (dx > 0) candidates.push_back("PosX");
        if (dx < 0) candidates.push_back("NegX");
        if (dy > 0) candidates.push_back("PosY");
        if (dy < 0) candidates.push_back("NegY");
    } else {
        // Z plane only
        if (dz > 0) candidates.push_back("PosZ");
        if (dz < 0) candidates.push_back("NegZ");
    }

    // -------------------------------
    // Step 3: simple adaptive choice
    // -------------------------------
    PortDirection chosen_dir;
    if (candidates.size() == 1) {
        chosen_dir = candidates[0];
    } else {
        // pick one of the productive directions randomly
        int idx = random() % candidates.size();
        chosen_dir = candidates[idx];
    }

    DPRINTF(RubyNetwork, "Router[%d]: Adaptive (simple) route -> %s\n",
            m_router->get_id(), chosen_dir.c_str());

    return m_outports_dirn2idx[chosen_dir];
}

int
RoutingUnit::outportComputeButterfly(RouteInfo route,
                                     int inport,
                                     PortDirection inport_dirn)
{
    int my_id   = m_router->get_id();
    int num_routers = m_router->get_net_ptr()->getNumRouters();
    int k = 0;
    while ((1 << k) < num_routers) ++k;          // 级数 k
    assert(my_id >= 0 && my_id < num_routers);

    int dest_id = route.dest_router;
    assert(dest_id >= 0 && dest_id < num_routers);

    if (dest_id == my_id) {                      // 已到达目的
        return m_outports_dirn2idx["Local"];
    }

    /* 找到最高不同位，即当前应处理的 stage */
    int diff = my_id ^ dest_id;
    int stage = k - 1;
    while (stage >= 0 && !(diff & (1 << stage))) --stage;
    assert(stage >= 0);                          // 必有不同位

    /* 看 dest 在该 bit 的值决定上下 */
    int dest_bit = (dest_id >> stage) & 1;
    std::string outport_dirn = dest_bit ? "Up_s" + std::to_string(stage)
                                        : "Down_s"   + std::to_string(stage);

    int butterfly_port = m_outports_dirn2idx[outport_dirn];
    return butterfly_port;
}

int
RoutingUnit::outportComputeButterflyRailway(RouteInfo route,
                                     int inport,
                                     PortDirection inport_dirn)
{
    int my_id   = m_router->get_id();
    int num_routers = m_router->get_net_ptr()->getNumRouters();
    int k = 0;
    while ((1 << k) < num_routers) ++k;          // 级数 k
    assert(my_id >= 0 && my_id < num_routers);

    int dest_id = route.dest_router;
    assert(dest_id >= 0 && dest_id < num_routers);

    if (dest_id == my_id) {                      // 已到达目的
        return m_outports_dirn2idx["Local"];
    }

    /* 找到最高不同位，即当前应处理的 stage */
    int diff = my_id ^ dest_id;
    int stage = k - 1;
    while (stage >= 0 && !(diff & (1 << stage))) --stage;
    assert(stage >= 0);                          // 必有不同位

    /* 看 dest 在该 bit 的值决定上下 */
    int dest_bit = (dest_id >> stage) & 1;
    std::string outport_dirn = dest_bit ? "Up_s" + std::to_string(stage)
                                        : "Down_s"   + std::to_string(stage);

    int butterfly_port = m_outports_dirn2idx[outport_dirn];
    int railway_port = m_outports_dirn2idx[my_id < dest_id ? "Right": "Left"];

    int butterfly_credit = m_router->countRequestsForPort(butterfly_port);
    int railway_credit = m_router->countRequestsForPort(railway_port);

    int h = butterfly_credit - railway_credit;
    // DPRINTF(RubyNetwork, "Router[%d]: Butterfly = %d, Railway = %d\n",
    //         m_router->get_id(), butterfly_credit, railway_credit);
    // change to cout version
    // std::cout << "Router[" << m_router->get_id() << "]: Butterfly = " << butterfly_credit
    //           << ", Railway = " << railway_credit << std::endl;
    return h <= 0? butterfly_port : railway_port;
}

// Template for implementing custom routing algorithm
// using port directions. (Example adaptive)
int
RoutingUnit::outportComputeCustom(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn)
{
    panic("%s placeholder executed", __FUNCTION__);
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
