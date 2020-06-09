#include "SHDposClient.h"
#include "ChainParams.h"
#include "SHDpos.h"

#include <libbrcdchain/Verify.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Log.h>
#include <time.h>
#include <boost/filesystem/path.hpp>


using namespace std;
using namespace dev;
using namespace dev::brc;
using namespace p2p;
using namespace dev::bacd;
namespace fs = boost::filesystem;

SHDposClient& dev::bacd::asDposClient(Interface& _c)
{
    if (dynamic_cast<SHDpos*>(_c.sealEngine()))
        return dynamic_cast<SHDposClient&>(_c);
    throw InvalidSHDposSealEngine();
}

SHDposClient* dev::bacd::asDposClient(Interface* _c)
{
    if (dynamic_cast<SHDpos*>(_c->sealEngine()))
        return &dynamic_cast<SHDposClient&>(*_c);
    throw InvalidSHDposSealEngine();
}

dev::bacd::SHDposClient::SHDposClient(ChainParams const& _params, int _networkID, p2p::Host& _host,
    std::shared_ptr<GasPricer> _gpForAdoption, boost::filesystem::path const& _dbPath,
    boost::filesystem::path const& _snapshotPath, WithExisting _forceAction,
    DBBlockConfig const& db_config, TransactionQueue::Limits const& _l)
  : Client(_params, _networkID, _host, _gpForAdoption, _dbPath, _snapshotPath, _forceAction, _l,
        db_config),
    // m_nodemonitor(_host.Networkrlp(), _params.getnodemonitorIp()),
    m_p2pHost(_host)
{
    // will throw if we're not an dpos seal engine.
    asDposClient(*this);
    m_params = _params;
    init(_host, _networkID);
    LOG(m_logger) << "init the dposClient check state : number: " << bc().info().number()
                  << " hash: " << bc().info().hash();
    if (!_params.getnodemonitorIp().empty())
    {
        m_isSendNodeStatus = true;
        m_nodemonitor.setMonitorParams(_host.Networkrlp(), _params.getnodemonitorIp());
    }
}

dev::bacd::SHDposClient::~SHDposClient()
{
    cwarn << "will close SHDposClient";
    bc().clean_cached_blocks(m_stateDB, m_StateExDB);
    // to wake up the thread from Client::doWork()
    m_signalled.notify_all();

    terminate();
}

SHDpos* dev::bacd::SHDposClient::dpos() const
{
    return dynamic_cast<SHDpos*>(Client::sealEngine());
}

void dev::bacd::SHDposClient::startSealing()
{
    setName("DposClient");
    if (m_params.m_block_addr_keys.find(author()) == m_params.m_block_addr_keys.end())
    {
        cwarn << " the author:" << author() << " not have private key....";
        return;
    }
    Client::startSealing();
}

int dev::bacd::SHDposClient::chainId() const
{
    return m_params.chainID;
}

void dev::bacd::SHDposClient::doWork(bool _doWait)
{
    try
    {
        bool t = true;
        // compare_exchange_strong(T& expected, T val, ...)

        if (m_syncBlockQueue.compare_exchange_strong(t, false))
        {
            syncBlockQueue();
            if (m_isSendNodeStatus)
            {
                sendDataToNodeMonitor();
            }
        }


        if (m_needStateReset)
        {
            cerror << " :SHDposClient::doWork   resetState";
            resetState();
            m_needStateReset = false;
        }

        t = true;
        bool isSealed = false;
        DEV_READ_GUARDED(x_working)
        isSealed = m_working.isSealed();

        if (!isSealed && !isMajorSyncing() && !m_remoteWorking &&
            m_syncTransactionQueue.compare_exchange_strong(t, false))
            syncTransactionQueue();

        tick();
        rejigSealing();
        callQueuedFunctions();

        DEV_READ_GUARDED(x_working)
        isSealed = m_working.isSealed();
        // If the block is sealed, we have to wait for it to tickle through the block queue
        // (which only signals as wanting to be synced if it is ready).
        if (!m_syncBlockQueue && !m_syncTransactionQueue && (_doWait || isSealed) && isWorking())
        {
            std::unique_lock<std::mutex> l(x_signalled);
            m_signalled.wait_for(l, chrono::milliseconds(10));
        }
    }
    catch (const boost::exception& e)
    {
        cwarn << boost::diagnostic_information(e);
    }
    catch (const std::exception& e)
    {
        cwarn << e.what();
    }
    catch (...)
    {
        cwarn << "unkown exception.";
    }
}

