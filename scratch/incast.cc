#include <iostream>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"
#include <ns3/internet-apps-module.h>
#include <ns3/ofswitch13-module.h>


// Network topology (default)
//
//        n2 n3 n4              .
//         \ | /                .
//          \|/                 .
//     n1--- s1---n8 (Server)   .
//          /|\                 .
//         / | \                .
//        n5 n6 n7              .
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Incast");

class OFSwitch13TsfccControllers;

std::stringstream filePlotQueue;
std::stringstream filePlotThroughput;

static bool firstCwnd = true;
static bool firstSshThr = true;
static bool firstRtt = true;
static bool firstRto = true;
static Ptr<OutputStreamWrapper> cWndStream;
static Ptr<OutputStreamWrapper> rWndStream;
static Ptr<OutputStreamWrapper> ssThreshStream;
static Ptr<OutputStreamWrapper> rttStream;
static Ptr<OutputStreamWrapper> rtoStream;
static uint32_t cWndValue;
static uint32_t ssThreshValue;
static double interval = 0.001;
uint64_t flowRecvBytes = 0;
std::ofstream tThtoughPut;

static void
CwndTracer (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd)
    {
      *cWndStream->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd = false;
    }
  *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue = newval;

  if (!firstSshThr)
    {
      *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << ssThreshValue << std::endl;
    }
}

static void
RwndTracer(uint32_t oldval, uint32_t newval){
  *rWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
}

static void
SsThreshTracer (uint32_t oldval, uint32_t newval)
{
  if (firstSshThr)
    {
      *ssThreshStream->GetStream () << "0.0 " << oldval << std::endl;
      firstSshThr = false;
    }
  *ssThreshStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  ssThreshValue = newval;

  if (!firstCwnd)
    {
      *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << cWndValue << std::endl;
    }
}

static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}

static void
RtoTracer (Time oldval, Time newval)
{
  if (firstRto)
    {
      *rtoStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRto = false;
    }
  *rtoStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
}


static void
TraceCwnd (std::string cwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  cWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow", MakeCallback (&CwndTracer));
}

