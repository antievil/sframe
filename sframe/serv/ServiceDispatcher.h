﻿
#ifndef SFRAME_SERVICE_DISPATCHER_H
#define SFRAME_SERVICE_DISPATCHER_H

#include <assert.h>
#include <memory.h>
#include <memory>
#include <vector>
#include <set>
#include <unordered_map>
#include <thread>
#include <algorithm>
#include "../util/BlockingQueue.h"
#include "../util/Singleton.h"
#include "../util/Serialization.h"
#include "Message.h"
#include "ProxyServiceMsg.h"
#include "AdminCmd.h"

namespace sframe{

class IoService;
class Service;
class Listener;
class ConnDistributeStrategy;


// 周期定时器
struct CycleTimer
{
	CycleTimer(int32_t sid, int32_t period) : sid(sid), next_time(0)
	{
		msg = std::make_shared<CycleMessage>(period);
	}

	int32_t sid;                          // 服务ID
	int64_t next_time;                    // 下次执行时间
	std::shared_ptr<CycleMessage> msg;    // 周期消息
};

// 服务调度器
class ServiceDispatcher : public singleton<ServiceDispatcher>, public noncopyable
{
public:

	ServiceDispatcher();

    ~ServiceDispatcher();

	// 发消息
	void SendMsg(Service * s, const std::shared_ptr<Message> & msg);

	// 发消息
	void SendMsg(int32_t sid, const std::shared_ptr<Message> & msg);

    // 发送消息
	template<typename T>
	void SendMsg(int32_t sid, const std::shared_ptr<T> & msg);

	// 发送内部服务消息
	template<typename... T_Args>
	void SendInsideServiceMsg(int32_t src_sid, int32_t dest_sid, int64_t session_key, uint16_t msg_id, T_Args&... args);

	// 发送网络服务消息
	template<typename... T_Args>
	void SendNetServiceMsg(int32_t src_sid, int32_t dest_sid, int64_t session_key, uint16_t msg_id, T_Args&... args);

	// 发送服务消息
	template<typename... T_Args>
	void SendServiceMsg(int32_t src_sid, int32_t dest_sid, int64_t session_key, uint16_t msg_id, T_Args&... args);

	// 设置远程服务监听地址
	void SetServiceListenAddr(const std::string & ip, uint16_t port);

	// 设置服务器管理监听地址
	void SetAdminListenAddr(const std::string & ip, uint16_t port);

	// 设置自定义监听地址
	bool SetCustomListenAddr(const std::string & desc_name, const std::string & ip, uint16_t port, const std::set<int32_t> & handle_services, ConnDistributeStrategy * distribute_strategy = nullptr);

	// 设置自定义监听地址
	bool SetCustomListenAddr(const std::string & desc_name, const std::string & ip, uint16_t port, int32_t handle_service);

    // 开始
    bool Start(int32_t thread_num);

    // 停止
    void Stop();

	// 调度服务(将指定服务压入调度队列)
	void Dispatch(Service * s);

    // 注册工作服务
	bool RegistService(int32_t sid, Service * service);

	// 注册远程服务
	bool RegistRemoteService(int32_t sid, const std::string & remote_ip, uint16_t remote_port);

	// 注册管理命令处理方法
	void RegistAdminCmd(const std::string & cmd, const AdminCmdHandleFunc & func);

	// 指定服务ID是否是本地服务
	bool IsLocalService(int32_t sid) const;

	// 获取IO服务
	const std::shared_ptr<IoService> & GetIoService() const
	{
		return _ioservice;
	}


private:

	// IO线程函数
	static void ExecIO(ServiceDispatcher * dispatcher);

	// 工作线程函数
	static void ExecWorker(ServiceDispatcher * dispatcher);

	// 准备代理服务
	Service * RepareProxyServer();

	// 获取服务
	Service * GetService(int32_t sid) const;

private:

	static const int32_t kServiceArrLen = 10000;                  // 服务数组长度

	std::unordered_map<int32_t, Service*> _all_service;           // 所有的本地服务
    Service * _services_arr[kServiceArrLen];                      // 服务数组，将sid小于kServiceArrLen的服务，拷贝一份在数组中，便于快速查找
    bool _running;                                                // 是否正在运行
    std::vector<std::thread*> _logic_threads;                     // 所有逻辑线程
	std::thread * _io_thread;                                     // IO线程（IO操作，已经周期定时检测）
	std::shared_ptr<IoService> _ioservice;                        // IO服务指针
	std::vector<Listener*> _listeners;                            // 监听器
	BlockingQueue<Service*> _dispach_service_queue;               // 服务调度队列
	std::vector<CycleTimer*> _cycle_timers;                       // 周期定时器列表
};

// 发送消息
template<typename T>
void ServiceDispatcher::SendMsg(int32_t sid, const std::shared_ptr<T> & msg)
{
	static_assert(std::is_base_of<Message, T>::value, "Message is not T Base");
	std::shared_ptr<Message> base_msg(msg);
	SendMsg(sid, base_msg);
}

// 发送内部服务消息
template<typename... T_Args>
void ServiceDispatcher::SendInsideServiceMsg(int32_t src_sid, int32_t dest_sid, int64_t session_key, uint16_t msg_id, T_Args&... args)
{
	std::shared_ptr<InsideServiceMessage<typename std::decay<T_Args>::type ...>> msg =
		std::make_shared<InsideServiceMessage<typename std::decay<T_Args>::type ...>>(std::forward<T_Args>(args)...);
	msg->src_sid = src_sid;
	msg->dest_sid = dest_sid;
	msg->session_key = session_key;
	msg->msg_id = msg_id;
	SendMsg(dest_sid, msg);
}

// 发送网络服务消息
template<typename... T_Args>
void ServiceDispatcher::SendNetServiceMsg(int32_t src_sid, int32_t dest_sid, int64_t session_key, uint16_t msg_id, T_Args&... args)
{
	std::shared_ptr<ProxyServiceMessageT<T_Args...>> msg = std::make_shared<ProxyServiceMessageT<T_Args...>>(args...);
	msg->src_sid = src_sid;
	msg->dest_sid = dest_sid;
	msg->session_key = session_key;
	msg->msg_id = msg_id;
	SendMsg(0, msg);
}

// 发送服务消息
template<typename... T_Args>
void ServiceDispatcher::SendServiceMsg(int32_t src_sid, int32_t dest_sid, int64_t session_key, uint16_t msg_id, T_Args&... args)
{
	Service * s = GetService(dest_sid);
	if (s)
	{
		std::shared_ptr<InsideServiceMessage<T_Args...>> msg = std::make_shared<InsideServiceMessage<T_Args...>>(args...);
		msg->src_sid = src_sid;
		msg->dest_sid = dest_sid;
		msg->session_key = session_key;
		msg->msg_id = msg_id;
		std::shared_ptr<Message> base_msg(msg);
		SendMsg(s, base_msg);
	}
	else
	{
		SendNetServiceMsg(src_sid, dest_sid, session_key, msg_id, args...);
	}
}

#define SERVICE_DISPATCHER (sframe::ServiceDispatcher::Instance())

}

#endif