void dev::bacd::SHDposClient::sendDataToNodeMonitor()
{
    if (utcTimeMilliSec() - 30 * 1000 < bc().info().timestamp())
    {
        monitorData _data = {bc().number(), bc().info().author(), bc().info().hash(),
            bc().info().gasUsed(), utcTimeMilliSec() - bc().info().timestamp(),
            bc().transactions().size(), pending().size(), m_p2pHost.peerCount(), utcTimeMilliSec(),
            m_p2pHost.peerSessionInfo()};
        // cnote << "sendDataToNodeMonitor threadid: " << std::this_thread::get_id();
        m_nodemonitor.setData(_data);
    }
}

/// if the block have manch transaction this function will use much time
/// so can't use the function
void dev::bacd::SHDposClient::getCurrCreater(
    CreaterType _type, std::vector<Address>& _creaters) const
{
    std::vector<PollData> creaters;
    switch (_type)
    {
    case Canlitor:
        creaters = preSeal().mutableVote().CanlitorAddress();
        break;
    case Varlitor:
        creaters = preSeal().mutableVote().VarlitorsAddress();
        break;
    default:
        break;
    }
    _creaters.clear();
    for (auto const& val : creaters)
        _creaters.push_back(val.m_addr);
}

bool dev::bacd::SHDposClient::verifyVarlitorPrivatrKey() const
{
    return m_params.m_block_addr_keys.find(author()) != m_params.m_block_addr_keys.end();
}


void dev::bacd::SHDposClient::rejigSealing()
{
    if (!m_wouldSeal)
        return;
    if ((wouldSeal() || remoteActive()) && !isSyncing())
    {
        if (!verifyVarlitorPrivatrKey())
        {
            cwarn << "not find private key..";
            return;
        }

        // verify block_rounds/seal_block/minner ....
        if (!isBlockSeal(utcTimeMilliSec()))
        {
            return;
        }

        if (m_is_firt_run)
        {
            m_is_firt_run = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(m_params.blockInterval*2));  // sleep : wait to sync
            return;
        }

        if (sealEngine()->shouldSeal(this))
        {
            m_wouldButShouldnot = false;

            //  check the parent autor is true id SHDpod
            //  if false : will reset the block current state example : time, blocl_num ...
            if (!checkPreviousBlock(m_working.previousBlock()))
            {
                // m_working.mutableState().exdb().rollback();
                m_working.resetCurrent();
                syncTransactionQueue();
                LOG(m_logger) << "the last author not created block and will reset current data to "
                                 "seal block...";
            }

            // LOG(m_loggerDetail) << "Rejmeigging seal engine...";
            DEV_WRITE_GUARDED(x_working)
            {
                if (m_working.isSealed())
                {
                    LOG(m_logger) << "Tried to seal sealed block...";
                    return;
                }

                // TODO is that needed? we have "Generating seal on" below
                m_working.commitToSeal(bc(), m_extraData);
            }
            DEV_READ_GUARDED(x_working)
            {
                DEV_WRITE_GUARDED(x_postSeal)
                m_postSeal = m_working;
                m_sealingInfo = m_working.info();
                auto author = m_working.author();

                if (!m_params.m_block_addr_keys.count(author))
                {
                    cerror << "not find author : " << author
                           << "private key , please set private key.";
                    return;
                }
                else
                {
                    m_sealingInfo.sign_block(m_params.m_block_addr_keys.at(author));
                }
            }
            //出块
            if (wouldSeal())
            {
                //调用父类接口 声明回调，提供证明后调用 保存在 m_onSealGenerated
                sealEngine()->onSealGenerated([=](bytes const& _header) {
                    if (this->submitSealed(_header))
                    {
                        m_onBlockSealed(_header);
                    }
                    else
                        LOG(m_logger) << "Submitting block failed...";
                });
                ctrace << "Generating seal on "
                       << m_sealingInfo.hash((IncludeSeal)(WithoutSeal | WithoutSign)) << " #"
                       << m_sealingInfo.number();
                sealEngine()->generateSeal(m_sealingInfo);
            }
        }
        else
            m_wouldButShouldnot = true;
    }
    if (!m_wouldSeal)
    {
        sealEngine()->cancelGeneration();
    }

    return;
}

