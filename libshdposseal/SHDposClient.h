
#pragma once
#include <libp2p/Host.h>
#include "ChainParams.h"
#include <libbrccore/KeyManager.h>
#include <libbrcdchain/Client.h>
#include <boost/filesystem/path.hpp>
#include "libbrcdchain/Interface.h"
#include "Common.h"
#include "libbrcdchain/Nodemonitor.h"
#include <libbrcdchain/Account.h>

namespace dev
{
namespace bacd
{
class SHDpos;
using namespace dev::brc;
DEV_SIMPLE_EXCEPTION(InvalidSHDposSealEngine);

class SHDposClient : public Client
{
public:
    SHDposClient(ChainParams const& _params, int _networkID, p2p::Host& _host,
        std::shared_ptr<GasPricer> _gpForAdoption, boost::filesystem::path const& _dbPath = {},
        boost::filesystem::path const& _snapshotPath = {},
        WithExisting _forceAction = WithExisting::Trust,
        DBBlockConfig const& db_config = DBBlockConfig(),
        TransactionQueue::Limits const& _l = TransactionQueue::Limits{102400, 102400});

    ~SHDposClient();

public:
    SHDpos* dpos() const;
    void startSealing() override;
    void doWork(bool _doWait) override;
    int chainId() const override;

    inline const BlockHeader    getCurrHeader()const     { return m_bc.info(); }
    inline h256                 getCurrBlockhash()const  { return m_bc.currentHash(); }
    inline h256                 getGenesisHash()const    { return m_bc.genesisHash(); }

	void getCurrCreater(CreaterType _type, std::vector<Address>& _creaters) const;

	bool verifyVarlitorPrivatrKey() const;

	/// verify standby_node
    ///@return true if own_addr can create_block
    ///@param super_addr : offline super_node addr
	bool  verify_standby(int64_t block_time, const dev::Address &own_addr, uint32_t varlitorInterval_time) const;

protected:
    void rejigSealing();
	void syncTransactionQueue();
private:
    void init(p2p::Host & _host, int _netWorkId);
    bool isBlockSeal(uint64_t _now);

    /// check the block is follow SHDpos  in mine creater_time
    /// @paramer _ph : the last block in chain
	bool checkPreviousBlock(BlockHeader const& _ph) const;
    void sendDataToNodeMonitor();
private:
    ChainParams                     m_params;         
    Logger                          m_logger{createLogger(VerbosityInfo, "DposClinet")};
    p2p::Host                       &m_p2pHost;
    NodeMonitor                     m_nodemonitor;
    bool                            m_is_firt_run = true;
    bool                            m_isSendNodeStatus = false;
};

SHDposClient& asDposClient(Interface& _c);
SHDposClient* asDposClient(Interface* _c);

}  // namespace brc
}  // namespace dev
