#include "manager.h"

using namespace fptn::vpn;


Manager::Manager(
    fptn::web::ServerPtr webServer,
    fptn::network::VirtualInterfacePtr networkInterface,
    const fptn::nat::TableSPtr& nat,
    const fptn::filter::FilterManagerSPtr& filter,
    const fptn::statistic::MetricsSPtr& prometheus
)
:
    webServer_(std::move(webServer)),
    networkInterface_(std::move(networkInterface)),
    nat_(nat),
    filter_(filter),
    prometheus_(prometheus)
{
}


Manager::~Manager()
{
    stop();
}


bool Manager::stop() noexcept
{
    running_ = false;
    if (readToClientThread_.joinable()) {
        readToClientThread_.join();
    }
    if (readFromClientThread_.joinable()) {
        readFromClientThread_.join();
    }
    if (collectStatistics_.joinable()) {
        collectStatistics_.join();
    }
    return (networkInterface_->stop() && webServer_->stop());
}


bool Manager::start() noexcept
{
    running_ = true;
    webServer_->start();
    networkInterface_->start();

    readToClientThread_ = std::thread(&Manager::runToClient, this);
    bool toStatus = readToClientThread_.joinable();

    readFromClientThread_ = std::thread(&Manager::runFromClient, this);
    bool fromStatus = readFromClientThread_.joinable();

    collectStatistics_ = std::thread(&Manager::runCollectStatistics, this);
    bool collectStatisticStatus = collectStatistics_.joinable();
    return (toStatus && fromStatus && collectStatisticStatus);
}

void Manager::runToClient() noexcept
{
    const std::chrono::milliseconds timeout{300};
    while (running_) {
        auto packet = networkInterface_->waitForPacket(timeout);
        if (!packet) {
            continue;
        }
        // get session
        auto session = nat_->getSessionByFakeIP(packet->ipLayer()->getDstIPv4Address());
        if (!session) {
            continue;
        }
        // check shaper
        auto shaper = session->getTrafficShaperToClient();
        if (shaper && !shaper->checkSpeedLimit(packet->size())) {
            continue;
        }
        // filter 
        packet = filter_->apply(std::move(packet));
        if (!packet) {
            continue;
        }
        // send
        webServer_->send(session->changeIPAddressToCleintIP(std::move(packet)));
    }
}


void Manager::runFromClient() noexcept
{
    constexpr std::chrono::milliseconds timeout{300};
    while (running_) {
        auto packet = webServer_->waitForPacket(timeout);
        if (!packet) {
            continue;
        }
        // get session
        auto session = nat_->getSessionByClientId(packet->clientId());
        if (!session) {
            continue;
        }
        // check shaper
        auto shaper = session->getTrafficShaperFromClient();
        if (shaper && !shaper->checkSpeedLimit(packet->size())) {
            continue;
        }
        // filter 
        packet = filter_->apply(std::move(packet));
        if (!packet) {
            continue;
        }
        // send
        networkInterface_->send(
            session->changeIPAddressToFakeIP(std::move(packet))
        );
    }
}

void Manager::runCollectStatistics() noexcept
{
    constexpr std::chrono::milliseconds timeout{300};
    constexpr std::chrono::seconds collectInterval{5};

    std::chrono::steady_clock::time_point lastCollectionTime;
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastCollectionTime > collectInterval) {
            nat_->updateStatistic(prometheus_);
            lastCollectionTime = now;
        }
        std::this_thread::sleep_for(timeout);
    }
}