static void
TraceRwnd (std::string rwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  rWndStream = ascii.CreateFileStream (rwnd_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/RWND", MakeCallback (&RwndTracer));
}

static void
TraceSsThresh (std::string ssthresh_tr_file_name)
{
  AsciiTraceHelper ascii;
  ssThreshStream = ascii.CreateFileStream (ssthresh_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/SlowStartThreshold", MakeCallback (&SsThreshTracer));
}

static void
TraceRtt (std::string rtt_tr_file_name)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/RTT", MakeCallback (&RttTracer));
}

static void
TraceRto (std::string rto_tr_file_name)
{
  AsciiTraceHelper ascii;
  rtoStream = ascii.CreateFileStream (rto_tr_file_name.c_str ());
  Config::ConnectWithoutContext ("/NodeList/2/$ns3::TcpL4Protocol/SocketList/0/RTO", MakeCallback (&RtoTracer));
}

void
CheckQueueSize (Ptr<QueueDisc> queue, std::string filePlotQueue)
{
  uint32_t qSize = StaticCast<RedQueueDisc> (queue)->GetNPackets();

  // check queue size every 1/1000 of a second
  Simulator::Schedule (Seconds (0.0001), &CheckQueueSize, queue, filePlotQueue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

void
CheckOfQueueSize (Ptr<OFSwitch13Queue> ofQue, std::string filePlotQueue)
{
  uint32_t qSize = ofQue->GetNPackets();

  // check queue size every 1/1000 of a second
  Simulator::Schedule (Seconds (0.0001), &CheckOfQueueSize, ofQue, filePlotQueue);

  std::ofstream fPlotQueue (filePlotQueue.c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

void
ThroughputPerSecond(Ptr<PacketSink> sink1Apps,std::string filePlotThrough)
{
	uint32_t totalRecvBytes = sink1Apps->GetTotalRx();
	uint32_t currentPeriodRecvBytes = totalRecvBytes - flowRecvBytes;

	flowRecvBytes = totalRecvBytes;

	Simulator::Schedule (Seconds(interval), &ThroughputPerSecond, sink1Apps, filePlotThrough);
	std::ofstream fPlotThroughput (filePlotThrough.c_str (), std::ios::out | std::ios::app);
	fPlotThroughput << Simulator::Now().GetSeconds() << " " << currentPeriodRecvBytes * 8 / (interval * 1000000) << std::endl;
}

void InstallApplication(uint16_t port, NodeContainer senders, Ipv4InterfaceContainer receIpIfaces, 
                        NodeContainer receiver, double start_time, double stop_time, 
                        uint32_t TCP_SEGMENT, double data_mbytes, uint32_t sendNum){
  // Configure port/address and install application, establish connection
  for (uint32_t i = 0; i < senders.GetN(); ++i) {
    V4PingHelper pingHelper(receIpIfaces.GetAddress(0)); // 使用服务器的IP地址
    pingHelper.SetAttribute("Verbose", BooleanValue(false));

    ApplicationContainer singleHostPingApps = pingHelper.Install(senders.Get(i));
    singleHostPingApps.Start(Seconds(0.3));

    // 将Ping应用程序添加到总的Ping应用程序容器
    // pingApps.Add(singleHostPingApps);
  }
  // long flow
  for(uint16_t i=0; i<senders.GetN(); i++)
  {
	  Address clientAddress(InetSocketAddress(receIpIfaces.GetAddress(0,0), port));
    BulkSendHelper sourceHelper ("ns3::TcpSocketFactory", clientAddress);
    sourceHelper.SetAttribute("SendSize", UintegerValue(TCP_SEGMENT));
    sourceHelper.SetAttribute ("MaxBytes", UintegerValue (data_mbytes/sendNum));
    ApplicationContainer sourceApp = sourceHelper.Install (senders.Get(i));
    sourceApp.Start (Seconds (start_time));
  }

}


//定时查询某个交换机的所有端口的所有队列长度
void QueryAllQueLength(Ptr<OFSwitch13Device> openFlowDev) {
  //获取交换机的端口数量
  size_t portSize = openFlowDev->GetSwitchPortSize();
  uint64_t dpid = openFlowDev->GetDpId();
  for(uint32_t i = 0; i < portSize; i++){
    Ptr<OFSwitch13Port> ofPort = openFlowDev->GetSwitchPort(i+1);
    Ptr<OFSwitch13Queue> ofQue = ofPort->GetPortQueue();
    uint16_t queueLength = ofQue->GetNPackets();
    uint16_t state = ofPort->GetCongestionState();
    uint16_t count = ofPort->GetCongestionRecoverCount();
    uint32_t port_no = i+1;
    //判断是否大于阈值
    if(queueLength > 20){
      // NS_LOG_INFO("The Port " << i+1 << " queueLength is " << queueLength);
      if(state == 0){
        state = 1;
        ofPort->SetCongestionState(state);
      }
      openFlowDev->SendQueueCongestionNotifyMessage(dpid,queueLength,port_no);
      count = 0;
      ofPort->SetCongestionRecoverCount(count);
    }else{
      if(state == 1){
        count++;
        ofPort->SetCongestionRecoverCount(count);
      }
      if(count == 3){
        openFlowDev->SendQueueCongestionRecoverMessage(dpid,queueLength,port_no);
        // NS_LOG_INFO("The count is " << count);
        count = 0;
        state = 0;
        ofPort->SetCongestionRecoverCount(count);
        ofPort->SetCongestionState(state);
      }
    }
    //OFSwitch13Device构造发送函数，发送到控制器
  }
  
  // Reschedule the function call
  Time delay = MicroSeconds(10); // Set the desired time interval
  Simulator::Schedule(delay, &QueryAllQueLength, openFlowDev);
}


int main (int argc, char *argv[])
{
  // Configure information
  std::string prefix_file_name = "Incast";
  uint32_t sendNum = 5;//(1~50)
  std::string transport_port = "TcpCubic";// TcpDctcp  TcpCubic
//   bool tsfcc_flag = false;
  std::string queue_disc_type = "RedQueueDisc";//only for dctcp
  // todo
  std::string data_link_layer_protocol = "csma";// csma   point_to_point
  bool is_sdn = true;// is SDN enabled?

  std::string queue_limit = "100p";//94
  double K = 18;//18
//   uint32_t DcvegasNqK = 4;

  std::string bandwidth = "1000Mbps";
  uint64_t delay = 10;
  std::string bottleneck_bw = "1000Mbps";
  uint64_t bottleneck_delay = 10;

  uint32_t IP_PACKET_SIZE = 1500;
  uint32_t TCP_SEGMENT = IP_PACKET_SIZE-40;
  double data_mbytes = 2*1024*1024;

  double minRto = 200;
  uint32_t initialCwnd = 2;

  double start_time = 4;
  double stop_time = 5;

  bool tracing = true;

  // Create directory information
 time_t rawtime;
 struct tm * timeinfo;
 char buffer[80];
 time (&rawtime);
 timeinfo = localtime(&rawtime);
//
 strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
 std::string currentTime (buffer);

  CommandLine cmd;
  cmd.AddValue ("sendNum","Number of left and right side leaf nodes", sendNum);
  cmd.AddValue ("tracing","Whether to log and track information", tracing);
  cmd.AddValue ("queuedisc","type of queuedisc", queue_disc_type);
  cmd.AddValue ("bandwidth", "Access bandwidth", bandwidth);
  cmd.AddValue ("delay", "Access delay", delay);
  cmd.AddValue ("bottleneck_bw", "Bottleneck bandwidth", bottleneck_bw);
  cmd.AddValue ("bottleneck_delay", "Bottleneck delay", bottleneck_delay);
  cmd.AddValue ("TCP_SEGMENT", "Packet size", TCP_SEGMENT);
  cmd.AddValue ("data", "Number of Megabytes of data to transmit, 0 means infinite", data_mbytes);
  cmd.AddValue ("start_time", "Start Time", start_time);
  cmd.AddValue ("stop_time", "Stop Time", stop_time);
  cmd.AddValue ("initialCwnd", "Initial Cwnd", initialCwnd);
  cmd.AddValue ("duplex",   "ns3::CsmaChannel::FullDuplex");
  cmd.AddValue ("minRto", "Minimum RTO", minRto);
  cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpDctcp, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
                "TcpLp, TcpBbr", transport_port);
  cmd.Parse (argc,argv);

  // To enable logging
  LogComponentEnable("Incast", LOG_LEVEL_INFO);
  // LogComponentEnable ("TcpRateOps", LOG_LEVEL_INFO);
//  LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO);
//  LogComponentEnable("RedQueueMMDisc", LOG_LEVEL_DEBUG);
//  LogComponentEnable("FifoQueueDisc", LOG_LEVEL_LOGIC);
//  LogComponentEnable("Ipv4QueueDiscItem", LOG_LEVEL_DEBUG);
//  LogComponentEnable("OFSwitch13TsfccController", LOG_LEVEL_ALL);
// LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);
//  LogComponentEnable("TcpTxBuffer", LOG_LEVEL_INFO);
//  LogComponentEnable("TcpDctcp", LOG_LEVEL_INFO);

  NS_LOG_INFO ("Configure TcpSocket");
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::" + transport_port));
  Config::SetDefault ("ns3::TcpSocketBase::MinRto", TimeValue (MilliSeconds (minRto)));
  Config::SetDefault ("ns3::TcpSocketBase::Timestamp", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (true));
  // Config::SetDefault ("ns3::RttMeanDeviation::Alpha", DoubleValue (1));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1460));
  Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue (1));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (initialCwnd));
  Config::SetDefault ("ns3::RttEstimator::InitialEstimation", TimeValue (MicroSeconds (100)));

  if(transport_port.compare("TcpDctcp") == 0 || transport_port.compare("TcpDstcp") == 0)
  {
	  NS_LOG_INFO ("Configure ECN and RED");
	  Config::SetDefault ("ns3::"+queue_disc_type+"::UseEcn", BooleanValue (true));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::MeanPktSize", UintegerValue (IP_PACKET_SIZE));
	  Config::SetDefault("ns3::"+queue_disc_type+"::QW", DoubleValue (1));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MinTh", DoubleValue (K));
	  Config::SetDefault("ns3::"+queue_disc_type+"::MaxTh", DoubleValue (K));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::Gentle", BooleanValue (false));
	  Config::SetDefault ("ns3::"+queue_disc_type+"::UseHardDrop", BooleanValue (false));
  }
  else
  {
	  NS_LOG_INFO ("Configure Fifo");
	  queue_disc_type = "FifoQueueDisc";
	  Config::SetDefault("ns3::FifoQueueDisc::MaxSize", QueueSizeValue (QueueSize (queue_limit)));
  }

  // Create sendNum, receiver, and switches
  NodeContainer switches;
  switches.Create(1);
  NodeContainer senders;
  senders.Create(sendNum);
  NodeContainer receiver;
  receiver.Create(1);

  NetDeviceContainer hostDevices;
  NetDeviceContainer switchPorts;
  NetDeviceContainer receDevices;

  Ipv4InterfaceContainer receIpIfaces;
  CsmaHelper csmaHelper;
  CsmaHelper csmaNeckLink;
  PointToPointHelper ptpLink;
  PointToPointHelper ptpNeckLink;
  QueueDiscContainer queueDiscs;
  Ptr<OFSwitch13Device> openFlowDev;
  if(is_sdn == true){
    if(data_link_layer_protocol.compare("csma") == 0){
      //利用SDN和全双工csma设置链路等
      // Configure channel attributes
      csmaHelper.SetChannelAttribute("DataRate", DataRateValue (DataRate (bandwidth)));
      csmaHelper.SetChannelAttribute("Delay", TimeValue (MicroSeconds (delay)));
      csmaHelper.SetChannelAttribute("FullDuplex", BooleanValue(true));

      csmaNeckLink.SetChannelAttribute("DataRate", DataRateValue (DataRate (bottleneck_bw)));
      csmaNeckLink.SetChannelAttribute("Delay", TimeValue (MicroSeconds (bottleneck_delay)));
      csmaNeckLink.SetChannelAttribute("FullDuplex", BooleanValue(true));

      // Install InternetStack
      InternetStackHelper stack;
      stack.Install (senders);
      stack.Install (receiver);

      // Configure Ipv4AddressHelper
      Ipv4AddressHelper address;
      address.SetBase("10.0.0.0", "255.255.255.0");

      // Configure net devices in nodes and connect sendNum with switches
      for(uint32_t i = 0; i<sendNum; i++)
      {
        NodeContainer pair (senders.Get(i), switches.Get(0));
        NetDeviceContainer link = csmaHelper.Install (pair);
        switchPorts.Add(link.Get(1));
        hostDevices.Add (link.Get (0));
      }

      // Connect switches with receiver
      NodeContainer pair (switches.Get(0), receiver.Get(0));
      NetDeviceContainer link = csmaNeckLink.Install (pair);
      switchPorts.Add(link.Get(0));
      receDevices.Add (link.Get (1));
      Ipv4InterfaceContainer hostIpIfaces = address.Assign (hostDevices);
      receIpIfaces = address.Assign (receDevices);

      Ptr<Node> controllerNode = CreateObject<Node> ();

      // 利用OFSwitch13InternalHelper配置openflow，安装上控制器和交换机，然后创建openflow信道
      Ptr<OFSwitch13InternalHelper> of13Helper = CreateObject<OFSwitch13InternalHelper> ();
      of13Helper->InstallController (controllerNode);
      openFlowDev = of13Helper->InstallSwitch (switches.Get(0), switchPorts);
      of13Helper->CreateOpenFlowChannels ();
      OFSwitch13Helper::EnableDatapathLogs ();
    }
  }else {
    if(data_link_layer_protocol.compare("point_to_point") == 0){
      // Configure channel attributes
      ptpLink.SetDeviceAttribute("DataRate", DataRateValue (DataRate (bandwidth)));
      ptpLink.SetChannelAttribute("Delay", TimeValue (MicroSeconds (delay)));

      ptpNeckLink.SetDeviceAttribute("DataRate", DataRateValue(DataRate(bottleneck_bw)));
      ptpNeckLink.SetChannelAttribute("Delay", TimeValue(MicroSeconds(bottleneck_delay)));

      // Install InternetStack
      InternetStackHelper stack;
      stack.InstallAll();

      // Configure TrafficControlHelper
      TrafficControlHelper tchRed;
      tchRed.SetRootQueueDisc("ns3::"+queue_disc_type);
      NS_LOG_INFO ("Install " << queue_disc_type);

      // Configure Ipv4AddressHelper
      Ipv4AddressHelper address;
      address.SetBase("10.0.0.0", "255.255.255.0");

      // Configure net devices in nodes and connect sendNum with switches
      for(uint32_t i = 0; i<sendNum; i++)
      {
        NetDeviceContainer devices;
        devices = ptpLink.Install(senders.Get(i), switches.Get(0));
        tchRed.Install(devices);
        address.NewNetwork();
        Ipv4InterfaceContainer interfaces = address.Assign(devices);
      }

      // Connect switches with receiver
      NetDeviceContainer devices;
      devices = ptpNeckLink.Install(switches.Get(0), receiver.Get(0));

      // Install queueDiscs in switch
      queueDiscs = tchRed.Install(devices);

      address.NewNetwork();
      Ipv4InterfaceContainer interfaces = address.Assign(devices);
      receIpIfaces.Add(interfaces.Get(1));

      // Configure routing
      NS_LOG_INFO ("Initialize Global Routing.");
      Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }else if(data_link_layer_protocol.compare("csma") == 0){
        // Configure channel attributes
        csmaHelper.SetChannelAttribute("DataRate", DataRateValue (DataRate (bandwidth)));
        csmaHelper.SetChannelAttribute("Delay", TimeValue (MicroSeconds (delay)));
        csmaHelper.SetChannelAttribute("FullDuplex", BooleanValue(true));

        csmaNeckLink.SetChannelAttribute("DataRate", DataRateValue (DataRate (bottleneck_bw)));
        csmaNeckLink.SetChannelAttribute("Delay", TimeValue (MicroSeconds (bottleneck_delay)));
        csmaNeckLink.SetChannelAttribute("FullDuplex", BooleanValue(true));

        // Install InternetStack
        InternetStackHelper stack;
        stack.InstallAll();

        // Configure TrafficControlHelper
        TrafficControlHelper tchRed;
        tchRed.SetRootQueueDisc("ns3::"+queue_disc_type);
        NS_LOG_INFO ("Install " << queue_disc_type);

        // Configure Ipv4AddressHelper
        Ipv4AddressHelper address;
        address.SetBase("10.0.0.0", "255.255.255.0");

        // Configure net devices in nodes and connect sendNum with switches
        Ipv4InterfaceContainer hostInterfaces;
        for(uint32_t i = 0; i<sendNum; i++)
        {
          NodeContainer pair (senders.Get(i), switches.Get(0));
          NetDeviceContainer link = csmaHelper.Install (pair);
          tchRed.Install(link);
          address.NewNetwork();
          Ipv4InterfaceContainer interfaces = address.Assign(link);
          hostInterfaces.Add(interfaces.Get(0));
        }

        // Connect switches with receiver
        NodeContainer pair (switches.Get(0), receiver.Get(0));
        NetDeviceContainer link = csmaNeckLink.Install (pair);
        // Install queueDiscs in switch
        queueDiscs = tchRed.Install(link);
        address.NewNetwork();
        Ipv4InterfaceContainer interfaces = address.Assign(link);
        receIpIfaces.Add(interfaces.Get(1));

        // Configure routing
        NS_LOG_INFO ("Initialize Global Routing.");
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    }
  }

  NS_LOG_INFO ("Build connections");
  uint16_t port = 50000;
  InstallApplication(port, senders, receIpIfaces, receiver, start_time, stop_time , TCP_SEGMENT, data_mbytes, sendNum);
  Address serverAddress(InetSocketAddress(Ipv4Address::GetAny(), port));
  PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", serverAddress);
  ApplicationContainer sinkApps = packetSinkHelper.Install(receiver.Get(0));
  sinkApps.Start(Seconds(start_time));
  sinkApps.Stop(Seconds(stop_time));
  
  // Collect data
  std::string dir = "incast/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
  std::cout << "Data directory:" << dir << std::endl;
  std::string pcap_dir = "pcap/" + transport_port.substr(0, transport_port.length()) + "/" + currentTime + "/";
  std::cout << "Pcap directory:" << pcap_dir << std::endl;
  if (tracing)
  {
    std::string dirToSave = "mkdir -p " + dir;
	  system (dirToSave.c_str ());
    std::string pcapDirToSave = "mkdir -p " + pcap_dir;
	  system (pcapDirToSave.c_str ());

	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceCwnd, dir+"/cwnd.data");
    Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRwnd, dir+"/rwnd.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceSsThresh, dir+"/ssth.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRtt, dir+"/rtt.data");
	  Simulator::Schedule (Seconds (start_time + 0.000001), &TraceRto, dir+"/rto.data");
    // of13Helper->EnableOpenFlowPcap ("openflow");
    // of13Helper->EnableDatapathStats ("switch-stats");
    // Get queue size
	  filePlotQueue <<  dir << "/" << "queue-size.plotme";
    if(data_link_layer_protocol.compare("csma") == 0){
      if(is_sdn == true){
        csmaHelper.EnablePcap (pcap_dir+"/switch", switchPorts, true);

        size_t portSize = openFlowDev->GetSwitchPortSize();
        Ptr<OFSwitch13Port> ofPort = openFlowDev->GetSwitchPort(portSize);
        Ptr<OFSwitch13Queue> ofQue = ofPort->GetPortQueue();
        Simulator::ScheduleNow (&CheckOfQueueSize, ofQue, filePlotQueue.str());
      }else{
        Ptr<QueueDisc> queue = queueDiscs.Get(0);
	      Simulator::ScheduleNow (&CheckQueueSize, queue, filePlotQueue.str());
      }
      csmaHelper.EnablePcap (pcap_dir+"/csmaHost", hostDevices);
      csmaHelper.EnablePcap (pcap_dir+"/receiveHost", receDevices);
    }else if(data_link_layer_protocol.compare("point_to_point") == 0){
      ptpLink.EnablePcapAll(pcap_dir+"/ptpHost", true);
      Ptr<QueueDisc> queue = queueDiscs.Get(0);
	    Simulator::ScheduleNow (&CheckQueueSize, queue, filePlotQueue.str());
    }

	  // Get delay
	  Simulator::Schedule (Seconds (start_time + 0.030001), &TraceRtt, "rtt.data");

	  // Get throughput
	  filePlotThroughput << dir << "/" << "throughput.plotme";
	  //remove (filePlotThroughput.str ().c_str());
	  Simulator::ScheduleNow (&ThroughputPerSecond, sinkApps.Get(0)->GetObject<PacketSink>(), filePlotThroughput.str ());
  }

  if(is_sdn == true){
    Time initialDelay = MicroSeconds(10); // Initial delay before the first execution
    Simulator::Schedule(initialDelay, &QueryAllQueLength, openFlowDev);
  }
  // Install FlowMonitor on all nodes
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  //AnimationInterface anim("dctcpVScubic.xml");
  Simulator::Stop (Seconds(stop_time));
  Simulator::Run ();

  // Get information from FlowMonitor
  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  double max_fct=0;
  uint32_t count=0;

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple FiveTuple = classifier->FindFlow (i->first);
      std::cout << "Flow " << i->first << " (" << FiveTuple.sourceAddress << " -> " << FiveTuple.destinationAddress << ")\n";
      std::cout << "  Statr time: " << i->second.timeFirstTxPacket << "\n";
      std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
      std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
      std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / 1000000 / (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
      std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
      std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
      std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / 1000000/ (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " Mbps\n";
      std::cout << "  FCT:  " << (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds() << " s\n";
      if(((i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds()>max_fct) && (count<sendNum))
      {
        max_fct = (i->second.timeLastRxPacket-i->second.timeFirstTxPacket).GetSeconds();
      }
  }
  double goodput = data_mbytes * 8.0 / 1000000 / max_fct;
  std::string filepath = "concurrentNum/" + transport_port.substr(0, transport_port.length()) + "/";
  if(is_sdn == true){
    filepath = "concurrentNum/sdn/";
  }
  std::string filename = filepath + "throughput.dat";
  std::string dirToCon = "mkdir -p " + filepath;
	system (dirToCon.c_str ());
  // 打开文件进行读取
  std::ifstream inputFile(filename);
  // 检查文件是否为空
  if (inputFile.peek() == std::ifstream::traits_type::eof()) {
      // 文件为空，可以向文件写入内容

      // 关闭输入文件流
      inputFile.close();

      // 打开文件进行写入
      std::ofstream outputFile(filename, std::ios::app);

      // 检查文件是否成功打开
      if (!outputFile.is_open()) {
          std::cerr << "Error opening file for writing: " << filename << std::endl;
          return 1;
      }

      // 写入内容
      outputFile << "#CurrentNum(个) Throughput(Mbps)" << std::endl;

      // 关闭输出文件流
      outputFile.close();

      std::cout << "Header added to the file." << std::endl;
  } else {
      // 文件不为空，只执行追加操作
      inputFile.close();
  }

  // 打开文件进行追加操作
  tThtoughPut.open(filename.c_str(), std::ios::out | std::ios::app);

  // 检查文件是否成功打开
  // if (!tThtoughPut.is_open()) {
  //     std::cerr << "Error opening file for appending: " << filename << std::endl;
  //     return 1;
  // }

  // 在这里继续写入其他内容
  tThtoughPut << sendNum << " " << goodput << std::endl;
  // 关闭输出文件流
  tThtoughPut.close();

  // std::cout << "goodput: " << goodput << " Mbps" << std::endl;
  // std::cout << "query FCT: " << max_fct << " s" << std::endl;

  // Collect data
  NS_LOG_INFO ("Collect data.");

  // std::ofstream myfile;
  // remove ((transport_port+"-incast-goodput.dat").c_str());
  // myfile.open (transport_port+"-incast-goodput.dat", std::fstream::in | std::fstream::out | std::fstream::app);
  // myfile << sendNum << " " << goodput << "\n";
  // myfile.close();

  Simulator::Destroy ();
  return 0;
}
