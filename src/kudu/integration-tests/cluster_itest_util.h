// Copyright (c) 2015, Cloudera, inc.
// Confidential Cloudera Information: Covered by NDA.
//
// This header file contains generic helper utilities for writing tests against
// MiniClusters and ExternalMiniClusters. Ideally, the functions will be
// generic enough to use with either type of cluster, due to operating
// primarily through RPC-based APIs or through KuduClient.
// However, it's also OK to include common operations against a particular
// cluster type if it's general enough to use from multiple tests while not
// belonging in the MiniCluster / ExternalMiniCluster classes themselves. But
// consider just putting stuff like that in those classes.

#ifndef KUDU_INTEGRATION_TESTS_CLUSTER_ITEST_UTIL_H_
#define KUDU_INTEGRATION_TESTS_CLUSTER_ITEST_UTIL_H_

#include <string>
#include <tr1/memory>
#include <tr1/unordered_map>
#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/master/master.pb.h"

namespace kudu {
class MonoDelta;
class Schema;
class Sockaddr;
class Status;

namespace client {
class KuduClient;
class KuduSchema;
class KuduTable;
}

namespace consensus {
class ConsensusServiceProxy;
class OpId;
}

namespace master {
class MasterServiceProxy;
}

namespace rpc {
class Messenger;
}

namespace tserver {
class TabletServerErrorPB;
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;
}

namespace itest {

struct TServerDetails {
  NodeInstancePB instance_id;
  master::TSRegistrationPB registration;
  gscoped_ptr<tserver::TabletServerServiceProxy> tserver_proxy;
  gscoped_ptr<tserver::TabletServerAdminServiceProxy> tserver_admin_proxy;
  gscoped_ptr<consensus::ConsensusServiceProxy> consensus_proxy;

  // Convenience function to get the UUID from the instance_id struct.
  const std::string& uuid() const;

  std::string ToString() const;
};

// tablet_id -> replica map.
typedef std::tr1::unordered_multimap<std::string, TServerDetails*> TabletReplicaMap;

// uuid -> tablet server map.
typedef std::tr1::unordered_map<std::string, TServerDetails*> TabletServerMap;

// Returns possibly the simplest imaginable schema, with a single int key column.
client::KuduSchema SimpleIntKeyKuduSchema();

// Create a populated TabletServerMap by interrogating the master.
// Note: The bare-pointer TServerDetails values must be deleted by the caller!
// Consider using ValueDeleter (in gutil/stl_util.h) for that.
Status CreateTabletServerMap(master::MasterServiceProxy* master_proxy,
                             const std::tr1::shared_ptr<rpc::Messenger>& messenger,
                             std::tr1::unordered_map<std::string, TServerDetails*>* ts_map);

// Gets a vector containing the latest OpId for each of the given replicas.
// Returns a bad Status if any replica cannot be reached.
Status GetLastOpIdForEachReplica(const std::string& tablet_id,
                                 const std::vector<TServerDetails*>& replicas,
                                 std::vector<consensus::OpId>* op_ids);

// Like the above, but for a single replica.
Status GetLastOpIdForReplica(const std::string& tablet_id,
                             TServerDetails* replica,
                             consensus::OpId* op_id);

// Wait until all of the servers have converged on the same log index.
// The converged index must be at least equal to 'minimum_index'.
//
// Requires that all servers are running. Returns Status::TimedOut if the
// indexes do not converge within the given timeout.
Status WaitForServersToAgree(const MonoDelta& timeout,
                             const TabletServerMap& tablet_servers,
                             const std::string& tablet_id,
                             int64_t minimum_index);

// Wait until all specified replicas have logged at least the given index.
// Unlike WaitForServersToAgree(), the servers do not actually have to converge
// or quiesce. They only need to progress to or past the given index.
Status WaitUntilAllReplicasHaveOp(const int64_t log_index,
                                  const std::string& tablet_id,
                                  const std::vector<TServerDetails*>& replicas,
                                  const MonoDelta& timeout);

// Get the committed consensus state from the given replica.
Status GetCommittedConsensusState(const TServerDetails* replica,
                                  const std::string& tablet_id,
                                  const MonoDelta& timeout,
                                  consensus::ConsensusStatePB* consensus_state);

// Wait until the number of voters in the committed quorum is 'quorum_size',
// according to the specified replica.
Status WaitUntilCommittedQuorumNumVotersIs(int quorum_size,
                                           const TServerDetails* replica,
                                           const std::string& tablet_id,
                                           const MonoDelta& timeout);

// Returns:
// Status::OK() if the replica is alive and leader of the quorum.
// Status::NotFound() if the replica is not part of the quorum or is dead.
// Status::IllegalState() if the replica is live but not the leader.
Status GetReplicaStatusAndCheckIfLeader(const TServerDetails* replica,
                                        const std::string& tablet_id,
                                        const MonoDelta& timeout);

// Wait until the specified replica is leader.
Status WaitUntilLeader(const TServerDetails* replica,
                       const std::string& tablet_id,
                       const MonoDelta& timeout);

// Start an election on the specified tserver.
// 'timeout' only refers to the RPC asking the peer to start an election. The
// StartElection() RPC does not block waiting for the results of the election,
// and neither does this call.
Status StartElection(const TServerDetails* replica,
                     const std::string& tablet_id,
                     const MonoDelta& timeout);

// Cause a leader to step down on the specified server.
// 'timeout' refers to the RPC timeout waiting synchronously for stepdown to
// complete on the leader side. Since that does not require communication with
// other nodes at this time, this call is rather quick.
Status LeaderStepDown(const TServerDetails* replica,
                      const std::string& tablet_id,
                      const MonoDelta& timeout,
                      tserver::TabletServerErrorPB* error = NULL);

// Write a "simple test schema" row to the specified tablet on the given
// replica. This schema is commonly used by tests and is defined in
// wire_protocol-test-util.h
// The caller must specify whether this is an INSERT or UPDATE call via
// write_type.
Status WriteSimpleTestRow(const TServerDetails* replica,
                          const std::string& tablet_id,
                          RowOperationsPB::Type write_type,
                          int32_t key,
                          int32_t int_val,
                          const std::string& string_val,
                          const MonoDelta& timeout);

// Run a ConfigChange to ADD_SERVER on 'replica_to_add'.
// The RPC request is sent to 'leader'.
Status AddServer(const TServerDetails* leader,
                 const std::string& tablet_id,
                 const TServerDetails* replica_to_add,
                 consensus::QuorumPeerPB::MemberType member_type,
                 const MonoDelta& timeout);

// Run a ConfigChange to REMOVE_SERVER on 'replica_to_remove'.
// The RPC request is sent to 'leader'.
Status RemoveServer(const TServerDetails* leader,
                    const std::string& tablet_id,
                    const TServerDetails* replica_to_remove,
                    const MonoDelta& timeout);

} // namespace itest
} // namespace kudu

#endif // KUDU_INTEGRATION_TESTS_CLUSTER_ITEST_UTIL_H_