void dev::bacd::SHDposClient::syncTransactionQueue()
{
    resyncStateFromChain();

    h256Hash changeds;
    TransactionReceipts newPendingReceipts;
    DEV_WRITE_GUARDED(x_working)
    {
        if (m_working.isSealed())
        {
            ctrace << "Skipping txq sync for a sealed block.";
            return;
        }
        int _exc_time = (m_params.blockInterval ? m_params.blockInterval * 2 / 5 : 400) -
                        m_working.exc_transaction_time();
        if (_exc_time <= 20)
            return;
        tie(newPendingReceipts, m_syncTransactionQueue) =
            m_working.sync(bc(), m_tq, *m_gp, _exc_time);
    }

    if (newPendingReceipts.empty())
    {
        auto s = m_tq.status();
        ctrace << "No transactions to process. " << m_working.pending().size() << " pending, "
               << s.current << " queued, " << s.future << " future, " << s.unverified
               << " unverified";
        return;
    }

    DEV_READ_GUARDED(x_working)
    DEV_WRITE_GUARDED(x_postSeal)
    m_postSeal = m_working;

    DEV_READ_GUARDED(x_postSeal)
    for (size_t i = 0; i < newPendingReceipts.size(); i++)
        appendFromNewPending(newPendingReceipts[i], changeds, m_postSeal.pending()[i].sha3());

    // Tell farm about new transaction (i.e. restart mining).
    onPostStateChanged();

    // Tell watches about the new transactions.
    noteChanged(changeds);
}

void dev::bacd::SHDposClient::init(p2p::Host& _host, int _netWorkId)
{
    CP2P_LOG << "capabilityHost :: SHDposHostCapability";
    auto brcCapability = make_shared<SHDposHostcapability>(
        _host.capabilityHost(), bc(), m_stateDB, m_tq, m_bq, _netWorkId);
    _host.registerCapability(brcCapability);
    m_SHDpos_host = brcCapability;
    /// broadcastBlock
    m_cb_blocks = this->setOnBlockImport([&](BlockHeader const& _info) {
        auto h = m_SHDpos_host.lock();
        if (h)
        {
            CP2P_LOG << "insert block hash";
            h->OnBlockImport(_info);
            //TODO update lastImport
        }
    });

    m_cb_trxHash = this->setOnTrxHash([&](h256 const& _h){
        auto h = m_SHDpos_host.lock();
        if (h)
        {
            CP2P_LOG << "insert trx hash";
            h->broadcastTransaction(_h);
            //TODO update lastImport
        }
    });
    // m_tq.onReady([&](h256Hash const& _trxHash) {  
    //     CP2P_LOG << "TODO , broadcast Transaction..";     
    //     auto h = m_SHDpos_host.lock();
    //     if(h){
    //         for(auto _h : _trxHash)
    //         {
    //             h->broadcastTransaction(_h);
    //         }
    //     }
    // });


    dpos()->initConfigAndGenesis(m_params);
    dpos()->setDposClient(this);

    if(auto h = m_SHDpos_host.lock()){
        h->setBlockInterval(m_params.blockInterval);
    }
}

bool dev::bacd::SHDposClient::isBlockSeal(uint64_t _now)
{
    // LOG(m_logger) << "start to check time and varlitor ...";
    //验证时间 考虑创世区块时间
    if (!dpos()->checkDeadline(_now))
        return false;

    //验证出块人，周期
    if (!dpos()->isBolckSeal(_now))
        return false;
    return true;
}

bool dev::bacd::SHDposClient::checkPreviousBlock(BlockHeader const& _ph) const
{
    Address _pAddr = _ph.author();
    if (_pAddr == Address())
        return true;

    int64_t curr_time =
        utcTimeMilliSec() / dpos()->dposConfig().blockInterval * dpos()->dposConfig().blockInterval;
    if (m_working.info().timestamp() < curr_time)
        return false;
    return true;
}

bool dev::bacd::SHDposClient::verify_standby(
    int64_t block_time, const dev::Address& own_addr, uint32_t varlitorInterval_time) const
{
    Verify verify_standby;
    return verify_standby.verify_standby(preSeal().mutableState(), block_time, own_addr,
        varlitorInterval_time, preSeal().info().number() >= config::newChangeHeight());
}


bool dev::bacd::SHDposClient::is_sync() const
{
    if (auto h = m_SHDpos_host.lock())
    {
        SHDposSyncState state = h->status();
        return state != SHDposSyncState::Idle || h->bq().items().first > 10;
    }
    return false;
}

bool dev::bacd::SHDposClient::isSyncing() const
{
    if (auto h = m_SHDpos_host.lock())
        return h->isSyncing();
    return false;
}
