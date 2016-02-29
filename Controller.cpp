#include "Controller.h"

#include <rtt.h>
#include<time.h>
#include<iostream>

#include"UlltraProto.h"

#include<fstream>

Controller::Controller(const Params &params)
{
#if _WIN32
    static bool needWSInit = true;

    if (needWSInit) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 0), &wsa);
        needWSInit = false;
    }
#endif


    LOG(logDEBUG) << "Node file: " << params.nodesFileName;
    std::ifstream nfs(params.nodesFileName);
    while(nfs.good() && !nfs.eof()) {
        std::string nh;
        nfs >> nh;
        nh = trim(nh);
        if(nh.empty() || nh[0] == '#') continue;
        Discovery::NodeDevice nd(nh);
        LOG(logINFO) << "Explicit node: " << nd;
        m_explicitNodes.push_back((nd));
        //m_discovery.addExplicitHosts(nh);
    }



    m_isRunning = true;
    RttThread::Routine updateThread(std::bind(&Controller::updateThreadMain, this, std::placeholders::_1));
    m_updateThread = new RttThread(updateThread);
	

	
	m_server.on("hello", [this](const NodeAddr &addr, const JsonNode &request, JsonNode &response) {
		response["name"] = m_discovery.getSelfName();
		response["id"] = m_discovery.getHwAddress();

		return;
	});
	/*
	m_server.on("link_eval", [](const NodeAddr &addr, const JsonObject &request, JsonObject &response) {
	
	});*/
}


Controller::~Controller()
{
}


void Controller::updateThreadMain(void *arg)
{
    m_discovery.onNodeDiscovered = [this](const Discovery::NodeDevice &node) {
        if (node.getId() > m_discovery.getHwAddress()) {
            LOG(logINFO) << "initiating link evaluation to node " << node;

            // request a latency test start
            m_discovery.send(UlltraProto::LatencyTestStartToken, node);
            m_linkEval.latencyTestMaster(node);
        }
    };


    m_discovery.onNodeLost = [](const Discovery::NodeDevice &node) {

    };

    m_discovery.on(UlltraProto::LatencyTestStartToken, [this](const Discovery::NodeDevice &node) {
        m_linkEval.latencyTestSlave(node);
    });

    if (!m_discovery.start(UlltraProto::DiscoveryPort)) {
        m_isRunning = false;
        LOG(logERROR) << "Discovery init failed!";
        return;
    }

    SocketAddress httpBindAddr(Discovery::NodeDevice::localAny.addrStorage);
    httpBindAddr.setPort(UP::HttpControlPort);

    if (!m_server.start(httpBindAddr.toString())) {
        LOG(logERROR) << "Control server init failed!";
        return;
    }


    if (!m_linkEval.init()) {
        m_isRunning = false;
        LOG(logERROR) << "Link evaluation init failed!";
        return;
    }

    time_t now;

    while (m_isRunning) {
        now = time(NULL);

        m_discovery.update(now);
        //m_linkEval.update(now);
        m_server.update();

        for(Discovery::NodeDevice &n : m_explicitNodes)
        {
			try {
				if (!n.alive() && difftime(now, n.timeLastConnectionTry) > 10) {
					n.timeLastConnectionTry = now;
					JsonNode hello;
					hello["name"] = m_discovery.getSelfName();
					hello["id"] = m_discovery.getHwAddress();
					auto resp = m_client.request(n.getAddr(UP::HttpControlPort), "hello", hello);
					

					resp["name"];
					resp["id"];
				}
			} 
			catch (...) {

			}
        }

#ifdef _WIN32
        Sleep(200);
#else
        usleep(1000*200);
#endif
    }

}

